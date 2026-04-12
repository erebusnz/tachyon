#include "circle_of_fifths.h"
#include "GUI_Paint.h"
#include "note_sprites.h"
#include <math.h>

#define WIDTH          128
#define HEIGHT         128
#define NUM_SEGMENTS   12
#define SEGMENT_ANGLE  30.0f   // 360 / 12
#define NUM_VISIBLE    2

#define OUTER_RADIUS   400.0f
#define INNER_RADIUS   (OUTER_RADIUS * 0.75f)
#define BORDER_WIDTH   5.0f

#define GREY_LIGHT     13
#define GREY_DARK      2
#define BG_LEVEL       0
#define BORDER_LEVEL   15

#define DEG2RAD        (3.14159265f / 180.0f)
#define RAD2DEG        (180.0f / 3.14159265f)

// Dots along the inner edge
#define DOT_RADIUS_BIG    8.0f    // virtual units
#define DOT_RADIUS_SMALL  4.0f
#define DOT_TRACK_RADIUS  (OUTER_RADIUS + DOT_RADIUS_BIG * 4.0f)  // above outer border
#define DOT_LEVEL         10      // grey level for dots
#define NUM_DOTS          24      // one every 15° (big at 0,30,60... small at 15,45,75...)

// Number of accidentals per note (C=0, G=1, ... F#=6, ... F=1)
static const uint8_t accidentals[NUM_SEGMENTS] = {
    0, 1, 2, 3, 4, 5, 6, 5, 4, 3, 2, 1
};

// Sprite angle indices: 0=-30°, 1=-15°, 2=0°, 3=+15°, 4=+30°
static const float sprite_angles[NOTE_SPRITE_NUM_ANGLES] = {
    -30.0f, -15.0f, 0.0f, 15.0f, 30.0f
};

// Viewport (constant, computed once)
static float vp_x, vp_y, vp_size;
static int vp_ready = 0;

static void compute_viewport(void)
{
    float half_span = NUM_VISIBLE * SEGMENT_ANGLE / 2.0f;
    float ang_start = -90.0f - half_span;
    float ang_end   = -90.0f + half_span;

    float min_x = 1e9f, min_y = 1e9f;
    float max_x = -1e9f, max_y = -1e9f;

    int steps = 64;
    for (int s = 0; s <= steps; s++) {
        float ang = (ang_start + (ang_end - ang_start) * s / steps) * DEG2RAD;
        float ca = cosf(ang);
        float sa = sinf(ang);
        for (int ri = 0; ri < 2; ri++) {
            float r = (ri == 0) ? INNER_RADIUS : OUTER_RADIUS;
            float x = r * ca;
            float y = r * sa;
            if (x < min_x) min_x = x;
            if (x > max_x) max_x = x;
            if (y < min_y) min_y = y;
            if (y > max_y) max_y = y;
        }
    }

    float padding = BORDER_WIDTH + 2.0f;
    min_x -= padding;  min_y -= padding;
    max_x += padding;  max_y += padding;

    float w = max_x - min_x;
    float h = max_y - min_y;
    float size = (w > h) ? w : h;
    float mid_x = (min_x + max_x) / 2.0f;
    float mid_y = (min_y + max_y) / 2.0f;

    size *= 1.05f;
    vp_x = mid_x - size / 2.0f;
    vp_y = mid_y - size / 2.0f;
    vp_size = size;
    vp_ready = 1;
}

// Find nearest sprite angle index
static int angle_index(float theta_from_top)
{
    int best = 0;
    float best_dist = fabsf(theta_from_top - sprite_angles[0]);
    for (int i = 1; i < NOTE_SPRITE_NUM_ANGLES; i++) {
        float d = fabsf(theta_from_top - sprite_angles[i]);
        if (d < best_dist) { best_dist = d; best = i; }
    }
    return best;
}

// Get a 4-bit pixel from packed sprite data (high nibble first)
static inline uint8_t sprite_get(const uint8_t *buf, int x, int y, int w)
{
    int idx = y * w + x;
    int byte = idx / 2;
    if (idx & 1)
        return buf[byte] & 0x0F;
    else
        return (buf[byte] >> 4) & 0x0F;
}

// Draw an anti-aliased dot at virtual position (cx,cy) with given radius
static void draw_dot(float cx, float cy, float radius, uint8_t level)
{
    float px_per_virt = WIDTH / vp_size;
    int screen_r = (int)(radius * px_per_virt + 1.5f);
    int scx = (int)((cx - vp_x) / vp_size * WIDTH);
    int scy = (int)((cy - vp_y) / vp_size * HEIGHT);

    for (int dy = -screen_r; dy <= screen_r; dy++) {
        int py = scy + dy;
        if (py < 0 || py >= HEIGHT) continue;
        for (int dx = -screen_r; dx <= screen_r; dx++) {
            int px = scx + dx;
            if (px < 0 || px >= WIDTH) continue;

            // Distance in virtual units
            float vdx = dx / px_per_virt;
            float vdy = dy / px_per_virt;
            float dist = sqrtf(vdx * vdx + vdy * vdy);

            if (dist > radius + 1.0f) continue;

            // Anti-alias: blend at the edge
            float alpha = radius - dist + 0.5f;
            if (alpha <= 0.0f) continue;
            if (alpha > 1.0f) alpha = 1.0f;

            int pixel = (int)(level * alpha + 0.5f);
            if (pixel > 0)
                Paint_SetPixel(px, py, (UWORD)pixel);
        }
    }
}

void cof_render_angle(float angle_deg)
{
    if (!vp_ready)
        compute_viewport();

    // Segments snap to 15° half-steps, dots move smoothly
    float snapped = roundf(angle_deg / 15.0f) * 15.0f;
    float rotation = -snapped;
    float smooth_rotation = -angle_deg;

    // Precompute segment grey levels
    uint8_t seg_grey[NUM_SEGMENTS];
    for (int i = 0; i < NUM_SEGMENTS; i++) {
        float t = accidentals[i] / 6.0f;
        seg_grey[i] = (uint8_t)(GREY_LIGHT + t * (GREY_DARK - GREY_LIGHT) + 0.5f);
    }

    float half_bw = BORDER_WIDTH / 2.0f;

    Paint_Clear(BG_LEVEL);

    // --- Pass 1: render donut segments ---
    for (int py = 0; py < HEIGHT; py++) {
        for (int px = 0; px < WIDTH; px++) {
            float x = vp_x + (px + 0.5f) / WIDTH * vp_size;
            float y = vp_y + (py + 0.5f) / HEIGHT * vp_size;

            float r = sqrtf(x * x + y * y);

            float dist_outer = r - OUTER_RADIUS;
            float dist_inner = INNER_RADIUS - r;

            if (dist_outer > half_bw + 1.0f || dist_inner > half_bw + 1.0f)
                continue;

            float angle = atan2f(y, x) * RAD2DEG;
            float adjusted = fmodf(angle + 90.0f + SEGMENT_ANGLE / 2.0f - rotation + 720.0f, 360.0f);
            int seg_index = (int)(adjusted / SEGMENT_ANGLE);
            if (seg_index >= NUM_SEGMENTS) seg_index = 0;
            float seg_pos = adjusted - seg_index * SEGMENT_ANGLE;

            float dist_to_start = seg_pos * DEG2RAD * r;
            float dist_to_end = (SEGMENT_ANGLE - seg_pos) * DEG2RAD * r;
            float dist_radial = (dist_to_start < dist_to_end) ? dist_to_start : dist_to_end;

            float dist_circle = fabsf(dist_outer);
            float d = fabsf(dist_inner);
            if (d < dist_circle) dist_circle = d;
            float dist_border = (dist_circle < dist_radial) ? dist_circle : dist_radial;

            float border_alpha = (dist_border - half_bw + 1.0f) / 2.0f;
            if (border_alpha < 0.0f) border_alpha = 0.0f;
            if (border_alpha > 1.0f) border_alpha = 1.0f;

            uint8_t fill;
            if (dist_outer > 0.0f || dist_inner > 0.0f)
                fill = BG_LEVEL;
            else
                fill = seg_grey[seg_index];

            float value = fill * border_alpha + BORDER_LEVEL * (1.0f - border_alpha);
            int pixel = (int)(value + 0.5f);
            if (pixel < 0) pixel = 0;
            if (pixel > 15) pixel = 15;

            if (pixel != BG_LEVEL)
                Paint_SetPixel(px, py, (UWORD)pixel);
        }
    }

    // --- Pass 2: blit pre-rendered text sprites ---
    for (int i = 0; i < NUM_SEGMENTS; i++) {
        float theta_from_top = i * SEGMENT_ANGLE + rotation;
        while (theta_from_top < -180.0f) theta_from_top += 360.0f;
        while (theta_from_top >  180.0f) theta_from_top -= 360.0f;
        if (theta_from_top < -32.0f || theta_from_top > 32.0f) continue;

        int ai = angle_index(theta_from_top);
        const NoteSprite *sp = &note_sprites[i][ai];
        if (sp->w == 0) continue;

        for (int sy = 0; sy < sp->h; sy++) {
            int py = sp->oy + sy;
            if (py < 0 || py >= HEIGHT) continue;
            for (int sx = 0; sx < sp->w; sx++) {
                int px = sp->ox + sx;
                if (px < 0 || px >= WIDTH) continue;

                uint8_t text_alpha = sprite_get(sp->pixels, sx, sy, sp->w);
                if (text_alpha == 0) continue;

                float x = vp_x + (px + 0.5f) / WIDTH * vp_size;
                float y = vp_y + (py + 0.5f) / HEIGHT * vp_size;
                float angle = atan2f(y, x) * RAD2DEG;
                float adjusted = fmodf(angle + 90.0f + SEGMENT_ANGLE / 2.0f - rotation + 720.0f, 360.0f);
                int seg_index = (int)(adjusted / SEGMENT_ANGLE);
                if (seg_index >= NUM_SEGMENTS) seg_index = 0;
                uint8_t bg = seg_grey[seg_index];

                float alpha = text_alpha / 15.0f;
                int pixel = (int)(bg * (1.0f - alpha) + 0.5f);
                if (pixel < 0) pixel = 0;
                if (pixel > 15) pixel = 15;

                Paint_SetPixel(px, py, (UWORD)pixel);
            }
        }
    }

    // --- Pass 3: dots along inner edge ---
    for (int d = 0; d < NUM_DOTS; d++) {
        float dot_angle_deg = d * (360.0f / NUM_DOTS) - 90.0f + smooth_rotation;
        // Normalize
        while (dot_angle_deg < -180.0f) dot_angle_deg += 360.0f;
        while (dot_angle_deg >  180.0f) dot_angle_deg -= 360.0f;
        // Skip dots far from viewport
        float from_top = dot_angle_deg + 90.0f;
        if (from_top < -40.0f || from_top > 40.0f) continue;

        float ang_rad = dot_angle_deg * DEG2RAD;
        float cx = DOT_TRACK_RADIUS * cosf(ang_rad);
        float cy = DOT_TRACK_RADIUS * sinf(ang_rad);

        // Big dot at segment centers (every 30° = every 2nd dot),
        // small dot at borders (the odd ones)
        int is_big = (d % 2 == 0);
        float radius = is_big ? DOT_RADIUS_BIG : DOT_RADIUS_SMALL;

        draw_dot(cx, cy, radius, DOT_LEVEL);
    }
}
