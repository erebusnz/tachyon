#include "unity.h"
#include "adsr.h"
#include <math.h>

/* The firmware ticks the envelope once per 128-frame audio block at
 * ~192 kHz: 191964.29 / 128 ≈ 1499.7 Hz. */
#define TICK_HZ 1499.72f

static adsr_params_t P;
static adsr_t E;

static int ticks_for_ms(float ms) { return (int)(ms * 0.001f * TICK_HZ + 0.5f); }

void setUp(void)
{
    adsr_init(&E);
    adsr_config(&P, 10.0f, 100.0f, 0.5f, 100.0f, TICK_HZ);
}

void tearDown(void) {}

/* Step until the attack completes (level reaches 1.0); returns tick count. */
static int run_attack(int max_ticks)
{
    for (int i = 1; i <= max_ticks; i++)
        if (adsr_step(&E, &P) >= 1.0f - 1e-3f) return i;
    return -1;
}

/* ---------------- stages ---------------- */

static void test_idle_outputs_zero(void)
{
    for (int i = 0; i < 100; i++)
        TEST_ASSERT_EQUAL_FLOAT(0.0f, adsr_step(&E, &P));
    TEST_ASSERT_TRUE(adsr_idle(&E));
}

static void test_attack_reaches_full_within_time(void)
{
    adsr_gate_on(&E);
    int expect = ticks_for_ms(10.0f);
    int got = run_attack(expect * 2);
    TEST_ASSERT_TRUE_MESSAGE(got > 0, "attack never completed");
    /* Within +10 % of the configured time, and not instantaneous. */
    TEST_ASSERT_LESS_OR_EQUAL_INT(expect + expect / 10 + 1, got);
    TEST_ASSERT_GREATER_OR_EQUAL_INT(expect / 2, got);
}

static void test_zero_attack_completes_in_one_tick(void)
{
    adsr_config(&P, 0.0f, 100.0f, 0.5f, 100.0f, TICK_HZ);
    adsr_gate_on(&E);
    float v = adsr_step(&E, &P);
    TEST_ASSERT_FALSE(isnan(v));
    TEST_ASSERT_EQUAL_FLOAT(1.0f, v);
}

static void test_decay_settles_to_sustain(void)
{
    adsr_config(&P, 0.0f, 100.0f, 0.5f, 100.0f, TICK_HZ);
    adsr_gate_on(&E);
    TEST_ASSERT_EQUAL_INT(1, run_attack(4));
    float v = 0.0f;
    for (int i = 0; i < ticks_for_ms(100.0f); i++) v = adsr_step(&E, &P);
    /* Nominal time = 3 time constants -> within ~6 % of the move. */
    TEST_ASSERT_FLOAT_WITHIN(0.06f * 0.5f + 1e-3f, 0.5f, v);
}

static void test_sustain_holds_and_tracks_live_edits(void)
{
    adsr_config(&P, 0.0f, 20.0f, 0.5f, 100.0f, TICK_HZ);
    adsr_gate_on(&E);
    float v = 0.0f;
    for (int i = 0; i < 500; i++) v = adsr_step(&E, &P);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.5f, v);

    /* Live sustain edit on a held note is followed. */
    P.sustain = 0.8f;
    for (int i = 0; i < 500; i++) v = adsr_step(&E, &P);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.8f, v);
}

static void test_release_decays_and_idles(void)
{
    adsr_config(&P, 0.0f, 20.0f, 0.5f, 100.0f, TICK_HZ);
    adsr_gate_on(&E);
    for (int i = 0; i < 500; i++) adsr_step(&E, &P);   /* settle at sustain */

    adsr_gate_off(&E);
    float v = 0.5f;
    for (int i = 0; i < ticks_for_ms(100.0f); i++) v = adsr_step(&E, &P);
    /* Most of the way down within the nominal release time... */
    TEST_ASSERT_LESS_THAN_FLOAT(0.06f * 0.5f + 1e-3f, v);
    /* ...and fully idle (level 0) well before 4x. */
    for (int i = 0; i < ticks_for_ms(300.0f); i++) v = adsr_step(&E, &P);
    TEST_ASSERT_TRUE(adsr_idle(&E));
    TEST_ASSERT_EQUAL_FLOAT(0.0f, v);
}

/* ---------------- retrigger / kill ---------------- */

static void test_retrigger_attacks_from_current_level(void)
{
    adsr_config(&P, 10.0f, 20.0f, 0.6f, 200.0f, TICK_HZ);
    adsr_gate_on(&E);
    for (int i = 0; i < 500; i++) adsr_step(&E, &P);
    adsr_gate_off(&E);
    float mid = 0.6f;
    for (int i = 0; i < ticks_for_ms(60.0f); i++) mid = adsr_step(&E, &P);
    TEST_ASSERT_TRUE(mid > 0.05f && mid < 0.6f);   /* mid-release */

    adsr_gate_on(&E);
    float v = adsr_step(&E, &P);
    /* Rises from the release level — no snap to zero, no jump. */
    TEST_ASSERT_GREATER_OR_EQUAL_FLOAT(mid, v);
    TEST_ASSERT_LESS_THAN_FLOAT(mid + 0.25f, v);
}

static void test_kill_is_fast_but_not_a_hard_cut(void)
{
    adsr_config(&P, 0.0f, 20.0f, 0.9f, 500.0f, TICK_HZ);
    adsr_gate_on(&E);
    for (int i = 0; i < 500; i++) adsr_step(&E, &P);

    adsr_kill(&E);
    float v = adsr_step(&E, &P);
    TEST_ASSERT_GREATER_THAN_FLOAT(0.1f, v);       /* a fade, not a cut */
    for (int i = 0; i < 40; i++) v = adsr_step(&E, &P);
    TEST_ASSERT_TRUE(adsr_idle(&E));               /* gone within ~25 ms */
}

static void test_gate_off_when_idle_stays_idle(void)
{
    adsr_gate_off(&E);
    adsr_kill(&E);
    TEST_ASSERT_TRUE(adsr_idle(&E));
    TEST_ASSERT_EQUAL_FLOAT(0.0f, adsr_step(&E, &P));
}

/* ---------------- global properties ---------------- */

static void test_output_always_in_bounds(void)
{
    static const float cfg[][4] = {
        { 0.0f, 5.0f, 0.0f, 5.0f },
        { 1.0f, 5000.0f, 1.0f, 10000.0f },
        { 5000.0f, 0.0f, 0.3f, 0.0f },
    };
    for (unsigned c = 0; c < sizeof cfg / sizeof cfg[0]; c++) {
        adsr_init(&E);
        adsr_config(&P, cfg[c][0], cfg[c][1], cfg[c][2], cfg[c][3], TICK_HZ);
        adsr_gate_on(&E);
        for (int i = 0; i < 3000; i++) {
            if (i == 2000) adsr_gate_off(&E);
            float v = adsr_step(&E, &P);
            TEST_ASSERT_TRUE_MESSAGE(v >= 0.0f && v <= 1.0f + 1e-6f,
                                     "level out of [0,1]");
            TEST_ASSERT_FALSE(isnan(v));
        }
    }
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_idle_outputs_zero);
    RUN_TEST(test_attack_reaches_full_within_time);
    RUN_TEST(test_zero_attack_completes_in_one_tick);
    RUN_TEST(test_decay_settles_to_sustain);
    RUN_TEST(test_sustain_holds_and_tracks_live_edits);
    RUN_TEST(test_release_decays_and_idles);
    RUN_TEST(test_retrigger_attacks_from_current_level);
    RUN_TEST(test_kill_is_fast_but_not_a_hard_cut);
    RUN_TEST(test_gate_off_when_idle_stays_idle);
    RUN_TEST(test_output_always_in_bounds);
    return UNITY_END();
}
