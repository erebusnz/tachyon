/* Tachyon IO bring-up test firmware.
 *
 * Two interfaces share the same low-level IO helpers:
 *   1. On-device MENU UI (OLED + encoder + pot) — the primary control surface.
 *      The encoder navigates a list of IO functions; a short-press enters an
 *      item, a long-press returns to the menu; the pot sets the selected
 *      item's live parameter (CV-out voltage, tone frequency, gate clock BPM).
 *   2. USB-CDC command console — retained in parallel for scripted/automated
 *      bench testing (bench/serial_cli.py). See test-firmware.md for the verbs.
 *
 * The menu owns the OLED; serial commands still drive IO and log to USB. */

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
#define F_TIM2      84000000.0f /* TIM2 = APB1x2 = 84 MHz (clock-in capture) */
#define CLK_PPQN    4            /* clock-in pulses/quarter-note: 4 = Doepfer/
                                  * Eurorack analog "step clock" default. BPM =
                                  * freq*60/PPQN (clock-input.md). */

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

/* DAC8552 24-bit frame control byte (ref: github.com/adn05/dac8552):
 *   UPDATE_DAC_A=0x10 (DB20, load A), UPDATE_DAC_B=0x20 (DB21, load B),
 *   BUFFER_A=0x00 / BUFFER_B=0x04 (DB18, data-buffer select), MODE_NORMAL=0x00.
 * "Write buffer + update output" = UPDATE_DAC_x | BUFFER_x:
 *   ch A = 0x10 | 0x00 = 0x10,  ch B = 0x20 | 0x04 = 0x24.
 * The channel is selected by the BUFFER bit (DB18), NOT DB21.
 * (Bench-confirmed 2026-06-10: the old code set DB21 as if it were the channel
 *  bit but left BUFFER at A, so every write landed in DAC A / VOUTA and VOUTB
 *  was never driven -- looked like a dead DAC channel but was this bug.)
 * Physical CV-OUT-A is wired to DAC channel B and vice-versa
 * (cv-output-dac.md channel swap), so jack 0 (A) -> DAC ch B -> 0x24. */
static void dac_write(int jack, uint16_t code)
{
  uint8_t ctrl  = (jack == 0) ? 0x24u : 0x10u; /* jack A->ch B, jack B->ch A */
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

/* Set the tone frequency (Hz) by deriving the per-sample phase increment. */
static void tone_set_hz(uint16_t hz)
{
  tone.inc = TWO_PI * (float)hz / I2S_FS;
}

/* ---- OLED framebuffer (owned by the menu UI; see menu section below) ---- */
static UBYTE oled_image[128 * 128 / 2];

static void oled_setup(void)
{
  System_Init();
  OLED_1in5_Init();
  OLED_1in5_Clear();
  Paint_NewImage(oled_image, 128, 128, 0, BLACK);
  Paint_SetScale(16);
  Paint_SelectImage(oled_image);
}

static void oled_test_pattern(void)
{
  Paint_Clear(BLACK);
  Paint_DrawRectangle(0, 0, 127, 127, WHITE, DOT_PIXEL_1X1, DRAW_FILL_EMPTY);
  Paint_DrawLine(0, 0, 127, 127, WHITE, DOT_PIXEL_1X1, LINE_STYLE_SOLID);
  Paint_DrawLine(127, 0, 0, 127, WHITE, DOT_PIXEL_1X1, LINE_STYLE_SOLID);
  Paint_DrawString_EN(18, 56, "TACHYON TEST", &Font12, BLACK, WHITE);
  OLED_1in5_Display(oled_image);
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

/* Blocking one-shot measurement used by the serial `clk` command. */
static void cmd_clk(void)
{
  uint32_t t1, t2;
  __HAL_TIM_CLEAR_FLAG(&htim2, TIM_FLAG_CC3);
  if (!wait_capture(2000, &t1) || !wait_capture(2000, &t2)) {
    printf("clk: no edges (timeout) — patch a clock into CLK-IN\r\n");
    return;
  }
  uint32_t ticks = t2 - t1;                 /* 32-bit timer, wrap-safe */
  float period_us = (float)ticks / F_TIM2 * 1e6f;
  float freq = F_TIM2 / (float)ticks;
  printf("clk: period=%.1f us  freq=%.3f Hz  bpm@%dppqn=%.2f\r\n",
         period_us, freq, CLK_PPQN, freq * 60.0f / (float)CLK_PPQN);
}

/* ===================== USB-CDC command console ===================== */

static int streq(const char *a, const char *b) { return strcmp(a, b) == 0; }

static void cmd_help(void)
{
  printf(
    "commands (encoder+pot menu also active on the OLED):\r\n"
    "  id                      firmware id\r\n"
    "  led on|off|blink        PB2 user LED\r\n"
    "  dac a|b <code>          raw 16-bit DAC (0-65535)\r\n"
    "  dac a|b mv <millivolt>  CV out 0-10000 mV\r\n"
    "  gate a|b 0|1            static jack level\r\n"
    "  gate a|b pulse <ms>     one-shot pulse\r\n"
    "  gate a|b clk <bpm> <ppqn>  free-run clock source\r\n"
    "  adc a|b                 read CV-IN code + volts\r\n"
    "  stream on|off           continuous ADC dump\r\n"
    "  clk                     measure CLK-IN period/BPM\r\n"
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
  } else if (streq(argv[0], "gate") && argc >= 3) {
    int ch = streq(argv[1], "b") ? 1 : 0;
    if (streq(argv[2], "pulse") && argc >= 4) {
      gate_set(ch, 1);
      gate_job[ch].mode = 1;
      gate_job[ch].t_next = HAL_GetTick() + (uint32_t)strtol(argv[3], NULL, 0);
      printf("gate %s pulse\r\n", argv[1]);
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
    } else {
      gate_job[ch].mode = 0;
      gate_set(ch, strtol(argv[2], NULL, 0) ? 1 : 0);
      printf("gate %s %d\r\n", argv[1], gate_job[ch].level);
    }
  } else if (streq(argv[0], "adc") && argc >= 2) {
    uint16_t v[3]; adc_read_all(v);
    int idx = streq(argv[1], "b") ? 1 : 0;
    printf("adc %s code=%u volts=%.3f\r\n", idx ? "b" : "a", v[idx], cv_in_volts(v[idx]));
  } else if (streq(argv[0], "stream") && argc >= 2) {
    adc_stream = streq(argv[1], "on");
    adc_stream_t = HAL_GetTick();
    printf("stream %s\r\n", adc_stream ? "on" : "off");
  } else if (streq(argv[0], "clk")) {
    cmd_clk();
  } else if (streq(argv[0], "tone") && argc >= 2) {
    long hz = strtol(argv[1], NULL, 0);
    if (hz <= 0) { tone.active = 0; printf("tone off\r\n"); }
    else { tone_set_hz((uint16_t)hz); tone.active = 1; printf("tone %ld Hz\r\n", hz); }
  } else if (streq(argv[0], "mute") && argc >= 2) {
    int muted = strtol(argv[1], NULL, 0) ? 1 : 0;
    HAL_GPIO_WritePin(MUTE_N_GPIO_Port, MUTE_N_Pin, muted ? GPIO_PIN_RESET : GPIO_PIN_SET);
    printf("mute %d (%s)\r\n", muted, muted ? "muted" : "unmuted");
  } else if (streq(argv[0], "oled") && argc >= 2 && streq(argv[1], "test")) {
    oled_test_pattern();
    printf("oled test pattern drawn (menu redraws on next nav)\r\n");
  } else if (streq(argv[0], "sd")) {
    int cd = (HAL_GPIO_ReadPin(SD_CD_GPIO_Port, SD_CD_Pin) == GPIO_PIN_SET);
    printf("sd card-detect=%s\r\n", cd ? "present" : "empty");
    if (cd) {
      uint8_t blk[512];
      if (HAL_SD_ReadBlocks(&hsd, blk, 0, 1, 200) == HAL_OK) {
        printf("sd block0: %02X %02X %02X %02X ... (state=%lu)\r\n",
               blk[0], blk[1], blk[2], blk[3], (unsigned long)HAL_SD_GetCardState(&hsd));
      } else {
        printf("sd read failed — if card was inserted after boot, reset the board\r\n");
      }
    }
  } else {
    printf("unknown: %s (try 'help')\r\n", argv[0]);
  }
}

/* ===================== On-device menu UI ===================== */
/* Encoder navigates; short-press enters an item / does its action; long-press
 * returns to the menu. The pot sets the selected item's live parameter. The
 * menu is the sole OLED renderer — serial commands drive the same hardware but
 * the menu shows its own last-commanded value (no DAC readback). */

/* Non-blocking CLK-IN monitor: polled every loop while the Clock In item is
 * open. Shares TIM2_CH3 with the serial `clk` command, but only one is in use
 * at a time in practice (human at the OLED vs. host script). */
static struct {
  uint32_t last_cap;
  uint8_t  have_last;
  uint32_t period_ticks;   /* last good inter-edge period, 0 = no clock */
  uint32_t t_last_edge;    /* HAL tick of last edge, for staleness timeout */
} clkmon;

static void clk_monitor_poll(uint32_t now)
{
  if (__HAL_TIM_GET_FLAG(&htim2, TIM_FLAG_CC3)) {
    uint32_t cap = HAL_TIM_ReadCapturedValue(&htim2, TIM_CHANNEL_3); /* clears flag */
    if (clkmon.have_last) {
      uint32_t d = cap - clkmon.last_cap;
      if (d) clkmon.period_ticks = d;
    }
    clkmon.last_cap = cap;
    clkmon.have_last = 1;
    clkmon.t_last_edge = now;
  } else if (clkmon.period_ticks && (now - clkmon.t_last_edge) > 1500) {
    clkmon.period_ticks = 0;   /* stale: declare no clock */
    clkmon.have_last = 0;
  }
}

typedef enum {
  IT_GATE_A, IT_GATE_B, IT_CVOUT_A, IT_CVOUT_B,
  IT_AUDIO, IT_ANALOG_IN, IT_CLOCK_IN, IT_COUNT
} menu_item_t;

typedef enum { UI_MENU, UI_ITEM } ui_state_t;

static const char *const menu_names[IT_COUNT] = {
  "Gate A", "Gate B", "CV-out A", "CV-out B", "Audio", "Analog In", "Clock In",
};

static struct {
  ui_state_t  state;
  int         cursor;        /* highlighted row in the menu */
  menu_item_t item;          /* active item in UI_ITEM */
  uint8_t     redraw;        /* pending render request */
  uint32_t    t_render;      /* last render tick */
  uint16_t    pot_raw;       /* last pot ADC code */
  uint16_t    cv_in[2];      /* last CV-IN A/B ADC codes (Analog In screen) */
  uint16_t    cv_mv[2];      /* commanded CV-out A/B (mV) */
  uint8_t     gate_disp[2];  /* 0 = LOW, 1 = HIGH, 2 = CLK */
  uint16_t    gate_bpm[2];   /* clock rate in CLK state */
  uint16_t    tone_hz;       /* commanded tone frequency */
  uint8_t     muted;         /* XSMT state shown on the Audio screen */
} ui;

/* Drive the gate hardware from the item's display state. */
static void apply_gate_state(int ch)
{
  if (ui.gate_disp[ch] == 0) {        /* LOW */
    gate_job[ch].mode = 0;
    gate_set(ch, 0);
  } else if (ui.gate_disp[ch] == 1) { /* HIGH */
    gate_job[ch].mode = 0;
    gate_set(ch, 1);
  } else {                            /* CLK */
    uint16_t bpm = ui.gate_bpm[ch] ? ui.gate_bpm[ch] : 120;
    uint32_t period_ms = 60000UL / bpm;
    gate_job[ch].half_ms = period_ms / 2 ? period_ms / 2 : 1;
    gate_job[ch].mode = 2;
    gate_job[ch].t_next = HAL_GetTick() + gate_job[ch].half_ms;
    gate_set(ch, 1);
  }
}

static void menu_enter_item(void)
{
  switch (ui.item) {
    case IT_AUDIO:
      tone_set_hz(ui.tone_hz);
      tone.active = 1;
      break;
    case IT_CLOCK_IN:
      clkmon.have_last = 0;
      clkmon.period_ticks = 0;
      clkmon.t_last_edge = HAL_GetTick();
      __HAL_TIM_CLEAR_FLAG(&htim2, TIM_FLAG_CC3);
      break;
    default:
      break;
  }
}

static void menu_exit_item(void)
{
  if (ui.item == IT_AUDIO) tone.active = 0;   /* silence on leaving Audio */
}

/* Short-press action inside an item. */
static void menu_item_action(void)
{
  switch (ui.item) {
    case IT_GATE_A:
    case IT_GATE_B: {
      int ch = (ui.item == IT_GATE_B) ? 1 : 0;
      ui.gate_disp[ch] = (uint8_t)((ui.gate_disp[ch] + 1) % 3); /* LOW->HIGH->CLK */
      apply_gate_state(ch);
      break;
    }
    case IT_AUDIO:
      ui.muted ^= 1u;
      HAL_GPIO_WritePin(MUTE_N_GPIO_Port, MUTE_N_Pin,
                        ui.muted ? GPIO_PIN_RESET : GPIO_PIN_SET);
      break;
    default:
      break;   /* CV-out / monitors: nothing on short-press */
  }
}

/* Map the pot to the active item's parameter (linear, with a small deadband so
 * pot noise doesn't jitter the output/display). Also refreshes the cached
 * CV-IN / pot codes used by the Analog In screen. */
static void menu_apply_pot(void)
{
  uint16_t v[3]; adc_read_all(v);
  ui.cv_in[0] = v[0];
  ui.cv_in[1] = v[1];
  ui.pot_raw  = v[2];
  uint32_t pot = v[2];

  switch (ui.item) {
    case IT_CVOUT_A:
    case IT_CVOUT_B: {
      int ch = (ui.item == IT_CVOUT_B) ? 1 : 0;
      uint16_t mv = (uint16_t)(pot * 10000UL / 4095UL);
      if (abs((int)mv - (int)ui.cv_mv[ch]) >= 25) {        /* ~10 ADC codes */
        ui.cv_mv[ch] = mv;
        dac_write(ch, (uint16_t)((uint32_t)mv * 65535UL / 10000UL));
        ui.redraw = 1;
      }
      break;
    }
    case IT_AUDIO: {
      uint16_t hz = (uint16_t)(20UL + pot * (5000UL - 20UL) / 4095UL);
      if (abs((int)hz - (int)ui.tone_hz) >= 5) {
        ui.tone_hz = hz;
        tone_set_hz(hz);
        ui.redraw = 1;
      }
      break;
    }
    case IT_GATE_A:
    case IT_GATE_B: {
      int ch = (ui.item == IT_GATE_B) ? 1 : 0;
      if (ui.gate_disp[ch] == 2) {                          /* only in CLK */
        uint16_t bpm = (uint16_t)(30UL + pot * (960UL - 30UL) / 4095UL);
        if (bpm != ui.gate_bpm[ch]) {
          ui.gate_bpm[ch] = bpm;
          uint32_t period_ms = 60000UL / (bpm ? bpm : 1);
          gate_job[ch].half_ms = period_ms / 2 ? period_ms / 2 : 1;
          ui.redraw = 1;
        }
      }
      break;
    }
    default:
      break;   /* monitors: pot unused */
  }
}

/* ---- rendering ---- */

/* One body text line, white-on-black, Font12, rows 0..5 (y = 20 + row*15). */
static void item_line(int row, const char *s)
{
  Paint_DrawString_EN(4, 20 + row * 15, s, &Font12, WHITE, BLACK);
}

static void menu_render_menu(void)
{
  Paint_Clear(BLACK);
  Paint_DrawRectangle(0, 0, 127, 14, WHITE, DOT_PIXEL_1X1, DRAW_FILL_FULL);
  Paint_DrawString_EN(4, 1, "TACHYON IO TEST", &Font12, BLACK, WHITE);

  for (int i = 0; i < IT_COUNT; i++) {
    int y = 18 + i * 15;
    if (i == ui.cursor) {   /* highlight bar: black text on a filled white row */
      Paint_DrawRectangle(0, y - 1, 127, y + 12, WHITE, DOT_PIXEL_1X1, DRAW_FILL_FULL);
      Paint_DrawString_EN(4, y, menu_names[i], &Font12, BLACK, WHITE);
    } else {
      Paint_DrawString_EN(4, y, menu_names[i], &Font12, WHITE, BLACK);
    }
  }
}

static void menu_render_item(void)
{
  char b[24];
  Paint_Clear(BLACK);
  Paint_DrawRectangle(0, 0, 127, 14, WHITE, DOT_PIXEL_1X1, DRAW_FILL_FULL);
  Paint_DrawString_EN(4, 1, menu_names[ui.item], &Font12, BLACK, WHITE);

  switch (ui.item) {
    case IT_GATE_A:
    case IT_GATE_B: {
      int ch = (ui.item == IT_GATE_B) ? 1 : 0;
      const char *st = ui.gate_disp[ch] == 0 ? "LOW" :
                       (ui.gate_disp[ch] == 1 ? "HIGH" : "CLK");
      snprintf(b, sizeof b, "state: %s", st);  item_line(0, b);
      if (ui.gate_disp[ch] == 2) {
        snprintf(b, sizeof b, "rate: %u BPM", ui.gate_bpm[ch]); item_line(1, b);
        item_line(2, "pot sets rate");
      }
      item_line(4, "click: cycle");
      break;
    }
    case IT_CVOUT_A:
    case IT_CVOUT_B: {
      int ch = (ui.item == IT_CVOUT_B) ? 1 : 0;
      snprintf(b, sizeof b, "%u mV", ui.cv_mv[ch]);            item_line(0, b);
      snprintf(b, sizeof b, "= %.2f V", (float)ui.cv_mv[ch] / 1000.0f); item_line(1, b);
      item_line(3, "pot sets level");
      break;
    }
    case IT_AUDIO:
      snprintf(b, sizeof b, "tone: %u Hz", ui.tone_hz);        item_line(0, b);
      snprintf(b, sizeof b, "audio: %s", ui.muted ? "MUTED" : "ON"); item_line(1, b);
      item_line(3, "click = mute");
      break;
    case IT_ANALOG_IN:
      snprintf(b, sizeof b, "CV-A: %.2f V", cv_in_volts(ui.cv_in[0])); item_line(0, b);
      snprintf(b, sizeof b, "CV-B: %.2f V", cv_in_volts(ui.cv_in[1])); item_line(1, b);
      snprintf(b, sizeof b, "pot:  %u", ui.pot_raw);           item_line(2, b);
      break;
    case IT_CLOCK_IN:
      if (clkmon.period_ticks) {
        float freq = F_TIM2 / (float)clkmon.period_ticks;
        snprintf(b, sizeof b, "%.2f Hz", freq);                item_line(0, b);
        snprintf(b, sizeof b, "%.1f BPM", freq * 60.0f / (float)CLK_PPQN); item_line(1, b);
        snprintf(b, sizeof b, "(%d PPQN)", CLK_PPQN);          item_line(2, b);
      } else {
        item_line(0, "no clock");
        item_line(1, "patch H9.8");
      }
      break;
    default:
      break;
  }

  Paint_DrawString_EN(4, 113, "hold = back", &Font12, WHITE, BLACK);
}

static void menu_render(void)
{
  if (ui.state == UI_MENU) menu_render_menu();
  else                     menu_render_item();
  OLED_1in5_Display(oled_image);
}

static void menu_poll(uint32_t now)
{
  int32_t d = encoder_get_delta();
  encoder_btn_event_t ev = encoder_get_button_event();

  if (ui.state == UI_MENU) {
    if (d) {
      int c = ui.cursor + (int)d;
      if (c < 0) c = 0;
      if (c >= IT_COUNT) c = IT_COUNT - 1;
      if (c != ui.cursor) { ui.cursor = c; ui.redraw = 1; }
    }
    if (ev == ENC_BTN_SHORT_PRESS) {
      ui.item  = (menu_item_t)ui.cursor;
      ui.state = UI_ITEM;
      menu_enter_item();
      ui.redraw = 1;
    }
    /* long-press in the menu: ignored */
  } else { /* UI_ITEM */
    if (ev == ENC_BTN_LONG_PRESS) {
      menu_exit_item();
      ui.state  = UI_MENU;
      ui.redraw = 1;
    } else if (ev == ENC_BTN_SHORT_PRESS) {
      menu_item_action();
      ui.redraw = 1;
    }
    if (ui.item == IT_CLOCK_IN) clk_monitor_poll(now);
  }

  /* periodic: apply the pot and repaint (monitors refresh continuously) */
  if ((now - ui.t_render) >= 33) {
    ui.t_render = now;
    if (ui.state == UI_ITEM) {
      menu_apply_pot();
      if (ui.item == IT_ANALOG_IN || ui.item == IT_CLOCK_IN) ui.redraw = 1;
    }
    if (ui.redraw) { ui.redraw = 0; menu_render(); }
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

  /* CLK-IN capture (polled in cmd_clk / clk_monitor_poll). */
  HAL_TIM_IC_Start(&htim2, TIM_CHANNEL_3);

  encoder_init();
  oled_setup();

  /* DAC and outputs to a safe zero state. */
  dac_write(0, 0);
  dac_write(1, 0);
  HAL_GPIO_WritePin(MUTE_N_GPIO_Port, MUTE_N_Pin, GPIO_PIN_RESET); /* start muted */

  /* Start the continuous I2S stream now (silence until a tone is set). The
   * PCM5102A needs uninterrupted BCK/LRCK to stay out of soft-mute. */
  tone_stream_start();

  /* Menu UI defaults. */
  ui.state    = UI_MENU;
  ui.cursor   = 0;
  ui.tone_hz  = 440;
  ui.gate_bpm[0] = ui.gate_bpm[1] = 120;
  ui.muted    = 1;                 /* matches the muted start above */
  ui.t_render = HAL_GetTick();
  menu_render();

  printf("\r\n=== Tachyon IO test (menu UI + serial console) ===\r\n");
  cmd_help();
  printf("> ");
}

static void service_jobs(void)
{
  uint32_t now = HAL_GetTick();

  /* gate background jobs (one-shot pulse + free-run clock) */
  for (int ch = 0; ch < 2; ch++) {
    if (gate_job[ch].mode == 1 && (int32_t)(now - gate_job[ch].t_next) >= 0) {
      gate_set(ch, 0);
      gate_job[ch].mode = 0;
    } else if (gate_job[ch].mode == 2 && (int32_t)(now - gate_job[ch].t_next) >= 0) {
      gate_set(ch, gate_job[ch].level ? 0 : 1);
      gate_job[ch].t_next = now + gate_job[ch].half_ms;
    }
  }

  /* LED blink */
  if (led_blink.active && (int32_t)(now - led_blink.t_off) >= 0) {
    HAL_GPIO_TogglePin(GPIOB, GPIO_PIN_2);
    led_blink.t_off = now + 200;
  }

  /* ADC stream ~10 Hz (serial `stream on`) */
  if (adc_stream && (now - adc_stream_t) >= 100) {
    adc_stream_t = now;
    uint16_t v[3]; adc_read_all(v);
    printf("stream a=%u(%.3fV) b=%u(%.3fV) pot=%u\r\n",
           v[0], cv_in_volts(v[0]), v[1], cv_in_volts(v[1]), v[2]);
  }

  /* tone playback runs entirely off the circular I2S DMA (started in
   * console_init); the half/complete callbacks refill the buffer. */
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
  menu_poll(HAL_GetTick());
}
