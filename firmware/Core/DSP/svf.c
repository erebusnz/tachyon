#include "svf.h"
#include <math.h>

#define SVF_FC_MIN   20.0f
#define SVF_FC_MAX   20000.0f
#define SVF_RES_MAX  0.97f      /* k >= 0.06: stable, no self-oscillation */

void svf_coef(svf_coef_t *c, float fc_hz, float res, float fs_hz)
{
    float fc_lim = 0.45f * fs_hz;
    if (fc_lim > SVF_FC_MAX) fc_lim = SVF_FC_MAX;
    if (fc_hz < SVF_FC_MIN) fc_hz = SVF_FC_MIN;
    if (fc_hz > fc_lim)     fc_hz = fc_lim;
    if (res < 0.0f)         res = 0.0f;
    if (res > SVF_RES_MAX)  res = SVF_RES_MAX;

    float g = tanf(3.14159265f * fc_hz / fs_hz);
    float k = 2.0f - 2.0f * res;
    c->a1 = 1.0f / (1.0f + g * (g + k));
    c->a2 = g * c->a1;
    c->a3 = g * c->a2;
}

void svf_reset(svf_t *s)
{
    s->ic1eq = 0.0f;
    s->ic2eq = 0.0f;
}
