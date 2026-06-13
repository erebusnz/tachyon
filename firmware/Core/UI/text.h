#ifndef TEXT_H
#define TEXT_H

#include <stdint.h>

/* Anti-aliased text + rounded-rect helpers for the 128x128 4-bit OLED.
 *
 * Renders the generated Inter atlas (inter_font.h) into the current Paint image
 * (Scale=16), same convention as cof_render_angle / menu_render. Levels are
 * grayscale 0..15. Glyph coverage is alpha-blended between bg and fg, so the
 * caller passes the background level explicitly (no framebuffer read-back).
 */

/* Total advance width of `s` in pixels. */
int  text_width(const char *s);

/* Font line height (for vertical spacing). */
int  text_line_height(void);

/* Draw `s` with its top-line at (x, y_top); fg = ink level, bg = the level the
 * text sits on (must match what was already drawn there). */
void text_draw(int x, int y_top, const char *s, uint8_t fg, uint8_t bg);

/* Draw `s` horizontally centered in [x0,x1] at top-line y_top. */
void text_draw_centered(int x0, int x1, int y_top, const char *s, uint8_t fg, uint8_t bg);

/* Rounded rectangle from (x0,y0) to (x1,y1), corner radius r, at `level`.
 * fill != 0 fills the interior; fill == 0 draws a 1px border. */
void gfx_round_rect(int x0, int y0, int x1, int y1, int r, uint8_t level, int fill);

#endif
