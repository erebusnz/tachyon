#include "unity.h"
#include "arp.h"
#include <string.h>

/* ---- step-callback capture ---- */
#define CAP_MAX 64
static int      cap_note[CAP_MAX];
static uint32_t cap_gate[CAP_MAX];
static int      cap_n;

static void on_step(int semitone, uint32_t gate_ms, void *ctx)
{
    (void)ctx;
    if (cap_n < CAP_MAX) {
        cap_note[cap_n] = semitone;
        cap_gate[cap_n] = gate_ms;
        cap_n++;
    }
}

static void cap_reset(void) { cap_n = 0; memset(cap_note, 0, sizeof cap_note); }

void setUp(void)
{
    arp_init(on_step, NULL);
    cap_reset();
}

void tearDown(void) {}

/* Fire `n` external clock pulses (one step each) at a fixed cadence. */
static void pulse_n(int n, uint32_t step_ms)
{
    uint32_t t = 1000;
    for (int i = 0; i < n; i++) { arp_clock_pulse(t); t += step_ms; }
}

/* ---------------- sequence generation ---------------- */

static void setup_chord_major(uint8_t octaves, arp_dir_t dir)
{
    static const uint8_t maj[3] = {0, 4, 7};
    arp_set_clock(ARP_CLK_EXTERNAL);
    arp_set_chord(maj, 3);
    arp_set_octaves(octaves);
    arp_set_dir(dir);
    arp_set_length(64);          /* long enough not to wrap during a single octave sweep */
    arp_set_enabled(true);
}

static void test_up_single_octave(void)
{
    setup_chord_major(1, ARP_UP);
    pulse_n(3, 100);
    TEST_ASSERT_EQUAL_INT(3, cap_n);
    TEST_ASSERT_EQUAL_INT(0, cap_note[0]);
    TEST_ASSERT_EQUAL_INT(4, cap_note[1]);
    TEST_ASSERT_EQUAL_INT(7, cap_note[2]);
}

static void test_down_single_octave(void)
{
    setup_chord_major(1, ARP_DOWN);
    pulse_n(3, 100);
    TEST_ASSERT_EQUAL_INT(7, cap_note[0]);
    TEST_ASSERT_EQUAL_INT(4, cap_note[1]);
    TEST_ASSERT_EQUAL_INT(0, cap_note[2]);
}

static void test_updown_no_repeated_endpoints(void)
{
    /* n=3 -> up 0,4,7 then down 4 (endpoints not repeated): len = 2n-2 = 4 */
    setup_chord_major(1, ARP_UPDOWN);
    pulse_n(4, 100);
    const int expect[4] = {0, 4, 7, 4};
    for (int i = 0; i < 4; i++) TEST_ASSERT_EQUAL_INT(expect[i], cap_note[i]);
}

static void test_two_octaves_stack(void)
{
    setup_chord_major(2, ARP_UP);
    pulse_n(6, 100);
    const int expect[6] = {0, 4, 7, 12, 16, 19};
    for (int i = 0; i < 6; i++) TEST_ASSERT_EQUAL_INT(expect[i], cap_note[i]);
}

static void test_length_wraps_sequence(void)
{
    setup_chord_major(1, ARP_UP);
    arp_set_length(2);           /* pattern loops every 2 steps */
    pulse_n(4, 100);
    /* step counter wraps at length 2: notes at seq[0], seq[1], seq[0], seq[1] */
    TEST_ASSERT_EQUAL_INT(0, cap_note[0]);
    TEST_ASSERT_EQUAL_INT(4, cap_note[1]);
    TEST_ASSERT_EQUAL_INT(0, cap_note[2]);
    TEST_ASSERT_EQUAL_INT(4, cap_note[3]);
}

static void test_random_stays_in_set(void)
{
    setup_chord_major(2, ARP_RANDOM);   /* set = {0,4,7,12,16,19} */
    pulse_n(40, 100);
    TEST_ASSERT_EQUAL_INT(40, cap_n);
    for (int i = 0; i < cap_n; i++) {
        int v = cap_note[i];
        bool ok = (v == 0 || v == 4 || v == 7 || v == 12 || v == 16 || v == 19);
        TEST_ASSERT_TRUE_MESSAGE(ok, "random note outside chord/octave set");
    }
}

/* ---------------- parameter clamping ---------------- */

static void test_bpm_clamped(void)
{
    arp_set_bpm(10);   TEST_ASSERT_EQUAL_UINT16(40,  arp_bpm());
    arp_set_bpm(999);  TEST_ASSERT_EQUAL_UINT16(300, arp_bpm());
    arp_set_bpm(128);  TEST_ASSERT_EQUAL_UINT16(128, arp_bpm());
}

static void test_octaves_clamped(void)
{
    arp_set_octaves(0); TEST_ASSERT_EQUAL_UINT8(1, arp_octaves());
    arp_set_octaves(9); TEST_ASSERT_EQUAL_UINT8(ARP_MAX_OCT, arp_octaves());
}

static void test_length_zero_becomes_one(void)
{
    arp_set_length(0);
    TEST_ASSERT_EQUAL_UINT8(1, arp_length());
}

static void test_invalid_dir_falls_back_up(void)
{
    arp_set_dir((arp_dir_t)99);
    TEST_ASSERT_EQUAL_INT(ARP_UP, arp_dir());
}

static void test_chord_count_capped(void)
{
    uint8_t big[8] = {0, 1, 2, 3, 4, 5, 6, 7};
    arp_set_clock(ARP_CLK_EXTERNAL);
    arp_set_dir(ARP_UP);
    arp_set_octaves(1);
    arp_set_length(64);
    arp_set_chord(big, 8);       /* capped to ARP_MAX_CHORD = 6 */
    arp_set_enabled(true);
    pulse_n(10, 100);
    /* chord capped to ARP_MAX_CHORD (6): notes are offsets 0..5 */
    TEST_ASSERT_EQUAL_INT(0, cap_note[0]);
    TEST_ASSERT_EQUAL_INT(5, cap_note[5]);
}

/* ---------------- gating: enable / clock source ---------------- */

static void test_disabled_emits_nothing(void)
{
    static const uint8_t maj[3] = {0, 4, 7};
    arp_set_clock(ARP_CLK_EXTERNAL);
    arp_set_chord(maj, 3);
    arp_set_enabled(false);
    pulse_n(5, 100);
    TEST_ASSERT_EQUAL_INT(0, cap_n);
}

static void test_external_pulse_ignored_when_internal(void)
{
    static const uint8_t maj[3] = {0, 4, 7};
    arp_set_chord(maj, 3);
    arp_set_clock(ARP_CLK_INTERNAL);
    arp_set_enabled(true);
    pulse_n(5, 100);             /* external pulses do nothing on internal clock */
    TEST_ASSERT_EQUAL_INT(0, cap_n);
}

static void test_internal_clock_fires_on_interval(void)
{
    static const uint8_t maj[3] = {0, 4, 7};
    arp_set_chord(maj, 3);
    arp_set_clock(ARP_CLK_INTERNAL);
    arp_set_bpm(120);            /* 60000/120 = 500 ms per step */
    arp_set_enabled(true);

    uint32_t t = 10000;
    arp_tick(t);                 /* restart: fires immediately */
    TEST_ASSERT_EQUAL_INT(1, cap_n);

    arp_tick(t + 200);           /* not yet due */
    TEST_ASSERT_EQUAL_INT(1, cap_n);

    arp_tick(t + 500);           /* due */
    TEST_ASSERT_EQUAL_INT(2, cap_n);

    arp_tick(t + 1000);          /* due again */
    TEST_ASSERT_EQUAL_INT(3, cap_n);
}

static void test_gate_length_bounds(void)
{
    setup_chord_major(1, ARP_UP);
    pulse_n(3, 100);             /* 100 ms cadence -> derived gate ~ dt/2 = 50 ms */
    /* first step has no previous: gate from bpm fallback; later steps dt/2 */
    TEST_ASSERT_GREATER_OR_EQUAL_UINT32(10u, cap_gate[1]);
    TEST_ASSERT_LESS_OR_EQUAL_UINT32(2000u, cap_gate[1]);
    TEST_ASSERT_EQUAL_UINT32(50u, cap_gate[2]);
}

static void test_updown_seq_wraps_at_correct_period(void)
{
    /* Chord starting at non-zero semitone (1) so the phantom s_seq[4] element
     * (uninitialized / stale, almost certainly 0) differs from note[0]=1.
     * Correct seq_len=4 wraps step 4 back to s_seq[0]=1.
     * Off-by-one mutant seq_len=5 plays s_seq[4] on the 5th step instead. */
    static const uint8_t chord[3] = {1, 5, 8};
    arp_set_clock(ARP_CLK_EXTERNAL);
    arp_set_chord(chord, 3);
    arp_set_octaves(1);
    arp_set_dir(ARP_UPDOWN);
    arp_set_length(64);
    arp_set_enabled(true);
    pulse_n(5, 100);
    /* seq = [1, 5, 8, 5], len=4; 5th step wraps to seq[0]=1 */
    TEST_ASSERT_EQUAL_INT(1, cap_note[0]);
    TEST_ASSERT_EQUAL_INT(5, cap_note[1]);
    TEST_ASSERT_EQUAL_INT(8, cap_note[2]);
    TEST_ASSERT_EQUAL_INT(5, cap_note[3]);
    TEST_ASSERT_EQUAL_INT(1, cap_note[4]);
}

static void test_fallback_gate_is_half_interval(void)
{
    /* First step has no previous timestamp so gate uses the BPM fallback:
     * 30000/bpm (~half the nominal step interval).
     * The doubling mutant uses 60000/bpm, giving 500 ms instead of 250 ms. */
    setup_chord_major(1, ARP_UP);
    arp_set_bpm(120);   /* 30000/120 = 250 ms; mutant gives 60000/120 = 500 ms */
    pulse_n(1, 100);
    TEST_ASSERT_EQUAL_UINT32(250u, cap_gate[0]);
}

static void test_gate_clamped_at_2000ms(void)
{
    /* 5000 ms cadence -> derived gate = dt/2 = 2500 ms.
     * Correct code clamps the gate ceiling to 2000; a raised-ceiling mutant
     * (e.g. 3000) would let 2500 pass through. */
    setup_chord_major(1, ARP_UP);
    pulse_n(3, 5000);
    TEST_ASSERT_EQUAL_UINT32(2000u, cap_gate[1]);
}

static void test_internal_clock_interval_bpm60(void)
{
    /* BPM=60: correct interval = 60000/60 = 1000 ms.
     * A halved-numerator mutant (30000/bpm) gives 500 ms and fires a step
     * too early -- the +500 ms tick must NOT fire under correct code. */
    static const uint8_t maj[3] = {0, 4, 7};
    arp_set_chord(maj, 3);
    arp_set_clock(ARP_CLK_INTERNAL);
    arp_set_bpm(60);
    arp_set_enabled(true);

    uint32_t t = 10000;
    arp_tick(t);            /* restart: fires immediately */
    TEST_ASSERT_EQUAL_INT(1, cap_n);

    arp_tick(t + 500);      /* half-interval: must NOT fire */
    TEST_ASSERT_EQUAL_INT(1, cap_n);

    arp_tick(t + 1000);     /* full interval: fires */
    TEST_ASSERT_EQUAL_INT(2, cap_n);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_gate_clamped_at_2000ms);
    RUN_TEST(test_internal_clock_interval_bpm60);
    RUN_TEST(test_fallback_gate_is_half_interval);
    RUN_TEST(test_updown_seq_wraps_at_correct_period);
    RUN_TEST(test_up_single_octave);
    RUN_TEST(test_down_single_octave);
    RUN_TEST(test_updown_no_repeated_endpoints);
    RUN_TEST(test_two_octaves_stack);
    RUN_TEST(test_length_wraps_sequence);
    RUN_TEST(test_random_stays_in_set);
    RUN_TEST(test_bpm_clamped);
    RUN_TEST(test_octaves_clamped);
    RUN_TEST(test_length_zero_becomes_one);
    RUN_TEST(test_invalid_dir_falls_back_up);
    RUN_TEST(test_chord_count_capped);
    RUN_TEST(test_disabled_emits_nothing);
    RUN_TEST(test_external_pulse_ignored_when_internal);
    RUN_TEST(test_internal_clock_fires_on_interval);
    RUN_TEST(test_gate_length_bounds);
    return UNITY_END();
}
