#ifndef CV_OUT_H
#define CV_OUT_H

#include <stdint.h>

/* Precision CV output: DAC8552 (SPI2) -> OPA1642 x4 -> 0..10 V at the jacks.
 * See cv-output-dac.md and calibration.md.
 *
 * Jacks are addressed by physical index (CV_OUT_A / CV_OUT_B). The DAC8552's
 * A/B channel cross-wiring is handled inside this module, so callers always
 * think in terms of the front-panel jack.
 */

#define CV_OUT_A   0u
#define CV_OUT_B   1u

/* Zero both jacks and load nominal calibration defaults. */
void cv_out_init(void);

/* Write a raw 16-bit DAC code (0..65535) to a jack. */
void cv_out_write_code(uint8_t jack, uint16_t code);

/* Write a calibrated output voltage (0..10 V) to a jack, applying the per-jack
 * slope/offset. Out-of-range voltages clamp to the 0..65535 code range. */
void cv_out_write_volts(uint8_t jack, float volts);

/* Override the per-jack two-point calibration (volts/LSB and volts at code 0).
 * Defaults are nominal (10.0/65535 V per LSB, 0 V offset) until set. */
void cv_out_set_cal(uint8_t jack, float slope, float offset);

#endif
