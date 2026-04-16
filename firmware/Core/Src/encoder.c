#include "encoder.h"
#include "main.h"

/* TIM4 is configured in x4 encoder mode: 4 counts per detent on the
 * EC11E18244AU (24 PPR, 24 detents/rev = 1 quad cycle per detent).
 * We divide by 4 to return detent units. */
#define COUNTS_PER_DETENT   4

/* Long-press threshold in ms. See user-interface.md §2.5. */
#define LONG_PRESS_MS       500

/* Software debounce on the switch, on top of the 10 nF cap.
 * With the KY-040 breakout (no cap) this also handles contact bounce. */
#define DEBOUNCE_MS         10

extern TIM_HandleTypeDef htim4;

static int16_t  s_last_count;       /* signed snapshot of TIM4->CNT */
static int32_t  s_detent_residual;  /* leftover raw counts < 4 */

static volatile uint8_t  s_btn_pressed;    /* 1 while SW is held down */
static volatile uint16_t s_btn_hold_ms;    /* how long it's been held */
static volatile uint8_t  s_btn_debounce;   /* debounce countdown after edge */
static volatile encoder_btn_event_t s_btn_event;
static volatile uint8_t  s_long_press_fired; /* latched so we only fire once */

void encoder_init(void)
{
    HAL_TIM_Encoder_Start(&htim4, TIM_CHANNEL_ALL);
    s_last_count = (int16_t)__HAL_TIM_GET_COUNTER(&htim4);
    s_detent_residual = 0;
    s_btn_pressed = 0;
    s_btn_hold_ms = 0;
    s_btn_debounce = 0;
    s_btn_event = ENC_BTN_NONE;
    s_long_press_fired = 0;
}

int32_t encoder_get_delta(void)
{
    int16_t  now  = (int16_t)__HAL_TIM_GET_COUNTER(&htim4);
    int16_t  diff = (int16_t)(now - s_last_count); /* wraps correctly mod 2^16 */
    s_last_count = now;

    int32_t raw = s_detent_residual + diff;
    int32_t detents = raw / COUNTS_PER_DETENT;
    s_detent_residual = raw - (detents * COUNTS_PER_DETENT);
    return detents;
}

encoder_btn_event_t encoder_get_button_event(void)
{
    encoder_btn_event_t ev = s_btn_event;
    s_btn_event = ENC_BTN_NONE;
    return ev;
}

/* Falling edge: button just went down. */
void encoder_button_isr(void)
{
    if (s_btn_debounce == 0 && !s_btn_pressed) {
        s_btn_pressed = 1;
        s_btn_hold_ms = 0;
        s_btn_debounce = DEBOUNCE_MS;
        s_long_press_fired = 0;
    }
}

/* Called from SysTick at 1 kHz. Handles:
 *  - debounce window after press
 *  - detecting release (poll PB4 level)
 *  - firing long-press while still held
 *  - firing short-press on release
 */
void encoder_tick_1ms(void)
{
    if (s_btn_debounce) {
        s_btn_debounce--;
    }

    if (!s_btn_pressed) {
        return;
    }

    s_btn_hold_ms++;

    /* Long press fires as soon as threshold is crossed, even before release. */
    if (!s_long_press_fired && s_btn_hold_ms >= LONG_PRESS_MS) {
        s_long_press_fired = 1;
        s_btn_event = ENC_BTN_LONG_PRESS;
    }

    /* Poll the pin to detect release (no rising-edge EXTI configured). */
    if (HAL_GPIO_ReadPin(USR_ENC_SW_GPIO_Port, USR_ENC_SW_Pin) == GPIO_PIN_SET) {
        s_btn_pressed = 0;
        s_btn_debounce = DEBOUNCE_MS;
        if (!s_long_press_fired) {
            s_btn_event = ENC_BTN_SHORT_PRESS;
        }
    }
}
