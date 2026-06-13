#ifndef APP_H
#define APP_H

/* Top-level UI state machine for the Tachyon front panel.
 *
 * On boot the device shows a menu of operating modes (see
 * Plans/hardware_ui_design_circle_of_fifths_controller.md). Selecting a mode
 * opens that mode's screen; a long-press on the encoder returns to the menu.
 *
 * The caller is responsible for initialising the OLED and the Paint image
 * (Scale=16) and for pushing the framebuffer with OLED_1in5_Display() after
 * each app_tick(); app_tick() only issues Paint_* draws into the current image.
 * The encoder must be initialised (encoder_init) before app_tick() is called.
 */

/* Reset to the main menu and clear per-mode state. Call once after init. */
void app_init(void);

/* Consume pending encoder input, advance the state machine, and render the
 * current screen into the selected Paint image. Call once per main-loop pass. */
void app_tick(void);

#endif
