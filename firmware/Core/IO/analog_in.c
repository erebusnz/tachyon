#include "analog_in.h"
#include "main.h"

extern ADC_HandleTypeDef hadc1;

/* hadc1 scans 3 ranks: [0] = PA0 CV_IN_A, [1] = PA1 CV_IN_B, [2] = PC0 pot. */
#define N_CH   3
#define AVG_N  8          /* moving-average window (power of two) */

static uint16_t s_hist[N_CH][AVG_N];
static uint32_t s_sum[N_CH];
static uint8_t  s_idx;

/* Read all three ranks in one scan (cv-input.md §5: 56-cycle sample time). */
static void adc_read_all(uint16_t v[N_CH])
{
  HAL_ADC_Start(&hadc1);
  for (int i = 0; i < N_CH; i++) {
    HAL_ADC_PollForConversion(&hadc1, 10);
    v[i] = (uint16_t)HAL_ADC_GetValue(&hadc1);
  }
  HAL_ADC_Stop(&hadc1);
}

void analog_in_sample(void)
{
  uint16_t v[N_CH];
  adc_read_all(v);
  for (int ch = 0; ch < N_CH; ch++) {
    s_sum[ch] -= s_hist[ch][s_idx];
    s_hist[ch][s_idx] = v[ch];
    s_sum[ch] += v[ch];
  }
  s_idx = (uint8_t)((s_idx + 1u) % AVG_N);
}

static uint16_t avg(int ch)
{
  return (uint16_t)(s_sum[ch] / AVG_N);
}

uint16_t analog_in_cv_code(uint8_t ch)
{
  return avg(ch == ANALOG_CV_B ? 1 : 0);
}

float analog_in_cv_volts(uint8_t ch)
{
  /* cv-input.md: inverting map, V_in = (1.663 - Vadc) / 0.330. */
  float vadc = (float)analog_in_cv_code(ch) * 3.3f / 4095.0f;
  return (1.663f - vadc) / 0.330f;
}

uint16_t analog_in_pot(void)
{
  return avg(2);
}

void analog_in_init(void)
{
  uint16_t v[N_CH];
  adc_read_all(v);
  for (int ch = 0; ch < N_CH; ch++) {
    s_sum[ch] = 0;
    for (int i = 0; i < AVG_N; i++) {
      s_hist[ch][i] = v[ch];
      s_sum[ch] += v[ch];
    }
  }
  s_idx = 0;
}
