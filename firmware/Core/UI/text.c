#include "text.h"
#include "GUI_Paint.h"
#include "inter_font.h"

#define SCREEN_W   128
#define SCREEN_H   128

static const Glyph *glyph_for(char c)
{
    unsigned uc = (unsigned char)c;
    if (uc < FONT_FIRST || uc > FONT_LAST) return 0;
    return &inter_glyphs[uc - FONT_FIRST];
}

/* 4-bit coverage value at flat index i (high nibble first). */
static inline uint8_t cover(const uint8_t *p, int i)
{
    uint8_t b = p[i >> 1];
    return (i & 1) ? (uint8_t)(b & 0x0F) : (uint8_t)(b >> 4);
}

static inline void put(int x, int y, uint8_t level)
{
    if (x < 0 || x >= SCREEN_W || y < 0 || y >= SCREEN_H) return;
    Paint_SetPixel((UWORD)x, (UWORD)y, (UWORD)level);
}

int text_line_height(void) { return FONT_LINE_H; }

int text_width(const char *s)
{
    int w = 0;
    for (; *s; s++) {
        const Glyph *g = glyph_for(*s);
        if (g) w += g->advance;
    }
    return w;
}

void text_draw(int x, int y_top, const char *s, uint8_t fg, uint8_t bg)
{
    int pen = x;
    for (; *s; s++) {
        const Glyph *g = glyph_for(*s);
        if (!g) continue;
        if (g->pixels && g->w && g->h) {
            int gx = pen + g->left;
            int gy = y_top + g->top;
            int i = 0;
            for (int yy = 0; yy < g->h; yy++) {
                for (int xx = 0; xx < g->w; xx++, i++) {
                    uint8_t a = cover(g->pixels, i);
                    if (!a) continue;
                    /* result = bg*(1-a) + fg*a, a in [0,1] = cover/15 */
                    int v = (bg * (15 - a) + fg * a + 7) / 15;
                    put(gx + xx, gy + yy, (uint8_t)v);
                }
            }
        }
        pen += g->advance;
    }
}

void text_draw_centered(int x0, int x1, int y_top, const char *s, uint8_t fg, uint8_t bg)
{
    int w = text_width(s);
    int x = x0 + ((x1 - x0) - w) / 2;
    if (x < x0) x = x0;
    text_draw(x, y_top, s, fg, bg);
}

void gfx_round_rect(int x0, int y0, int x1, int y1, int r, uint8_t level, int fill)
{
    if (x1 < x0 || y1 < y0) return;
    int w = x1 - x0, h = y1 - y0;
    if (r * 2 > w) r = w / 2;
    if (r * 2 > h) r = h / 2;
    if (r < 0) r = 0;

    for (int y = y0; y <= y1; y++) {
        for (int x = x0; x <= x1; x++) {
            /* Which corner region (if any) is this pixel in? */
            int cx = -1, cy = -1;
            if      (x < x0 + r && y < y0 + r) { cx = x0 + r; cy = y0 + r; }
            else if (x > x1 - r && y < y0 + r) { cx = x1 - r; cy = y0 + r; }
            else if (x < x0 + r && y > y1 - r) { cx = x0 + r; cy = y1 - r; }
            else if (x > x1 - r && y > y1 - r) { cx = x1 - r; cy = y1 - r; }

            if (cx >= 0) {
                int dx = x - cx, dy = y - cy;
                int d2 = dx * dx + dy * dy;
                if (d2 > r * r) continue;                 /* outside the rounded corner */
                if (!fill && d2 <= (r - 1) * (r - 1))      /* inside the arc ring */
                    continue;
                put(x, y, level);
            } else if (fill) {
                put(x, y, level);
            } else if (x == x0 || x == x1 || y == y0 || y == y1) {
                put(x, y, level);                          /* straight border */
            }
        }
    }
}
