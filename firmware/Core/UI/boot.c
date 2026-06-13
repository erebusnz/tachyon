#include "boot.h"
#include "OLED_1in5.h"
#include "GUI_Paint.h"
#include "main.h"          /* HAL_GetTick */

/* Starfield warp: stars fly from the center outward, accelerating into a
 * light-speed streak. Each star is a point in normalized space (x,y in [-1,1],
 * depth z in (0,1]); the screen projection is (cx + x/z*PROJ, cy + y/z*PROJ),
 * so as z shrinks the star races toward the edge and its streak lengthens. */

#define NSTARS   72
#define CX       64
#define CY       64
#define PROJ     34.0f
#define Z_NEAR   0.05f

typedef struct {
  float x, y, z;
  int   psx, psy;     /* previous projected position (for the motion streak) */
  uint8_t has_prev;
} Star;

static Star s_star[NSTARS];
static uint32_t s_rng = 0x1234abcdu;

static float frand(void)
{
  s_rng = s_rng * 1664525u + 1013904223u;
  return (float)(s_rng >> 8) / (float)(1u << 24);   /* [0,1) */
}

static void respawn(Star *s, int spread_depth)
{
  do {
    s->x = frand() * 2.0f - 1.0f;
    s->y = frand() * 2.0f - 1.0f;
  } while (s->x * s->x + s->y * s->y < 0.02f);       /* avoid the dead center */
  /* New stars start far away; at init we spread depth so the field is full. */
  s->z = spread_depth ? (Z_NEAR + frand() * (1.0f - Z_NEAR))
                      : (0.6f + frand() * 0.4f);
  s->has_prev = 0;
}

static int project(const Star *s, int *sx, int *sy)
{
  float k = PROJ / s->z;
  int X = CX + (int)(s->x * k);
  int Y = CY + (int)(s->y * k);
  *sx = X; *sy = Y;
  return (X >= 0 && X < 128 && Y >= 0 && Y < 128);
}

void boot_splash(UBYTE *image, uint32_t duration_ms)
{
  s_rng ^= HAL_GetTick() * 2654435761u;
  for (int i = 0; i < NSTARS; i++) respawn(&s_star[i], 1);

  uint32_t t0 = HAL_GetTick();
  uint32_t last = t0;

  for (;;) {
    uint32_t now = HAL_GetTick();
    uint32_t el = now - t0;
    if (el >= duration_ms) break;

    float dt = (float)(now - last) / 1000.0f;
    last = now;
    if (dt > 0.1f) dt = 0.1f;

    /* Accelerate over time: gentle drift -> warp. */
    float t = (float)el / 1000.0f;
    float speed = 0.25f + t * t * 0.20f;

    Paint_Clear(0);

    for (int i = 0; i < NSTARS; i++) {
      Star *s = &s_star[i];
      s->z -= speed * dt;
      if (s->z < Z_NEAR) { respawn(s, 0); }

      int sx, sy;
      if (!project(s, &sx, &sy)) { respawn(s, 0); continue; }

      uint8_t lvl = (uint8_t)((1.0f - s->z) * 15.0f);
      if (lvl < 2) lvl = 2;

      if (s->has_prev) {
        Paint_DrawLine((UWORD)s->psx, (UWORD)s->psy, (UWORD)sx, (UWORD)sy,
                       lvl, DOT_PIXEL_1X1, LINE_STYLE_SOLID);
      }
      Paint_SetPixel((UWORD)sx, (UWORD)sy, 15);   /* bright head */

      s->psx = sx; s->psy = sy; s->has_prev = 1;
    }

    OLED_1in5_Display(image);
  }

  /* Hit light speed: a white-out flash that fades to black as the menu takes
   * over. */
  Paint_Clear(15);
  OLED_1in5_Display(image);
  HAL_Delay(60);
  for (int lvl = 15; lvl >= 0; lvl -= 2) {
    Paint_Clear((UWORD)lvl);
    OLED_1in5_Display(image);
    HAL_Delay(18);
  }
}
