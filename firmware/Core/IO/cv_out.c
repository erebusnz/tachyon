#include "cv_out.h"
#include "main.h"
#include <math.h>

extern SPI_HandleTypeDef hspi2;

/* Two-point calibration per physical jack (calibration.md §2):
 *   code = round((V_target - offset) / slope). */
typedef struct {
  float slope;    /* volts per DAC code */
  float offset;   /* volts at DAC code 0 */
} cv_cal_t;

#define CV_NOMINAL_SLOPE  (10.0f / 65535.0f)   /* ideal 152.6 uV/LSB */

static cv_cal_t s_cal[2] = {
  { CV_NOMINAL_SLOPE, 0.0f },   /* jack A */
  { CV_NOMINAL_SLOPE, 0.0f },   /* jack B */
};

void cv_out_write_code(uint8_t jack, uint16_t code)
{
  /* DAC8552 24-bit frame (cv-output-dac.md §3): the channel is the BUFFER bit
   * (DB18), and the physical jacks are cross-wired to the DAC channels —
   * jack A -> DAC ch B -> ctrl 0x24, jack B -> DAC ch A -> ctrl 0x10. */
  uint8_t ctrl  = (jack == CV_OUT_A) ? 0x24u : 0x10u;
  uint8_t tx[3] = { ctrl, (uint8_t)(code >> 8), (uint8_t)(code & 0xFF) };

  HAL_GPIO_WritePin(DAC_SPI_CS_GPIO_Port, DAC_SPI_CS_Pin, GPIO_PIN_RESET);
  HAL_SPI_Transmit(&hspi2, tx, 3, 10);
  HAL_GPIO_WritePin(DAC_SPI_CS_GPIO_Port, DAC_SPI_CS_Pin, GPIO_PIN_SET);
}

void cv_out_write_volts(uint8_t jack, float volts)
{
  uint8_t j = (jack == CV_OUT_B) ? 1u : 0u;
  long code = lroundf((volts - s_cal[j].offset) / s_cal[j].slope);
  if (code < 0)     code = 0;
  if (code > 65535) code = 65535;
  cv_out_write_code(jack, (uint16_t)code);
}

void cv_out_set_cal(uint8_t jack, float slope, float offset)
{
  uint8_t j = (jack == CV_OUT_B) ? 1u : 0u;
  s_cal[j].slope  = slope;
  s_cal[j].offset = offset;
}

void cv_out_init(void)
{
  cv_out_write_code(CV_OUT_A, 0);
  cv_out_write_code(CV_OUT_B, 0);
}
