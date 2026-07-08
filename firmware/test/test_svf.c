#include "unity.h"
#include "svf.h"
#include <math.h>

#define FS 192000.0f

static svf_t S;

void setUp(void)    { svf_reset(&S); }
void tearDown(void) {}

/* Steady-state amplitude of a sine at `hz` through coefficients `c`:
 * warm up, then peak-track over several cycles. */
static float sine_amp(const svf_coef_t *c, float hz)
{
    const float w = 2.0f * 3.14159265f * hz / FS;
    int warm = (int)(FS * 0.1f);              /* 100 ms settle */
    int meas = (int)(4.0f * FS / hz);          /* >= 4 cycles */
    float peak = 0.0f;
    for (int i = 0; i < warm + meas; i++) {
        float y = svf_lp(&S, c, sinf(w * (float)i));
        if (i >= warm && fabsf(y) > peak) peak = fabsf(y);
    }
    return peak;
}

/* ---------------- frequency response ---------------- */

static void test_dc_passes_unchanged(void)
{
    static const float fcs[] = { 50.0f, 400.0f, 5000.0f };
    static const float rss[] = { 0.0f, 0.5f };
    for (unsigned f = 0; f < 3; f++)
        for (unsigned r = 0; r < 2; r++) {
            svf_coef_t c;
            svf_coef(&c, fcs[f], rss[r], FS);
            svf_reset(&S);
            float y = 0.0f;
            for (int i = 0; i < 40000; i++) y = svf_lp(&S, &c, 0.5f);
            TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.5f, y);
        }
}

static void test_attenuates_well_above_cutoff(void)
{
    svf_coef_t c;
    svf_coef(&c, 1000.0f, 0.15f, FS);
    /* 8x the cutoff through a 2-pole lowpass: at least -20 dB. */
    TEST_ASSERT_LESS_THAN_FLOAT(0.1f, sine_amp(&c, 8000.0f));
}

static void test_passband_is_unity(void)
{
    svf_coef_t c;
    svf_coef(&c, 2000.0f, 0.15f, FS);
    float a = sine_amp(&c, 200.0f);            /* a decade below cutoff */
    TEST_ASSERT_TRUE_MESSAGE(a > 0.9f && a < 1.1f, "passband not ~unity");
}

static void test_resonance_boost_is_monotonic(void)
{
    float a[3];
    static const float rss[3] = { 0.0f, 0.6f, 0.9f };
    for (int i = 0; i < 3; i++) {
        svf_coef_t c;
        svf_coef(&c, 1000.0f, rss[i], FS);
        svf_reset(&S);
        a[i] = sine_amp(&c, 1000.0f);
    }
    TEST_ASSERT_LESS_THAN_FLOAT(a[1], a[0]);
    TEST_ASSERT_LESS_THAN_FLOAT(a[2], a[1]);
}

/* ---------------- stability ---------------- */

/* Excite hard, then feed silence: output must ring down, never blow up. */
static void test_silence_decays_over_full_param_grid(void)
{
    static const float fcs[] = { 20.0f, 100.0f, 1000.0f, 10000.0f, 20000.0f };
    static const float rss[] = { 0.0f, 0.5f, 0.97f };
    for (unsigned f = 0; f < 5; f++)
        for (unsigned r = 0; r < 3; r++) {
            svf_coef_t c;
            svf_coef(&c, fcs[f], rss[r], FS);
            svf_reset(&S);
            for (int i = 0; i < 2000; i++)                   /* excitation */
                svf_lp(&S, &c, (i & 1) ? 1.0f : -1.0f);
            float y = 1.0f;
            for (int i = 0; i < (int)(2.0f * FS); i++) {      /* 2 s silence */
                y = svf_lp(&S, &c, 0.0f);
                TEST_ASSERT_TRUE_MESSAGE(isfinite(y), "output not finite");
            }
            TEST_ASSERT_LESS_THAN_FLOAT(1e-3f, fabsf(y));
        }
}

/* The property TPT buys us: full-range per-block cutoff jumps under high
 * resonance stay bounded — this is exactly what a fast envelope sweep does. */
static void test_per_block_cutoff_jumps_stay_bounded(void)
{
    svf_coef_t lo, hi;
    svf_coef(&lo, 20.0f, 0.9f, FS);
    svf_coef(&hi, 20000.0f, 0.9f, FS);
    const float w = 2.0f * 3.14159265f * 500.0f / FS;

    for (int i = 0; i < 96000; i++) {
        const svf_coef_t *c = ((i / 128) & 1) ? &hi : &lo;   /* jump per block */
        float y = svf_lp(&S, c, sinf(w * (float)i));
        TEST_ASSERT_TRUE_MESSAGE(isfinite(y), "output not finite");
        TEST_ASSERT_TRUE_MESSAGE(fabsf(y) < 10.0f, "output unbounded");
    }

    svf_coef_t c;                       /* and it recovers to silence */
    svf_coef(&c, 1000.0f, 0.5f, FS);
    float y = 1.0f;
    for (int i = 0; i < 96000; i++) y = svf_lp(&S, &c, 0.0f);
    TEST_ASSERT_LESS_THAN_FLOAT(1e-2f, fabsf(y));
}

static void test_reset_clears_state(void)
{
    svf_coef_t c;
    svf_coef(&c, 500.0f, 0.5f, FS);
    for (int i = 0; i < 1000; i++) svf_lp(&S, &c, 1.0f);
    svf_reset(&S);
    TEST_ASSERT_EQUAL_FLOAT(0.0f, svf_lp(&S, &c, 0.0f));
}

/* Out-of-range requests are clamped, not honored into instability. */
static void test_extreme_params_are_clamped(void)
{
    svf_coef_t c;
    svf_coef(&c, 1e9f, 5.0f, FS);       /* absurd cutoff + resonance */
    svf_reset(&S);
    float y = 0.0f;
    for (int i = 0; i < 40000; i++) {
        y = svf_lp(&S, &c, 0.5f);
        TEST_ASSERT_TRUE(isfinite(y));
    }
    TEST_ASSERT_FLOAT_WITHIN(0.02f, 0.5f, y);   /* still passes DC */

    svf_coef(&c, -100.0f, -1.0f, FS);   /* below range */
    svf_reset(&S);
    for (int i = 0; i < 40000; i++) {
        y = svf_lp(&S, &c, 0.5f);
        TEST_ASSERT_TRUE(isfinite(y));
    }
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_dc_passes_unchanged);
    RUN_TEST(test_attenuates_well_above_cutoff);
    RUN_TEST(test_passband_is_unity);
    RUN_TEST(test_resonance_boost_is_monotonic);
    RUN_TEST(test_silence_decays_over_full_param_grid);
    RUN_TEST(test_per_block_cutoff_jumps_stay_bounded);
    RUN_TEST(test_reset_clears_state);
    RUN_TEST(test_extreme_params_are_clamped);
    return UNITY_END();
}
