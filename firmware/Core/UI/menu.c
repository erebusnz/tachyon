#include "menu.h"
#include "text.h"
#include "GUI_Paint.h"

#define SCREEN_W       128
#define SCREEN_H       128

/* Grayscale levels (Paint scale 16). */
#define BG_LEVEL       0
#define FG_LEVEL       15
#define BORDER_LEVEL   7      /* rounded outline on unselected items */

/* Layout */
#define ITEM_H         24     /* rounded box height */
#define ITEM_GAP       4
#define BOX_X0         4
#define BOX_X1         (SCREEN_W - 5)
#define BOX_R          5      /* corner radius */
#define TEXT_INSET_Y   3      /* text top-line within the box */

void menu_init(Menu *m, const char *const *items, uint8_t count)
{
    m->items = items;
    m->count = count;
    m->selected = 0;
}

void menu_move(Menu *m, int32_t delta)
{
    if (m->count == 0)
        return;

    int32_t n = (int32_t)m->count;
    int32_t sel = ((int32_t)m->selected + delta) % n;
    if (sel < 0)
        sel += n;
    m->selected = (uint8_t)sel;
}

void menu_render(const Menu *m)
{
    Paint_Clear(BG_LEVEL);

    if (m->count == 0)
        return;

    /* Vertically center the stack of items. (>5 items would need scrolling.) */
    int total = (int)m->count * ITEM_H + ((int)m->count - 1) * ITEM_GAP;
    int top0  = (SCREEN_H - total) / 2;
    if (top0 < 0) top0 = 0;

    /* Each item sits in a rounded box; the selected one is filled and its text
     * inverts to dark-on-light. */
    for (uint8_t i = 0; i < m->count; i++) {
        int by0 = top0 + i * (ITEM_H + ITEM_GAP);
        int by1 = by0 + ITEM_H;
        int yt  = by0 + TEXT_INSET_Y;

        if (i == m->selected) {
            gfx_round_rect(BOX_X0, by0, BOX_X1, by1, BOX_R, FG_LEVEL, 1);
            text_draw_centered(BOX_X0, BOX_X1, yt, m->items[i], BG_LEVEL, FG_LEVEL);
        } else {
            gfx_round_rect(BOX_X0, by0, BOX_X1, by1, BOX_R, BORDER_LEVEL, 0);
            text_draw_centered(BOX_X0, BOX_X1, yt, m->items[i], FG_LEVEL, BG_LEVEL);
        }
    }
}
