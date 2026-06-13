#include "arp.h"

/* ---- configuration ---- */
static arp_step_fn s_cb;
static void       *s_ctx;

static uint8_t   s_chord[ARP_MAX_CHORD];
static uint8_t   s_chord_n;
static arp_dir_t s_dir;
static uint8_t   s_octaves = 1;
static uint8_t   s_length  = 8;
static arp_clk_t s_clk;
static uint16_t  s_bpm     = 120;
static bool      s_enabled;

/* ---- generated sequence (direction-ordered note list) ---- */
static int      s_seq[ARP_MAX_SEQ];
static uint8_t  s_seq_len;

/* ---- runtime ---- */
static uint8_t  s_step;          /* step counter, wraps at s_length */
static uint32_t s_next_ms;       /* internal clock: tick of next step */
static uint32_t s_prev_step_ms;  /* for gate-length derivation */
static uint8_t  s_have_prev;
static uint8_t  s_restart;       /* fire a fresh step on the next clock */
static uint32_t s_rng = 0xA5A5A5A5u;
static uint8_t  s_seeded;

static uint32_t rng_next(void)
{
  uint32_t x = s_rng;
  x ^= x << 13; x ^= x >> 17; x ^= x << 5;
  s_rng = x;
  return x;
}

/* Rebuild the direction-ordered note list from chord x octaves. */
static void rebuild_seq(void)
{
  int base[ARP_MAX_NOTES];
  int n = 0;
  for (uint8_t o = 0; o < s_octaves; o++)
    for (uint8_t c = 0; c < s_chord_n; c++)
      base[n++] = (int)s_chord[c] + 12 * (int)o;

  if (n == 0) { s_seq_len = 0; s_step = 0; return; }

  switch (s_dir) {
  case ARP_DOWN:
    for (int i = 0; i < n; i++) s_seq[i] = base[n - 1 - i];
    s_seq_len = (uint8_t)n;
    break;
  case ARP_UPDOWN:
    if (n > 1) {
      int k = 0;
      for (int i = 0; i < n; i++)       s_seq[k++] = base[i];        /* up */
      for (int i = n - 2; i >= 1; i--)  s_seq[k++] = base[i];        /* down, no endpoints */
      s_seq_len = (uint8_t)(2 * n - 2);
    } else {
      s_seq[0] = base[0];
      s_seq_len = 1;
    }
    break;
  case ARP_UP:
  case ARP_RANDOM:   /* RANDOM picks a random index into the ascending list */
  default:
    for (int i = 0; i < n; i++) s_seq[i] = base[i];
    s_seq_len = (uint8_t)n;
    break;
  }

  s_step = 0;
}

static void do_step(uint32_t now)
{
  if (!s_cb || s_seq_len == 0) return;

  int note = (s_dir == ARP_RANDOM)
             ? s_seq[rng_next() % s_seq_len]
             : s_seq[s_step % s_seq_len];

  uint32_t gate;
  if (s_have_prev) {
    uint32_t dt = now - s_prev_step_ms;
    gate = dt / 2u;
  } else {
    gate = (s_bpm ? (30000u / s_bpm) : 100u);  /* ~half the nominal interval */
  }
  if (gate < 10u)   gate = 10u;
  if (gate > 2000u) gate = 2000u;
  s_prev_step_ms = now;
  s_have_prev = 1;

  s_cb(note, gate, s_ctx);

  uint8_t len = s_length ? s_length : 1;
  s_step = (uint8_t)((s_step + 1u) % len);
}

static void seed_once(uint32_t now)
{
  if (!s_seeded) { s_rng = now ? now : 0xA5A5A5A5u; s_seeded = 1; }
}

void arp_tick(uint32_t now_ms)
{
  if (!s_enabled || s_clk != ARP_CLK_INTERNAL) return;
  seed_once(now_ms);

  uint16_t bpm = s_bpm ? s_bpm : 120;
  uint32_t interval = 60000u / bpm;
  if (interval == 0) interval = 1;

  if (s_restart) {
    s_restart = 0;
    s_step = 0;
    s_have_prev = 0;
    do_step(now_ms);
    s_next_ms = now_ms + interval;
    return;
  }
  if ((int32_t)(now_ms - s_next_ms) >= 0) {
    do_step(now_ms);
    s_next_ms = now_ms + interval;
  }
}

void arp_clock_pulse(uint32_t now_ms)
{
  if (!s_enabled || s_clk != ARP_CLK_EXTERNAL) return;
  seed_once(now_ms);
  if (s_restart) { s_restart = 0; s_step = 0; s_have_prev = 0; }
  do_step(now_ms);
}

void arp_init(arp_step_fn fn, void *ctx)
{
  s_cb = fn;
  s_ctx = ctx;
  s_chord_n = 0;
  s_dir = ARP_UP;
  s_octaves = 1;
  s_length = 8;
  s_clk = ARP_CLK_INTERNAL;
  s_bpm = 120;
  s_enabled = false;
  s_seq_len = 0;
  s_step = 0;
  s_have_prev = 0;
  s_restart = 0;
}

void arp_set_chord(const uint8_t *offsets, uint8_t count)
{
  if (count > ARP_MAX_CHORD) count = ARP_MAX_CHORD;
  for (uint8_t i = 0; i < count; i++) s_chord[i] = offsets[i];
  s_chord_n = count;
  rebuild_seq();
}

void arp_set_dir(arp_dir_t dir)
{
  if (dir >= ARP_DIR_COUNT) dir = ARP_UP;
  s_dir = dir;
  rebuild_seq();
}

void arp_set_octaves(uint8_t octaves)
{
  if (octaves < 1) octaves = 1;
  if (octaves > ARP_MAX_OCT) octaves = ARP_MAX_OCT;
  s_octaves = octaves;
  rebuild_seq();
}

void arp_set_length(uint8_t steps)
{
  s_length = steps ? steps : 1;
}

void arp_set_clock(arp_clk_t src)
{
  s_clk = src;
  s_restart = 1;   /* re-arm so the pattern starts cleanly on the new source */
}

void arp_set_bpm(uint16_t bpm)
{
  if (bpm < 40)  bpm = 40;
  if (bpm > 300) bpm = 300;
  s_bpm = bpm;
}

void arp_set_enabled(bool en)
{
  if (en && !s_enabled) s_restart = 1;
  s_enabled = en;
}

arp_dir_t arp_dir(void)      { return s_dir; }
uint8_t   arp_octaves(void)  { return s_octaves; }
uint8_t   arp_length(void)   { return s_length; }
arp_clk_t arp_clock(void)    { return s_clk; }
uint16_t  arp_bpm(void)      { return s_bpm; }
bool      arp_enabled(void)  { return s_enabled; }
