#include "clock_in.h"
#include "main.h"

extern TIM_HandleTypeDef htim2;

#define F_TIM2        84000000.0f   /* TIM2 = APB1x2 = 84 MHz */
#define STALE_MS      1500u         /* declare "no clock" after this gap */

static struct {
  uint32_t last_cap;
  uint8_t  have_last;
  uint32_t period_ticks;   /* last good inter-edge period, 0 = no clock */
  uint32_t t_last_edge;    /* HAL tick of last edge, for staleness */
  uint8_t  edge_pending;   /* latched on each new capture, consumed by clock_in_edge */
} s_clk;

void clock_in_init(void)
{
  s_clk.have_last = 0;
  s_clk.period_ticks = 0;
  s_clk.edge_pending = 0;
  s_clk.t_last_edge = HAL_GetTick();
  __HAL_TIM_CLEAR_FLAG(&htim2, TIM_FLAG_CC3);
  HAL_TIM_IC_Start(&htim2, TIM_CHANNEL_3);
}

void clock_in_poll(uint32_t now_ms)
{
  if (__HAL_TIM_GET_FLAG(&htim2, TIM_FLAG_CC3)) {
    uint32_t cap = HAL_TIM_ReadCapturedValue(&htim2, TIM_CHANNEL_3); /* clears flag */
    if (s_clk.have_last) {
      uint32_t d = cap - s_clk.last_cap;   /* 32-bit timer, wrap-safe */
      if (d) s_clk.period_ticks = d;
    }
    s_clk.last_cap = cap;
    s_clk.have_last = 1;
    s_clk.t_last_edge = now_ms;
    s_clk.edge_pending = 1;
  } else if (s_clk.period_ticks && (now_ms - s_clk.t_last_edge) > STALE_MS) {
    s_clk.period_ticks = 0;   /* stale: no clock */
    s_clk.have_last = 0;
  }
}

bool clock_in_edge(void)
{
  if (s_clk.edge_pending) { s_clk.edge_pending = 0; return true; }
  return false;
}

bool clock_in_present(void)
{
  return s_clk.period_ticks != 0;
}

float clock_in_hz(void)
{
  return s_clk.period_ticks ? (F_TIM2 / (float)s_clk.period_ticks) : 0.0f;
}

float clock_in_bpm(uint8_t ppqn)
{
  if (!s_clk.period_ticks || ppqn == 0) return 0.0f;
  return clock_in_hz() * 60.0f / (float)ppqn;
}
