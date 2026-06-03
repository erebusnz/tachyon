#ifndef TEST_CONSOLE_H
#define TEST_CONSOLE_H

#include <stdint.h>

/* USB-CDC test console for Tachyon IO bring-up.
 * See test-firmware.md for the command set and bench procedures. */

/* Configure pins the console drives directly (gate GPIOs, clk capture)
 * and print the banner. Call once after all MX_* init in main(). */
void console_init(void);

/* Push one received byte into the RX ring. Called from CDC_Receive_FS
 * (USB IRQ context) — lock-free single-producer enqueue only. */
void console_rx_byte(uint8_t b);

/* Drain the RX ring, dispatch complete command lines, and service the
 * non-blocking background jobs (gate clock, gate pulse, ADC stream,
 * tone playback, encoder timing). Call every main-loop iteration. */
void console_poll(void);

#endif /* TEST_CONSOLE_H */
