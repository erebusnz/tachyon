#ifndef TEST_CONSOLE_H
#define TEST_CONSOLE_H

#include <stdint.h>

/* Tachyon IO bring-up test firmware: an on-device OLED menu UI driven by the
 * encoder + pot, plus a USB-CDC command console in parallel. See
 * test-firmware.md for the serial command set and bench procedures. */

/* Init the IO pins, OLED, encoder, I2S stream and the menu UI, then print the
 * console banner. Call once after all MX_* init in main(). */
void console_init(void);

/* Push one received byte into the RX ring. Called from CDC_Receive_FS
 * (USB IRQ context) — lock-free single-producer enqueue only. */
void console_rx_byte(uint8_t b);

/* Drain the RX ring + dispatch serial commands, service the non-blocking
 * background jobs (gate clock/pulse, ADC stream), and run the menu UI
 * (encoder nav, pot parameter, OLED render). Call every main-loop iteration.
 * (Encoder debounce/long-press timing itself runs in SysTick.) */
void console_poll(void);

#endif /* TEST_CONSOLE_H */
