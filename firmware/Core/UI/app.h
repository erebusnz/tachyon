#ifndef APP_H
#define APP_H

#include <stdint.h>
#include <stdbool.h>

/* Top-level UI state machine for the Tachyon front panel.
 *
 * On boot the device shows a menu of operating modes (see
 * Plans/hardware_ui_design_circle_of_fifths_controller.md). Selecting a mode
 * opens that mode's screen; a long-press on the encoder returns to the menu.
 *
 * Logic and drawing are split so the caller can run them at different rates:
 * app_tick() (input + engines, cheap) every main-loop pass for tight arp/gate
 * timing, app_render() + OLED_1in5_Display() only at the display frame rate.
 * The caller initialises the OLED and the Paint image (Scale=16); app_render()
 * only issues Paint_* draws into the current image. The encoder must be
 * initialised (encoder_init) before app_tick() is called.
 */

/* Do all boot-time SD I/O while audio is still silent: cache the wavetable
 * folder list and load the default wavetable. Call once BEFORE audio_init()
 * (and after the SD volume is mounted) so SD reads never contend with the audio
 * DMA/render IRQ. */
void app_preload(void);

/* Reset to the main menu and clear per-mode state, and point the oscillator at
 * the preloaded wavetable. Call once after audio_init()/wt_osc_install(). */
void app_init(void);

/* Consume pending encoder input and advance the state machine + engines (arp,
 * MIDI drain, gate window). Call once per main-loop pass. Does NOT draw. */
void app_tick(void);

/* True when the screen content changed since the last app_render() — the
 * caller should skip rendering (and the framebuffer push) entirely when false;
 * full-screen renders cost tens of ms (the COF wheel is procedural) and would
 * jitter the arp/gate timing for no visual change. */
bool app_dirty(void);

/* Render the current screen into the selected Paint image (clears the dirty
 * flag). Call at the display frame rate when app_dirty(), before pushing the
 * framebuffer with OLED_1in5_Display(). */
void app_render(void);

/* USB-MIDI input seam. The USB-MIDI device class (usbd_midi.c) calls
 * app_midi_event() from the OTG IRQ for each parsed note event; events are
 * queued lock-free and drained on the main loop in app_tick(), driving the
 * polyphonic voice pool while the USB MIDI mode is active. Velocity drives
 * the destination picked by the Filter Cfg VelMode row (filters.md §3.5). */
#define APP_MIDI_NOTE_OFF  0u
#define APP_MIDI_NOTE_ON   1u
void app_midi_event(uint8_t kind, uint8_t note, uint8_t vel);

#endif
