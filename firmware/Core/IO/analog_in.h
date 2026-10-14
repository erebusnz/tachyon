#ifndef ANALOG_IN_H
#define ANALOG_IN_H

#include <stdint.h>

/* Analog inputs on ADC1 (1 kHz timer-triggered DMA scan):
 * CV-IN A (PA0), CV-IN B (PA1),
 * and the panel pot USR_POT_1 (PC0). See cv-input.md and user-interface.md §3.
 *
 * Acquisition continues during OLED rendering and SD access. CV is unfiltered
 * to preserve steps; the pot uses a one-pole filter (1/8 at 1 kHz, ~7.5 ms).
 * Accessors return the latest completed scan, without touching the ADC.
 */

#define ANALOG_CV_A   0u
#define ANALOG_CV_B   1u

/* Start TIM8/ADC1/DMA2 Stream0 and wait for the first scan (startup only). */
void analog_in_init(void);

/* DMA2 Stream0 interrupt dispatch; no polling or blocking work. */
void analog_in_dma_irq(void);

/* Completed scans, for acquisition-rate diagnostics (wraps naturally). */
uint32_t analog_in_scan_count(void);

/* Latest raw ADC code (0..4095) for a CV-IN channel. */
uint16_t analog_in_cv_code(uint8_t ch);

/* Latest CV-IN voltage using the cv-input.md inverting transfer function. */
float analog_in_cv_volts(uint8_t ch);

/* Filtered pot code (0..4095). */
uint16_t analog_in_pot(void);

#endif
