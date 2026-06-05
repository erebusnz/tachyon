/* USB-CDC test console for Tachyon IO bring-up.
 * Command set and bench procedures are documented in test-firmware.md.
 * One verb per IO function: the host commands a known output (scope
 * measures) or applies a stimulus (console reports what the MCU saw). */

#include "test_console.h"
#include "main.h"
#include "encoder.h"

#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>

#include "DEV_Config.h"
#include "OLED_1in5.h"
#include "GUI_Paint.h"

/* Peripheral handles owned by main.c. */
extern ADC_HandleTypeDef hadc1;
extern I2S_HandleTypeDef hi2s3;
extern SD_HandleTypeDef  hsd;
extern SPI_HandleTypeDef hspi1;
extern SPI_HandleTypeDef hspi2;
extern TIM_HandleTypeDef htim2;
extern TIM_HandleTypeDef htim3;
extern TIM_HandleTypeDef htim4;

/* ---- RX ring (single-producer in USB IRQ, single-consumer in poll) ---- */
#define RX_SZ 256
static volatile uint8_t  rx_buf[RX_SZ];
static volatile uint16_t rx_head;
static volatile uint16_t rx_tail;

void console_rx_byte(uint8_t b)
{
  uint16_t next = (uint16_t)((rx_head + 1u) % RX_SZ);
  if (next != rx_tail) {        /* drop on overflow rather than block the ISR */
    rx_buf[rx_head] = b;
    rx_head = next;
  }
}

/* ---- background jobs serviced from console_poll ---- */
#define I2S_FS     192000.0f
/* Continuous circular DMA buffer of DMA_FRAMES stereo frames, refilled one half
 * at a time from the I2S Tx half/complete callbacks. Streaming never stops, so
 * BCK/LRCK run continuously — the PCM5102A soft-mutes if its clocks halt, which
 * is why blocking per-buffer transmits left the output silent. 4 halfwords per
 * frame: L_hi,L_lo,R_hi,R_lo (32-bit I2S sent as halfword pairs). */
#define DMA_FRAMES  256
#define TONE_AMP    0x30000000  /* ~0.375 full-scale, headroom against clipping */
#define TWO_PI      6.283185307179586f

static struct {
  uint8_t  active;                 /* 1 = emit sine, 0 = emit digital silence */
  float    phase;
  float    inc;
  uint16_t buf[DMA_FRAMES * 4];
} tone;

static struct {
  uint8_t  mode;        /* 0 = static/idle, 1 = one-shot pulse, 2 = free-run clock */
  uint8_t  level;       /* current jack level (1 = high) */
  uint32_t t_next;      /* HAL tick of next edge */
  uint32_t half_ms;     /* half period for clock mode */
} gate_job[2];

static struct {
  uint8_t  active;
  uint32_t t_off;       /* HAL tick to turn off */
} led_blink;

static uint8_t  adc_stream;
static uint32_t adc_stream_t;
static int32_t  enc_count;
static uint32_t tick_1ms_prev;

/* ---- low-level IO helpers ---- */

static GPIO_TypeDef *gate_port(int ch) { return ch ? GATE_OUT_B_GPIO_Port : GATE_OUT_A_GPIO_Port; }
static uint16_t      gate_pin (int ch) { return ch ? GATE_OUT_B_Pin       : GATE_OUT_A_Pin; }

/* MOSFET inverts (gate-output.md): jack HIGH = GPIO LOW. We expose the
 * JACK level; firmware does the inversion here. */
static void gate_set(int ch, int jack_high)
{
  HAL_GPIO_WritePin(gate_port(ch), gate_pin(ch), jack_high ? GPIO_PIN_RESET : GPIO_PIN_SET);
  gate_job[ch].level = (uint8_t)(jack_high ? 1 : 0);
}

/* DAC8552 24-bit frame (DAC8552.md): [23:22]=0, [21]=channel(0=A,1=B),
 * [20]=load, [19:18]=power-down(00), [17:16]=0, [15:0]=data.
 * Physical CV-OUT-A is wired to DAC channel B and vice-versa
 * (cv-output-dac.md channel swap), so jack 0 -> chbit 1. */
static void dac_write(int jack, uint16_t code)
{
  uint8_t chbit = (jack == 0) ? 1u : 0u;
  uint8_t ctrl  = (uint8_t)((chbit << 5) | (1u << 4)); /* load, normal PD */
  uint8_t tx[3] = { ctrl, (uint8_t)(code >> 8), (uint8_t)(code & 0xFF) };

  HAL_GPIO_WritePin(DAC_SPI_CS_GPIO_Port, DAC_SPI_CS_Pin, GPIO_PIN_RESET);
  HAL_SPI_Transmit(&hspi2, tx, 3, 10);
  HAL_GPIO_WritePin(DAC_SPI_CS_GPIO_Port, DAC_SPI_CS_Pin, GPIO_PIN_SET);
}

/* hadc1 scans 3 ranks: [0]=PA0 CV_IN_A, [1]=PA1 CV_IN_B, [2]=PC0 USR_POT_1. */
static void adc_read_all(uint16_t v[3])
{
  HAL_ADC_Start(&hadc1);
  for (int i = 0; i < 3; i++) {
    HAL_ADC_PollForConversion(&hadc1, 10);
    v[i] = (uint16_t)HAL_ADC_GetValue(&hadc1);
  }
  HAL_ADC_Stop(&hadc1);
}

/* CV-in transfer function (cv-input.md): inverting, V_in = (1.663 - Vadc)/0.330. */
static float cv_in_volts(uint16_t code)
{
  float vadc = (float)code * 3.3f / 4095.0f;
  return (1.663f - vadc) / 0.330f;
}

/* Fill one half (DMA_FRAMES/2 frames) of the circular buffer. When the tone is
 * inactive we still write zeros so the clocks keep running and the DAC stays
 * locked/unmuted, just outputting silence. Runs in DMA IRQ context. */
static void tone_fill_half(int half)
{
  int start = half ? (DMA_FRAMES / 2) : 0;
  int end   = start + (DMA_FRAMES / 2);
  for (int i = start; i < end; i++) {
    int32_t s = 0;
    if (tone.active) {
      s = (int32_t)((float)TONE_AMP * sinf(tone.phase));
      tone.phase += tone.inc;
      if (tone.phase >= TWO_PI) tone.phase -= TWO_PI;
    }
    uint16_t hi = (uint16_t)((uint32_t)s >> 16);
    uint16_t lo = (uint16_t)((uint32_t)s & 0xFFFF);
    tone.buf[i * 4 + 0] = hi;  /* L */
    tone.buf[i * 4 + 1] = lo;
    tone.buf[i * 4 + 2] = hi;  /* R */
    tone.buf[i * 4 + 3] = lo;
  }
}

/* Circular Tx DMA callbacks: refill the half the DMA just finished playing
 * (HalfCplt = first half, Cplt = second half). The other half is being read. */
void HAL_I2S_TxHalfCpltCallback(I2S_HandleTypeDef *hi2s)
{
  if (hi2s->Instance == SPI3) tone_fill_half(0);
}
void HAL_I2S_TxCpltCallback(I2S_HandleTypeDef *hi2s)
{
  if (hi2s->Instance == SPI3) tone_fill_half(1);
}

/* Start the never-ending circular stream (called once from console_init).
 * Size = count of 32-bit samples (L+R) = DMA_FRAMES*2; for 32-bit data format
 * the HAL doubles it internally to DMA_FRAMES*4 halfwords = the whole buffer. */
static void tone_stream_start(void)
{
  tone_fill_half(0);
  tone_fill_half(1);
  HAL_I2S_Transmit_DMA(&hi2s3, tone.buf, DMA_FRAMES * 2);
}

/* ---- OLED status display ----
 * The screen mirrors what the bench is doing: a title bar names the IO under
 * test (e.g. "H3 CV-OUT A") and up to 4 body lines carry the live values.
 * Commands set the text; the full-frame blit is rate-limited and only runs
 * when something changed, so it never stalls the I2S/SPI tests. */
static UBYTE oled_image[128 * 128 / 2];

#define DISP_BODY_ROWS 4
#define DISP_COLS      20            /* ~21 Font12 glyphs across 128 px */

static struct {
  char     title[DISP_COLS];
  char     body[DISP_BODY_ROWS][DISP_COLS];
  uint8_t  dirty;
  uint32_t t_last;
} disp;

static void oled_setup(void)
{
  System_Init();
  OLED_1in5_Init();
  OLED_1in5_Clear();
  Paint_NewImage(oled_image, 128, 128, 0, BLACK);
  Paint_SetScale(16);
  Paint_SelectImage(oled_image);
}

/* Set the title bar and clear all body lines (each command starts fresh). */
static void disp_title(const char *fmt, ...)
{
  va_list ap; va_start(ap, fmt);
  vsnprintf(disp.title, sizeof(disp.title), fmt, ap);
  va_end(ap);
  for (int i = 0; i < DISP_BODY_ROWS; i++) disp.body[i][0] = '\0';
  disp.dirty = 1;
}

static void disp_line(int row, const char *fmt, ...)
{
  if (row < 0 || row >= DISP_BODY_ROWS) return;
  va_list ap; va_start(ap, fmt);
  vsnprintf(disp.body[row], sizeof(disp.body[row]), fmt, ap);
  va_end(ap);
  disp.dirty = 1;
}

static void disp_render(void)
{
  Paint_Clear(BLACK);
  Paint_DrawRectangle(0, 0, 127, 15, WHITE, DOT_PIXEL_1X1, DRAW_FILL_FULL);
  Paint_DrawString_EN(2, 2, disp.title, &Font12, WHITE, BLACK); /* black on bar */
  for (int i = 0; i < DISP_BODY_ROWS; i++) {
    if (disp.body[i][0]) {
      Paint_DrawString_EN(2, 22 + i * 16, disp.body[i], &Font12, BLACK, WHITE);
    }
  }
  OLED_1in5_Display(oled_image);
}

/* Redraw at most ~8 Hz, only when a command/job changed the contents. */
static void disp_service(uint32_t now)
{
  if (disp.dirty && (now - disp.t_last) >= 120) {
    disp.dirty = 0;
    disp.t_last = now;
    disp_render();
  }
}

static void oled_test_pattern(void)
{
  Paint_Clear(BLACK);
  Paint_DrawRectangle(0, 0, 127, 127, WHITE, DOT_PIXEL_1X1, DRAW_FILL_EMPTY);
  Paint_DrawLine(0, 0, 127, 127, WHITE, DOT_PIXEL_1X1, LINE_STYLE_SOLID);
  Paint_DrawLine(127, 0, 0, 127, WHITE, DOT_PIXEL_1X1, LINE_STYLE_SOLID);
  Paint_DrawString_EN(18, 56, "TACHYON TEST", &Font12, BLACK, WHITE);
  OLED_1in5_Display(oled_image);
  disp.t_last = HAL_GetTick();   /* don't let a status redraw stomp it instantly */
}

/* ---- clock input capture (PA2 / TIM2_CH3), polled, no IRQ ---- */
static int wait_capture(uint32_t timeout_ms, uint32_t *cap)
{
  uint32_t start = HAL_GetTick();
  while (!__HAL_TIM_GET_FLAG(&htim2, TIM_FLAG_CC3)) {
    if ((HAL_GetTick() - start) > timeout_ms) return 0;
  }
  *cap = HAL_TIM_ReadCapturedValue(&htim2, TIM_CHANNEL_3); /* clears flag */
  return 1;
}

static void cmd_clk(void)
{
  uint32_t t1, t2;
  __HAL_TIM_CLEAR_FLAG(&htim2, TIM_FLAG_CC3);
  disp_title("H9 CLK-IN");
  disp_line(0, "measuring...");
  if (!wait_capture(2000, &t1) || !wait_capture(2000, &t2)) {
    printf("clk: no edges (timeout) — patch a clock into CLK-IN\r\n");
    disp_line(0, "no edges");
    disp_line(1, "patch a clock");
    return;
  }
  uint32_t ticks = t2 - t1;                 /* 32-bit timer, wrap-safe */
  float f_timer = 84000000.0f;              /* TIM2 = APB1x2 = 84 MHz */
  float period_us = (float)ticks / f_timer * 1e6f;
  float freq = f_timer / (float)ticks;
  float bpm1 = freq * 60.0f;                /* BPM at 1 PPQN */
  printf("clk: period=%.1f us  freq=%.3f Hz  bpm@1ppqn=%.2f\r\n",
         period_us, freq, bpm1);
  disp_line(0, "f = %.2f Hz", freq);
  disp_line(1, "T = %.1f us", period_us);
  disp_line(2, "bpm@1 = %.1f", bpm1);
}

/* ---- command dispatch ---- */
static int streq(const char *a, const char *b) { return strcmp(a, b) == 0; }

static void cmd_help(void)
{
  printf(
    "commands:\r\n"
    "  id                      firmware id\r\n"
    "  led on|off|blink        PB2 user LED\r\n"
    "  dac a|b <code>          raw 16-bit DAC (0-65535)\r\n"
    "  dac a|b mv <millivolt>  CV out 0-10000 mV\r\n"
    "  gate a|b 0|1            static jack level\r\n"
    "  gate a|b pulse <ms>     one-shot pulse\r\n"
    "  gate a|b clk <bpm> <ppqn>  free-run clock source\r\n"
    "  adc a|b                 read CV-IN code + volts\r\n"
    "  pot                     read USR_POT_1\r\n"
    "  stream on|off           continuous ADC dump\r\n"
    "  clk                     measure CLK-IN period/BPM\r\n"
    "  enc                     encoder count\r\n"
    "  tone <hz>               play sine (0 = stop)\r\n"
    "  mute 0|1                XSMT: 0=unmute 1=mute\r\n"
    "  oled test               draw OLED test pattern\r\n"
    "  sd                      SD card-detect + block 0 read\r\n");
}

static void dispatch(char *line)
{
  char *argv[6];
  int argc = 0;
  char *tok = strtok(line, " \t");
  while (tok && argc < 6) { argv[argc++] = tok; tok = strtok(NULL, " \t"); }
  if (argc == 0) return;

  if (streq(argv[0], "help") || streq(argv[0], "?")) {
    cmd_help();
  } else if (streq(argv[0], "id")) {
    printf("tachyon-test " __DATE__ " " __TIME__ "\r\n");
  } else if (streq(argv[0], "led") && argc >= 2) {
    if (streq(argv[1], "on"))  { led_blink.active = 0; HAL_GPIO_WritePin(GPIOB, GPIO_PIN_2, GPIO_PIN_SET); }
    else if (streq(argv[1], "off")) { led_blink.active = 0; HAL_GPIO_WritePin(GPIOB, GPIO_PIN_2, GPIO_PIN_RESET); }
    else if (streq(argv[1], "blink")) { led_blink.active = 1; led_blink.t_off = HAL_GetTick() + 200; }
    printf("led %s\r\n", argv[1]);
    disp_title("USER LED");
    disp_line(0, "PB2 = %s", argv[1]);
  } else if (streq(argv[0], "dac") && argc >= 3) {
    int jack = streq(argv[1], "b") ? 1 : 0;
    uint16_t code;
    if (streq(argv[2], "mv") && argc >= 4) {
      long mv = strtol(argv[3], NULL, 0);
      if (mv < 0) mv = 0;
      if (mv > 10000) mv = 10000;
      code = (uint16_t)((mv * 65535L) / 10000L);
    } else {
      long c = strtol(argv[2], NULL, 0);
      if (c < 0) c = 0;
      if (c > 65535) c = 65535;
      code = (uint16_t)c;
    }
    dac_write(jack, code);
    printf("dac %s code=%u\r\n", jack ? "b" : "a", code);
    disp_title("H3 CV-OUT %s", jack ? "B" : "A");
    disp_line(0, "code = %u", code);
    disp_line(1, "= %.2f V", (float)code * 10.0f / 65535.0f);
  } else if (streq(argv[0], "gate") && argc >= 3) {
    int ch = streq(argv[1], "b") ? 1 : 0;
    disp_title("H9 GATE-OUT %s", ch ? "B" : "A");
    if (streq(argv[2], "pulse") && argc >= 4) {
      gate_set(ch, 1);
      gate_job[ch].mode = 1;
      gate_job[ch].t_next = HAL_GetTick() + (uint32_t)strtol(argv[3], NULL, 0);
      printf("gate %s pulse\r\n", argv[1]);
      disp_line(0, "pulse %ld ms", strtol(argv[3], NULL, 0));
    } else if (streq(argv[2], "clk") && argc >= 5) {
      long bpm = strtol(argv[3], NULL, 0);
      long ppqn = strtol(argv[4], NULL, 0);
      if (bpm < 1) bpm = 1;
      if (ppqn < 1) ppqn = 1;
      uint32_t period_ms = (uint32_t)(60000L / (bpm * ppqn));
      gate_job[ch].half_ms = period_ms / 2 ? period_ms / 2 : 1;
      gate_job[ch].mode = 2;
      gate_job[ch].t_next = HAL_GetTick() + gate_job[ch].half_ms;
      gate_set(ch, 1);
      printf("gate %s clk %ld bpm %ld ppqn (half=%lu ms)\r\n",
             argv[1], bpm, ppqn, (unsigned long)gate_job[ch].half_ms);
      disp_line(0, "clk %ld bpm", bpm);
      disp_line(1, "%ld ppqn", ppqn);
    } else {
      gate_job[ch].mode = 0;
      gate_set(ch, strtol(argv[2], NULL, 0) ? 1 : 0);
      printf("gate %s %d\r\n", argv[1], gate_job[ch].level);
      disp_line(0, "jack = %s", gate_job[ch].level ? "HIGH" : "LOW");
    }
  } else if (streq(argv[0], "adc") && argc >= 2) {
    uint16_t v[3]; adc_read_all(v);
    int idx = streq(argv[1], "b") ? 1 : 0;
    printf("adc %s code=%u volts=%.3f\r\n", idx ? "b" : "a", v[idx], cv_in_volts(v[idx]));
    disp_title("H9 CV-IN %s", idx ? "B" : "A");
    disp_line(0, "code = %u", v[idx]);
    disp_line(1, "= %.3f V", cv_in_volts(v[idx]));
  } else if (streq(argv[0], "pot")) {
    uint16_t v[3]; adc_read_all(v);
    printf("pot code=%u\r\n", v[2]);
    disp_title("H9 USR-POT");
    disp_line(0, "code = %u", v[2]);
    disp_line(1, "= %u %%", (unsigned)(v[2] * 100u / 4095u));
  } else if (streq(argv[0], "stream") && argc >= 2) {
    adc_stream = streq(argv[1], "on");
    adc_stream_t = HAL_GetTick();
    printf("stream %s\r\n", adc_stream ? "on" : "off");
    if (adc_stream) disp_title("H9 CV-IN/POT");
    else            disp_title("stream off");
  } else if (streq(argv[0], "clk")) {
    cmd_clk();
  } else if (streq(argv[0], "enc")) {
    printf("enc count=%ld\r\n", (long)enc_count);
    disp_title("H4 USR-ENC");
    disp_line(0, "count = %ld", (long)enc_count);
  } else if (streq(argv[0], "tone") && argc >= 2) {
    long hz = strtol(argv[1], NULL, 0);
    if (hz <= 0) { tone.active = 0; printf("tone off\r\n"); disp_title("H2 A-OUT"); disp_line(0, "tone off"); }
    else { tone.inc = TWO_PI * (float)hz / I2S_FS; tone.active = 1; printf("tone %ld Hz\r\n", hz);
           disp_title("H2 A-OUT L/R"); disp_line(0, "tone %ld Hz", hz); }
  } else if (streq(argv[0], "mute") && argc >= 2) {
    int muted = strtol(argv[1], NULL, 0) ? 1 : 0;
    HAL_GPIO_WritePin(MUTE_N_GPIO_Port, MUTE_N_Pin, muted ? GPIO_PIN_RESET : GPIO_PIN_SET);
    printf("mute %d (%s)\r\n", muted, muted ? "muted" : "unmuted");
    disp_title("DAC MUTE");
    disp_line(0, "%s", muted ? "MUTED" : "UNMUTED");
  } else if (streq(argv[0], "oled") && argc >= 2 && streq(argv[1], "test")) {
    oled_test_pattern();
    printf("oled test pattern drawn\r\n");
  } else if (streq(argv[0], "sd")) {
    int cd = (HAL_GPIO_ReadPin(SD_CD_GPIO_Port, SD_CD_Pin) == GPIO_PIN_SET);
    printf("sd card-detect=%s\r\n", cd ? "present" : "empty");
    disp_title("SD CARD");
    disp_line(0, "detect = %s", cd ? "present" : "empty");
    if (cd) {
      uint8_t blk[512];
      if (HAL_SD_ReadBlocks(&hsd, blk, 0, 1, 200) == HAL_OK) {
        printf("sd block0: %02X %02X %02X %02X ... (state=%lu)\r\n",
               blk[0], blk[1], blk[2], blk[3], (unsigned long)HAL_SD_GetCardState(&hsd));
        disp_line(1, "blk0 %02X %02X %02X", blk[0], blk[1], blk[2]);
      } else {
        printf("sd read failed — if card was inserted after boot, reset the board\r\n");
        disp_line(1, "read failed");
      }
    }
  } else {
    printf("unknown: %s (try 'help')\r\n", argv[0]);
  }
}

/* ---- public API ---- */

void console_init(void)
{
  /* Gate outputs as plain push-pull GPIO (the app build uses the timer AF;
   * for bring-up we drive them directly). Power-up = jack LOW (GPIO HIGH). */
  GPIO_InitTypeDef g = {0};
  g.Mode = GPIO_MODE_OUTPUT_PP;
  g.Pull = GPIO_NOPULL;
  g.Speed = GPIO_SPEED_FREQ_LOW;
  g.Pin = GATE_OUT_A_Pin; HAL_GPIO_Init(GATE_OUT_A_GPIO_Port, &g);
  g.Pin = GATE_OUT_B_Pin; HAL_GPIO_Init(GATE_OUT_B_GPIO_Port, &g);
  gate_set(0, 0);
  gate_set(1, 0);

  /* CLK-IN capture (polled in cmd_clk). */
  HAL_TIM_IC_Start(&htim2, TIM_CHANNEL_3);

  encoder_init();
  oled_setup();

  /* DAC and outputs to a safe zero state. */
  dac_write(0, 0);
  dac_write(1, 0);
  HAL_GPIO_WritePin(MUTE_N_GPIO_Port, MUTE_N_Pin, GPIO_PIN_RESET); /* start muted */

  /* Start the continuous I2S stream now (silence until `tone` is issued). The
   * PCM5102A needs uninterrupted BCK/LRCK to stay out of soft-mute. */
  tone_stream_start();

  tick_1ms_prev = HAL_GetTick();

  disp_title("TACHYON TEST");
  disp_line(0, "console ready");
  disp_line(1, "USB-CDC up");
  disp_render();
  disp.t_last = HAL_GetTick();

  printf("\r\n=== Tachyon IO test console ===\r\n");
  cmd_help();
  printf("> ");
}

static void service_jobs(void)
{
  uint32_t now = HAL_GetTick();

  /* 1 ms encoder timing + count accumulation */
  if (now != tick_1ms_prev) {
    while (tick_1ms_prev != now) { encoder_tick_1ms(); tick_1ms_prev++; }
    int32_t d = encoder_get_delta();
    if (d) { enc_count += d; disp_title("H4 USR-ENC"); disp_line(0, "count = %ld", (long)enc_count); }
    encoder_btn_event_t ev = encoder_get_button_event();
    if (ev == ENC_BTN_SHORT_PRESS) { printf("enc: SHORT press\r\n"); disp_line(1, "SW: SHORT"); }
    else if (ev == ENC_BTN_LONG_PRESS) { printf("enc: LONG press\r\n"); disp_line(1, "SW: LONG"); }
  }

  /* gate background jobs */
  for (int ch = 0; ch < 2; ch++) {
    if (gate_job[ch].mode == 1 && (int32_t)(now - gate_job[ch].t_next) >= 0) {
      gate_set(ch, 0);
      gate_job[ch].mode = 0;
    } else if (gate_job[ch].mode == 2 && (int32_t)(now - gate_job[ch].t_next) >= 0) {
      gate_set(ch, gate_job[ch].level ? 0 : 1);
      gate_job[ch].t_next = now + gate_job[ch].half_ms;
      disp_line(2, "jack = %s", gate_job[ch].level ? "HIGH" : "LOW");
    }
  }

  /* LED blink */
  if (led_blink.active && (int32_t)(now - led_blink.t_off) >= 0) {
    HAL_GPIO_TogglePin(GPIOB, GPIO_PIN_2);
    led_blink.t_off = now + 200;
  }

  /* ADC stream ~10 Hz */
  if (adc_stream && (now - adc_stream_t) >= 100) {
    adc_stream_t = now;
    uint16_t v[3]; adc_read_all(v);
    printf("stream a=%u(%.3fV) b=%u(%.3fV) pot=%u\r\n",
           v[0], cv_in_volts(v[0]), v[1], cv_in_volts(v[1]), v[2]);
    disp_line(0, "A %.2fV B %.2fV", cv_in_volts(v[0]), cv_in_volts(v[1]));
    disp_line(1, "pot = %u", v[2]);
  }

  /* tone playback runs entirely off the circular I2S DMA (started in
   * console_init); the half/complete callbacks refill the buffer, so there is
   * nothing to service here. */

  disp_service(now);
}

void console_poll(void)
{
  static char line[128];
  static uint16_t idx;

  while (rx_tail != rx_head) {
    uint8_t c = rx_buf[rx_tail];
    rx_tail = (uint16_t)((rx_tail + 1u) % RX_SZ);

    if (c == '\r' || c == '\n') {
      if (idx > 0) {
        line[idx] = '\0';
        dispatch(line);
        idx = 0;
        printf("> ");
      }
    } else if (idx < sizeof(line) - 1) {
      line[idx++] = (char)c;
    }
  }

  service_jobs();
}
