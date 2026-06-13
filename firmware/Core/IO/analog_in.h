#ifndef ANALOG_IN_H
#define ANALOG_IN_H

#include <stdint.h>

/* Analog inputs on ADC1 (polled 3-rank scan): CV-IN A (PA0), CV-IN B (PA1),
 * and the panel pot USR_POT_1 (PC0). See cv-input.md and user-interface.md §3.
 *
 * Readings are smoothed with an 8-sample moving average. Call analog_in_sample()
 * periodically (e.g. every UI tick); the accessors return the filtered value.
 */

#define ANALOG_CV_A   0u
#define ANALOG_CV_B   1u

/* Prime the moving-average filter to the current input level. */
void analog_in_init(void);

/* Run one ADC scan and fold the result into the moving average. */
void analog_in_sample(void);

/* Filtered raw ADC code (0..4095) for a CV-IN channel. */
uint16_t analog_in_cv_code(uint8_t ch);

/* Filtered CV-IN voltage using the cv-input.md inverting transfer function. */
float analog_in_cv_volts(uint8_t ch);

/* Filtered pot code (0..4095). */
uint16_t analog_in_pot(void);

#endif
