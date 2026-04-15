#ifndef CIRCLE_OF_FIFTHS_H
#define CIRCLE_OF_FIFTHS_H

#include <stdint.h>

// Note indices
#define COF_C   0
#define COF_G   1
#define COF_D   2
#define COF_A   3
#define COF_E   4
#define COF_B   5
#define COF_Fs  6
#define COF_Db  7
#define COF_Ab  8
#define COF_Eb  9
#define COF_Bb  10
#define COF_F   11

// Render the circle of fifths zoomed view at the given rotation angle
// (in degrees). 0 = C at top, 30 = G at top, etc.
// `marked[i] != 0` draws note i with a black halo border around the text.
// Pass NULL for no marks.
// Draws into the currently selected Paint image via Paint_SetPixel().
// The Paint image must be initialised with Scale=16 before calling.
void cof_render_angle(float angle_deg, const uint8_t *marked);

#endif
