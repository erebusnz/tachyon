#ifndef MENU_H
#define MENU_H

#include <stdint.h>

/* Reusable vertical-list menu widget for the 128x128 SSD1327 OLED.
 *
 * Rendering draws into the currently selected Paint image via the GUI_Paint
 * API (same convention as cof_render_angle); the caller owns the framebuffer
 * and pushes it to the panel with OLED_1in5_Display(). The Paint image must be
 * initialised with Scale=16 (4-bit grayscale, levels 0..15) before rendering.
 */
typedef struct {
    const char *const *items;  /* array of NUL-terminated item labels */
    uint8_t count;             /* number of items */
    uint8_t selected;          /* index of the highlighted item */
} Menu;

/* Initialise a menu over `items` (count entries), selection at index 0. */
void menu_init(Menu *m, const char *const *items, uint8_t count);

/* Move the selection by `delta` steps, wrapping around the ends. */
void menu_move(Menu *m, int32_t delta);

/* Render the item list, vertically centered, into the current Paint image, with
 * the selected row drawn as a highlighted (inverted) rounded bar. */
void menu_render(const Menu *m);

#endif
