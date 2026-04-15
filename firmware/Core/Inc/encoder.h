#ifndef ENCODER_H
#define ENCODER_H

#include <stdint.h>

typedef enum {
    ENC_BTN_NONE = 0,
    ENC_BTN_SHORT_PRESS,
    ENC_BTN_LONG_PRESS,
} encoder_btn_event_t;

/* Initialize encoder: starts TIM4 quadrature counting. Call after MX_TIM4_Init. */
void encoder_init(void);

/* Signed detent delta since last call. One detent = one full quadrature cycle (x4).
 * Positive = CW, negative = CCW. Resets internal accumulator. */
int32_t encoder_get_delta(void);

/* Returns a pending button event, or ENC_BTN_NONE. Consumes the event. */
encoder_btn_event_t encoder_get_button_event(void);

/* Called from EXTI3 ISR (falling edge of PB3). */
void encoder_button_isr(void);

/* Called from main loop on each tick (~1 ms) to time long-press and debounce. */
void encoder_tick_1ms(void);

#endif
