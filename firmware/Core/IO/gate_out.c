#include "gate_out.h"
#include "main.h"

static struct {
  uint8_t  mode;     /* 0 = static, 1 = one-shot pulse, 2 = free-run clock */
  uint8_t  level;    /* current jack level (1 = high) */
  uint32_t t_next;   /* HAL tick of next edge */
  uint32_t half_ms;  /* half period for clock mode */
} s_job[2];

static GPIO_TypeDef *gate_port(int ch) { return ch ? GATE_OUT_B_GPIO_Port : GATE_OUT_A_GPIO_Port; }
static uint16_t      gate_pin (int ch) { return ch ? GATE_OUT_B_Pin       : GATE_OUT_A_Pin; }

void gate_out_set(uint8_t ch, bool jack_high)
{
  /* IO-board MOSFET inverts (gate-output.md): jack HIGH = GPIO LOW. */
  HAL_GPIO_WritePin(gate_port(ch), gate_pin(ch),
                    jack_high ? GPIO_PIN_RESET : GPIO_PIN_SET);
  s_job[ch].level = jack_high ? 1u : 0u;
}

void gate_out_pulse(uint8_t ch, uint32_t ms)
{
  gate_out_set(ch, true);
  s_job[ch].mode = 1;
  s_job[ch].t_next = HAL_GetTick() + ms;
}

void gate_out_clock(uint8_t ch, uint16_t bpm)
{
  if (bpm < 1) bpm = 1;
  uint32_t period_ms = 60000UL / bpm;
  s_job[ch].half_ms = period_ms / 2 ? period_ms / 2 : 1;
  s_job[ch].mode = 2;
  s_job[ch].t_next = HAL_GetTick() + s_job[ch].half_ms;
  gate_out_set(ch, true);
}

void gate_out_service(uint32_t now_ms)
{
  for (int ch = 0; ch < 2; ch++) {
    if (s_job[ch].mode == 1 && (int32_t)(now_ms - s_job[ch].t_next) >= 0) {
      gate_out_set((uint8_t)ch, false);
      s_job[ch].mode = 0;
    } else if (s_job[ch].mode == 2 && (int32_t)(now_ms - s_job[ch].t_next) >= 0) {
      gate_out_set((uint8_t)ch, s_job[ch].level ? false : true);
      s_job[ch].t_next = now_ms + s_job[ch].half_ms;
    }
  }
}

void gate_out_init(void)
{
  /* Plain push-pull GPIO (the MX_TIM2/3 timer AF on these pins is unused in
   * this scheme). Power-up = jack LOW. */
  GPIO_InitTypeDef g = {0};
  g.Mode  = GPIO_MODE_OUTPUT_PP;
  g.Pull  = GPIO_NOPULL;
  g.Speed = GPIO_SPEED_FREQ_LOW;
  g.Pin = GATE_OUT_A_Pin; HAL_GPIO_Init(GATE_OUT_A_GPIO_Port, &g);
  g.Pin = GATE_OUT_B_Pin; HAL_GPIO_Init(GATE_OUT_B_GPIO_Port, &g);

  s_job[0].mode = 0;
  s_job[1].mode = 0;
  gate_out_set(GATE_A, false);
  gate_out_set(GATE_B, false);
}
