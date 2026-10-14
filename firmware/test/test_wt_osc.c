#include "unity.h"
/* Include the renderer to inspect pending voice ownership as well as output. */
#include "../Core/IO/wt_osc.c"

test_dwt_t test_dwt;
test_debug_t test_debug;
static audio_render_fn render;
void audio_set_render(audio_render_fn fn, void *ctx) { (void)ctx; render = fn; }
static int32_t output[AUDIO_BLOCK_FRAMES * 2];
static const int16_t positive[] = {10000, 10000};
static const int16_t negative[] = {-10000, -10000};
static multisample_t tables;

void setUp(void)
{
  wt_osc_init();
  wt_osc_set_filter_enabled(false);
  tables.zone_count = 2;
  tables.zones[0] = (ms_zone_t){ .key_low=0, .key_high=63, .key_root=60,
    .frames=2, .samples=positive };
  tables.zones[1] = (ms_zone_t){ .key_low=64, .key_high=127, .key_root=72,
    .frames=2, .samples=negative };
  wt_osc_set_multisample(&tables);
  wt_osc_install();
}
void tearDown(void) {}
static void block(void) { render(output, AUDIO_BLOCK_FRAMES, NULL); }
static void blocks(int n) { while (n--) block(); }

static void test_retrigger_fades_through_zero(void)
{
  wt_osc_note(60, 100);
  block();
  int32_t before = output[254];
  wt_osc_note(72, 200);
  block();
  TEST_ASSERT_EQUAL_INT32(before, output[0]);
  TEST_ASSERT_TRUE(output[254] < output[0]);
  blocks(7); /* 1024 frames: old voice has reached exact silence. */
  TEST_ASSERT_EQUAL_INT32(0, output[254]);
  block();
  TEST_ASSERT_EQUAL_INT32(0, output[0]);
  TEST_ASSERT_TRUE(output[254] < 0);
  TEST_ASSERT_EQUAL_PTR(&tables.zones[1], s_voice[0].zone);
}

static void test_full_polyphony_steals_oldest_and_ignores_old_note_off(void)
{
  for (int i=0; i<4; ++i) wt_osc_note_on(60+i, 100, 100);
  block();
  wt_osc_note_on(72, 200, 100);
  TEST_ASSERT_TRUE(s_voice[0].pending);
  wt_osc_note_off(60);
  TEST_ASSERT_TRUE(s_voice[0].pending);
  blocks(9);
  TEST_ASSERT_EQUAL_PTR(&tables.zones[1], s_voice[0].zone);
  TEST_ASSERT_EQUAL_INT(4, s_voice[0].active+s_voice[1].active+
                              s_voice[2].active+s_voice[3].active);
}

static void test_note_off_cancels_pending_replacement(void)
{
  wt_osc_set_filter_enabled(true);
  wt_osc_note_on(60, 100, 100);
  blocks(20);
  wt_osc_note_on(60, 200, 100);
  wt_osc_note_off(60);
  blocks(10);
  TEST_ASSERT_FALSE(s_voice[0].active);
  TEST_ASSERT_FALSE(s_voice[0].pending);
}

static void test_repeated_requests_do_not_restart_fade(void)
{
  wt_osc_note(60, 100);
  block();
  wt_osc_note(70, 200);
  for (int i=0; i<8; ++i) { block(); wt_osc_note(72, 300); }
  block();
  TEST_ASSERT_FALSE(s_voice[0].pending);
  TEST_ASSERT_EQUAL_INT(72, s_voice[0].midi);
  TEST_ASSERT_FLOAT_WITHIN(0.000001f, 600.0f/AUDIO_FS_HZ, s_voice[0].phase_inc);
}

static void test_table_change_and_panic_cancel_pending(void)
{
  wt_osc_note(60, 100);
  wt_osc_note(72, 200);
  wt_osc_set_multisample(NULL);
  blocks(10);
  TEST_ASSERT_EQUAL_INT32(0, output[0]);
  TEST_ASSERT_FALSE(s_voice[0].pending);
  wt_osc_set_multisample(&tables);
  wt_osc_note(60, 100);
  wt_osc_note(72, 200);
  wt_osc_all_off();
  blocks(10);
  TEST_ASSERT_EQUAL_INT32(0, output[0]);
}

static void test_legato_does_not_schedule_a_fade(void)
{
  wt_osc_set_pitch(60, 100);
  block();
  float phase = s_voice[0].phase;
  wt_osc_set_pitch(61, 110);
  TEST_ASSERT_FALSE(s_voice[0].retiring);
  TEST_ASSERT_EQUAL_FLOAT(phase, s_voice[0].phase);
}

static void test_pending_note_is_not_a_releasing_voice_candidate(void)
{
  wt_osc_set_filter_enabled(true);
  for (int i=0; i<4; ++i) wt_osc_note_on(60+i, 100, 100);
  blocks(20);
  wt_osc_note_off(60);
  wt_osc_note_on(72, 200, 100);
  wt_osc_note_on(73, 210, 100);
  TEST_ASSERT_EQUAL_INT(72, s_voice[0].midi);
  TEST_ASSERT_EQUAL_INT(73, s_voice[1].midi);
}

static void test_chord_gate_off_cancels_all_replacements(void)
{
  wt_osc_set_filter_enabled(true);
  int notes[] = {60, 61, 62, 63};
  float hz[] = {100, 110, 120, 130};
  wt_osc_chord(notes, hz, 4);
  blocks(20);
  wt_osc_chord(notes, hz, 4);
  wt_osc_gate_off();
  blocks(10);
  for (int i=0; i<4; ++i) TEST_ASSERT_FALSE(s_voice[i].active);
}

int main(void)
{
  UNITY_BEGIN();
  RUN_TEST(test_retrigger_fades_through_zero);
  RUN_TEST(test_full_polyphony_steals_oldest_and_ignores_old_note_off);
  RUN_TEST(test_note_off_cancels_pending_replacement);
  RUN_TEST(test_repeated_requests_do_not_restart_fade);
  RUN_TEST(test_table_change_and_panic_cancel_pending);
  RUN_TEST(test_legato_does_not_schedule_a_fade);
  RUN_TEST(test_pending_note_is_not_a_releasing_voice_candidate);
  RUN_TEST(test_chord_gate_off_cancels_all_replacements);
  return UNITY_END();
}
