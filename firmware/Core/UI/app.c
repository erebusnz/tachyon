#include "app.h"
#include "menu.h"
#include "circle_of_fifths.h"
#include "encoder.h"
#include "GUI_Paint.h"
#include "main.h"
#include "cv_out.h"
#include "audio.h"
#include "wt_osc.h"
#include "gate_out.h"
#include "clock_in.h"
#include "arp.h"
#include "multisample.h"
#include "usbd_midi.h"
#include "sd_fs.h"
#include <math.h>
#include <string.h>
#include <stdio.h>

#define SCREEN_W       128
#define SCREEN_H       128
#define BG_LEVEL       0
#define FG_LEVEL       15

/* Pitch mapping.
 *
 * The wheel's note index counts fifths above C (COF_C=0, COF_G=1, ...), so the
 * chromatic semitone of the centered root is (idx * 7) mod 12. Pitch maps to a
 * 1 V/oct CV on CV-OUT-A and an audible tone; the arpeggiator works in absolute
 * semitones (root + chord offset + octave) via the _st helpers.
 */
#define PREVIEW_C_HZ     261.6256f    /* C4 — tone reference octave */
#define CV_OCTAVE_BASE   3.0f         /* volts at semitone 0 (mid of the 0–10 V range) */

static int cof_semitone(int idx)
{
    return (idx * 7) % 12;            /* fifths-above-C index -> chromatic semitone */
}

static float note_freq_st(int st)
{
    return PREVIEW_C_HZ * powf(2.0f, (float)st / 12.0f);
}

static float note_cv_st(int st)
{
    return CV_OCTAVE_BASE + (float)st / 12.0f;
}

/* Screens. The first menu items are persistent MODES (pitch source x engine);
 * "Config" opens config screens that don't change the running mode.
 * APP_SCREEN_BROWSE is reached from Audio Cfg, not the menu. */
typedef enum {
    APP_SCREEN_MENU = 0,
    APP_SCREEN_KEY_ARP,   /* mode: key wheel   -> Arp         */
    APP_SCREEN_CHORD_ARP, /* mode: chord wheel -> Arp         */
    APP_SCREEN_USB_MIDI,  /* mode: USB MIDI    -> Direct poly */
    APP_SCREEN_CONFIG,    /* Config submenu (Arp / Audio)  */
    APP_SCREEN_ARP_CFG,   /* config */
    APP_SCREEN_AUDIO,     /* config */
    APP_SCREEN_BROWSE,    /* from Audio Cfg */
} AppScreen;

/* Menu mode index i selects screen APP_SCREEN_FIRST_MODE + i. */
#define APP_SCREEN_FIRST_MODE  APP_SCREEN_KEY_ARP

/* Top menu: the play modes (index 0..N_MODES-1) then "Config" (a submenu). */
#define N_MODES  3
static const char *const k_mode_items[] = {
    "Key Arp",
    "Chord Arp",
    "USB MIDI",
    "Config",
};
#define MODE_COUNT  (sizeof(k_mode_items) / sizeof(k_mode_items[0]))

/* Persistent operating mode = (pitch source x engine), decoupled from the
 * on-screen view: the engine runs every loop regardless of which screen is up,
 * and keeps running when you back out to the menu / open a config screen. */
typedef enum { SRC_WHEEL = 0, SRC_CHORD, SRC_MIDI } pitch_src_t;
typedef enum { ENG_DIRECT = 0, ENG_ARP } engine_t;

static AppScreen   s_screen;
static Menu        s_menu;
static bool        s_mode_active;   /* false at boot -> silent until a mode is picked */
static pitch_src_t s_src;
static engine_t    s_engine;
static float       s_key_angle;     /* circle-of-fifths rotation, degrees */
static uint8_t     s_marked[12];    /* per-note marks (future chord def; unused in P1) */
static uint32_t    s_tone_off;      /* HAL tick to silence the current arp note (0 = none) */
static int         s_root_semitone; /* chromatic root the arp/Direct plays (live) */

/* Key context (set on the Key Arp wheel, shared with the Chord wheel). */
static int  s_key_tonic = 0;        /* tonic, chromatic 0..11 */
static bool s_key_major = true;     /* key quality: major or (natural) minor */

/* The key's seven diatonic triads, ordered by circle-of-fifths position
 * (IV I V ii vi iii vii deg) for the chord wheel. */
typedef struct { uint8_t cof_pos; uint8_t root; uint8_t quality; } chordinfo_t; /* quality 0=maj 1=min 2=dim */
static chordinfo_t s_chords[7];
static int s_chord_sel;             /* selected diatonic chord 0..6 */

/* ---- active wavetable + SD folder browser ---- */
#define WT_MAX_FOLDERS   160
#define DEFAULT_WT       "scaled_polygonal_1_base_3_harmonics_resamp"

static multisample_t s_active_ms;                 /* the loaded multisample */
static char  s_cur_wt[SD_FOLDER_NAME_LEN];         /* its folder name */
static bool  s_wt_loaded;
static int   s_level_pct = 100;                    /* output level 0..100 */
static char  s_folders[WT_MAX_FOLDERS][SD_FOLDER_NAME_LEN];
static int   s_folder_count;
static int   s_browse_sel;
static int   s_audio_cursor;    /* Audio Cfg row: 0=Wavetable, 1=Level */
static bool  s_audio_editing;   /* Audio Cfg: rotation edits Level value */
#define AUDIO_ROWS  2
static int   s_config_sel;      /* Config submenu row: 0=Arp, 1=Audio */

/* SD read of wavetable folder `name` into the active multisample. No oscillator
 * calls — pure data load. Returns true on success. */
static bool load_wt_data(const char *name)
{
    char path[16 + SD_FOLDER_NAME_LEN];
    snprintf(path, sizeof path, "0:/%s", name);
    if (multisample_load(path, &s_active_ms)) {
        snprintf(s_cur_wt, sizeof s_cur_wt, "%s", name);
        s_wt_loaded = true;
        return true;
    }
    s_wt_loaded = false;
    return false;
}

/* Runtime wavetable change (from the browser). Gates the oscillator to silence
 * while the SD read rewrites the PCM pool, then re-points it. */
static void app_load_wavetable(const char *name)
{
    wt_osc_set_multisample(NULL);
    if (load_wt_data(name)) wt_osc_set_multisample(&s_active_ms);
}

void app_preload(void)
{
    /* Runs before audio_init(): SD reads here have no audio-IRQ contention, so
     * the folder list (cached for the browser) and the default wavetable load
     * reliably. */
    s_folder_count = sd_fs_list_folders(s_folders, WT_MAX_FOLDERS);
    load_wt_data(DEFAULT_WT);
}

/* Note index currently centered at the top of the wheel (angle 0). */
static int centered_note_index(void)
{
    int snapped = (int)lroundf(s_key_angle / 30.0f);
    return ((snapped % 12) + 12) % 12;
}

/* ---- arpeggiator step -> outputs ---- */

static void arp_step_cb(int semitone, uint32_t gate_ms, void *ctx)
{
    (void)ctx;
    int st = s_root_semitone + semitone;
    cv_out_write_volts(CV_OUT_A, note_cv_st(st));
    wt_osc_note(60 + st, note_freq_st(st));   /* MIDI C4 = 60 = semitone 0 */
    gate_out_pulse(GATE_A, gate_ms);
    gate_out_pulse(GATE_B, gate_ms);
    s_tone_off = HAL_GetTick() + gate_ms;
}

/* ---- Arp settings screen ---- */

typedef enum {
    AP_DIR = 0, AP_OCT, AP_LEN, AP_CLK, AP_TEMPO, AP_COUNT
} arp_param_t;

static int  s_arp_cursor;
static bool s_arp_editing;   /* rotation edits the cursored param's value */

static const char *const arp_names[AP_COUNT] = {
    "Dir", "Octave", "Length", "Clock", "Tempo",
};

/* Adjust the parameter at the cursor by the encoder delta. */
static void arp_param_adjust(arp_param_t p, int32_t d)
{
    switch (p) {
    case AP_DIR: {
        int v = ((int)arp_dir() + (int)d) % ARP_DIR_COUNT;
        if (v < 0) v += ARP_DIR_COUNT;
        arp_set_dir((arp_dir_t)v);
        break;
    }
    case AP_OCT: {
        int v = (int)arp_octaves() + (int)d;
        if (v < 1) v = 1;
        if (v > (int)ARP_MAX_OCT) v = (int)ARP_MAX_OCT;
        arp_set_octaves((uint8_t)v);
        break;
    }
    case AP_LEN: {
        static const uint8_t L[] = { 4, 8, 16 };
        int cur = 1;
        for (int i = 0; i < 3; i++) if (L[i] == arp_length()) cur = i;
        int v = (cur + (int)d) % 3;
        if (v < 0) v += 3;
        arp_set_length(L[v]);
        break;
    }
    case AP_CLK:
        arp_set_clock(arp_clock() == ARP_CLK_INTERNAL ? ARP_CLK_EXTERNAL
                                                      : ARP_CLK_INTERNAL);
        break;
    case AP_TEMPO: {
        int v = (int)arp_bpm() + (int)d * 5;
        if (v < 40)  v = 40;
        if (v > 300) v = 300;
        arp_set_bpm((uint16_t)v);
        break;
    }
    default:
        break;
    }
}

/* Format the value of parameter `p` into `b`. */
static void arp_value_str(arp_param_t p, char *b, int n)
{
    static const char *const dirs[ARP_DIR_COUNT] = { "Up", "Down", "UpDn", "Rand" };
    switch (p) {
    case AP_DIR:     snprintf(b, n, "%s", dirs[arp_dir()]); break;
    case AP_OCT:     snprintf(b, n, "%u", arp_octaves()); break;
    case AP_LEN:     snprintf(b, n, "%u", arp_length()); break;
    case AP_CLK:     snprintf(b, n, "%s", arp_clock() == ARP_CLK_EXTERNAL ? "Ext" : "Int"); break;
    case AP_TEMPO:
        if (arp_clock() == ARP_CLK_EXTERNAL) snprintf(b, n, "ext");
        else                                 snprintf(b, n, "%u", arp_bpm());
        break;
    default: b[0] = '\0'; break;
    }
}

static void arp_render(void)
{
    char val[12], line[24];

    Paint_Clear(BG_LEVEL);
    Paint_DrawRectangle(0, 0, 127, 14, FG_LEVEL, DOT_PIXEL_1X1, DRAW_FILL_FULL);
    Paint_DrawString_EN(4, 1, "Arp", &Font12, BG_LEVEL, FG_LEVEL);

    for (int i = 0; i < AP_COUNT; i++) {
        int y = 16 + i * 15;
        arp_value_str((arp_param_t)i, val, sizeof val);
        /* Brackets around the value mark the param being edited. */
        if (i == s_arp_cursor && s_arp_editing)
            snprintf(line, sizeof line, "%s:[%s]", arp_names[i], val);
        else
            snprintf(line, sizeof line, "%s: %s", arp_names[i], val);
        if (i == s_arp_cursor) {
            Paint_DrawRectangle(0, y - 1, 127, y + 12, FG_LEVEL, DOT_PIXEL_1X1, DRAW_FILL_FULL);
            Paint_DrawString_EN(4, y, line, &Font12, BG_LEVEL, FG_LEVEL);
        } else {
            Paint_DrawString_EN(4, y, line, &Font12, FG_LEVEL, BG_LEVEL);
        }
    }
}

/* ---- shared helper: draw a horizontally-centered string ---- */

static void draw_centered(int y, const char *s, sFONT *font, UWORD fg, UWORD bg)
{
    int w = (int)strlen(s) * font->Width;
    int x = (SCREEN_W - w) / 2;
    if (x < 0) x = 0;
    Paint_DrawString_EN((UWORD)x, (UWORD)y, s, font, fg, bg);
}

/* Draw a list row at `y`; highlighted (inverted) when selected. */
static void draw_row(int y, const char *s, int selected)
{
    if (selected) {
        Paint_DrawRectangle(0, y - 1, 127, y + 12, FG_LEVEL, DOT_PIXEL_1X1, DRAW_FILL_FULL);
        Paint_DrawString_EN(4, y, s, &Font12, BG_LEVEL, FG_LEVEL);
    } else {
        Paint_DrawString_EN(4, y, s, &Font12, FG_LEVEL, BG_LEVEL);
    }
}

static const char *const k_note_names[12] = {
    "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"
};

static float midi_freq(int m)
{
    return 440.0f * powf(2.0f, (float)(m - 69) / 12.0f);
}

/* ---- USB-MIDI input: lock-free event ring + poly-voice routing ----
 *
 * The USB-MIDI device class (usbd_midi.c, added later) parses note events in
 * the OTG IRQ and calls app_midi_event() — a single-producer/single-consumer
 * push. The events are drained here on the main loop (midi_drain in app_tick)
 * and routed to the polyphonic voice pool while the USB MIDI mode is active, so
 * the oscillator's critical section is never entered from IRQ context. */

#define MIDI_RING_LEN 64                 /* power of two */
typedef struct { uint8_t kind, note, vel; } midi_evt_t;
static volatile midi_evt_t s_midi_ring[MIDI_RING_LEN];
static volatile uint8_t s_midi_head;     /* producer: USB IRQ */
static volatile uint8_t s_midi_tail;     /* consumer: main loop */

static uint8_t s_midi_held[16];          /* bitmap of currently-held notes */
static int     s_midi_held_n;            /* popcount of s_midi_held */
static int     s_midi_last = -1;         /* last note-on (for the screen) */

void app_midi_event(uint8_t kind, uint8_t note, uint8_t vel)
{
    uint8_t h = s_midi_head;
    uint8_t next = (uint8_t)((h + 1u) & (MIDI_RING_LEN - 1u));
    if (next == s_midi_tail) return;     /* ring full — drop the event */
    s_midi_ring[h].kind = kind;
    s_midi_ring[h].note = note;
    s_midi_ring[h].vel  = vel;
    __DMB();                             /* publish the slot before the index */
    s_midi_head = next;
}

static bool midi_evt_pop(midi_evt_t *e)
{
    if (s_midi_tail == s_midi_head) return false;
    *e = s_midi_ring[s_midi_tail];
    s_midi_tail = (uint8_t)((s_midi_tail + 1u) & (MIDI_RING_LEN - 1u));
    return true;
}

/* Forget all held notes (mode change / panic). */
static void midi_reset(void)
{
    for (int i = 0; i < 16; i++) s_midi_held[i] = 0;
    s_midi_held_n = 0;
    s_midi_last = -1;
}

/* Route one drained event to the voice pool — gated to the USB MIDI mode so
 * stale notes never leak into another mode. Velocity is ignored for now
 * (wt_osc exposes only a master level; see usb-midi.md §5). */
static void midi_note_on(uint8_t note, uint8_t vel)
{
    (void)vel;
    if (!s_mode_active || s_src != SRC_MIDI || note > 127) return;
    if (!(s_midi_held[note >> 3] & (1u << (note & 7)))) {
        s_midi_held[note >> 3] |= (uint8_t)(1u << (note & 7));
        s_midi_held_n++;
    }
    s_midi_last = note;
    wt_osc_note_on((int)note, midi_freq((int)note));
}

static void midi_note_off(uint8_t note)
{
    if (!s_mode_active || s_src != SRC_MIDI || note > 127) return;
    if (s_midi_held[note >> 3] & (1u << (note & 7))) {
        s_midi_held[note >> 3] &= (uint8_t)~(1u << (note & 7));
        if (s_midi_held_n > 0) s_midi_held_n--;
    }
    wt_osc_note_off((int)note);
}

static void midi_drain(void)
{
    midi_evt_t e;
    while (midi_evt_pop(&e)) {
        if (e.kind == APP_MIDI_NOTE_ON) midi_note_on(e.note, e.vel);
        else                            midi_note_off(e.note);
    }
}

/* Select a persistent mode (from the menu). */
static void set_mode(pitch_src_t src, engine_t eng)
{
    s_src = src;
    s_engine = eng;
    s_mode_active = true;
    s_tone_off = 0;
    midi_reset();
    arp_set_enabled(eng == ENG_ARP);
    wt_osc_all_off();          /* clean start; the engine re-activates the voice */
}

static void render_usb_midi(void)
{
    char line[24];
    Paint_Clear(BG_LEVEL);
    Paint_DrawRectangle(0, 0, 127, 14, FG_LEVEL, DOT_PIXEL_1X1, DRAW_FILL_FULL);
    Paint_DrawString_EN(4, 1, "USB MIDI", &Font12, BG_LEVEL, FG_LEVEL);

    if (!USBD_MIDI_IsConnected()) {
        draw_centered(50, "waiting for host", &Font12, FG_LEVEL, BG_LEVEL);
    } else if (s_midi_held_n > 0 && s_midi_last >= 0) {
        snprintf(line, sizeof line, "%s%d",
                 k_note_names[s_midi_last % 12], s_midi_last / 12 - 1);
        draw_centered(40, line, &Font24, FG_LEVEL, BG_LEVEL);
        snprintf(line, sizeof line, "%d held", s_midi_held_n);
        draw_centered(82, line, &Font12, FG_LEVEL, BG_LEVEL);
    } else {
        draw_centered(50, "connected", &Font12, FG_LEVEL, BG_LEVEL);
    }
    draw_centered(104, "push = all-off", &Font12, FG_LEVEL, BG_LEVEL);
    draw_centered(116, "hold = back", &Font12, FG_LEVEL, BG_LEVEL);
}

/* ---- Key / chord wheel ---- */

static void arp_use_triad(int quality)   /* 0=maj 1=min 2=dim */
{
    static const uint8_t maj[3] = { 0, 4, 7 };
    static const uint8_t min[3] = { 0, 3, 7 };
    static const uint8_t dim[3] = { 0, 3, 6 };
    if (quality == 1)      arp_set_chord(min, 3);
    else if (quality == 2) arp_set_chord(dim, 3);
    else                   arp_set_chord(maj, 3);
}

/* Key Arp: the arp plays the tonic triad of the selected key. */
static void key_arp_apply(void)
{
    s_root_semitone = s_key_tonic;
    arp_use_triad(s_key_major ? 0 : 1);
}

/* Build the key's seven diatonic triads, ordered by circle-of-fifths position.
 * The chord set is the same for relative major/minor; only the home tonic
 * differs. In COF order from (relative-major tonic - 1): IV I V ii vi iii vii,
 * qualities maj maj maj min min min dim. */
static void build_chords(void)
{
    int rel_major = s_key_major ? s_key_tonic : (s_key_tonic + 3) % 12;
    int pos0 = (rel_major * 7) % 12;              /* COF position of rel-major tonic */
    static const uint8_t qual_by_cof[7] = { 0, 0, 0, 1, 1, 1, 2 };
    for (int k = 0; k < 7; k++) {
        int cof = (pos0 - 1 + k + 12) % 12;
        s_chords[k].cof_pos = (uint8_t)cof;
        s_chords[k].root    = (uint8_t)cof_semitone(cof);
        s_chords[k].quality = qual_by_cof[k];
    }
}

static int chord_tonic_index(void)            /* index of the I / i chord */
{
    for (int k = 0; k < 7; k++)
        if (s_chords[k].root == s_key_tonic) return k;
    return 1;
}

/* Chord Arp: the arp plays the selected diatonic chord. */
static void chord_arp_apply(void)
{
    s_root_semitone = s_chords[s_chord_sel].root;
    arp_use_triad(s_chords[s_chord_sel].quality);
}

static void chord_name(int k, char *b, int n) /* "C", "Dm", "Bdim" */
{
    const chordinfo_t *c = &s_chords[k];
    const char *suf = c->quality == 1 ? "m" : (c->quality == 2 ? "dim" : "");
    snprintf(b, n, "%s%s", k_note_names[c->root % 12], suf);
}

static void render_chord(void)
{
    char line[24];

    /* COF wheel: mark the 7 diatonic chords, rotate the selected one to the top. */
    uint8_t marked[12] = { 0 };
    for (int k = 0; k < 7; k++) marked[s_chords[k].cof_pos] = 1;
    cof_render_angle((float)s_chords[s_chord_sel].cof_pos * 30.0f, marked);

    /* The wheel shows only the root letter — overlay the chord name (with
     * quality) and the key in the empty lower area. */
    chord_name(s_chord_sel, line, sizeof line);
    draw_centered(90, line, &Font16, FG_LEVEL, BG_LEVEL);
    snprintf(line, sizeof line, "key %s %s", k_note_names[s_key_tonic],
             s_key_major ? "maj" : "min");
    draw_centered(116, line, &Font12, FG_LEVEL, BG_LEVEL);
}

/* ---- Audio Config + wavetable browser ---- */

#define BROWSE_ROWS  8

static void enter_browser(void)
{
    /* Folder list was cached at boot by app_preload() — no SD read here. Just
     * place the cursor on the current wavetable. */
    s_browse_sel = 0;
    for (int i = 0; i < s_folder_count; i++)
        if (strcmp(s_folders[i], s_cur_wt) == 0) { s_browse_sel = i; break; }
    s_screen = APP_SCREEN_BROWSE;
}

static void render_audio_cfg(void)
{
    char line[24];
    Paint_Clear(BG_LEVEL);
    Paint_DrawRectangle(0, 0, 127, 14, FG_LEVEL, DOT_PIXEL_1X1, DRAW_FILL_FULL);
    Paint_DrawString_EN(4, 1, "Audio Cfg", &Font12, BG_LEVEL, FG_LEVEL);

    /* Row 0: Wavetable (push opens the browser). Current name shown below. */
    draw_row(22, "Wavetable", s_audio_cursor == 0);
    snprintf(line, sizeof line, " %.18s", s_wt_loaded ? s_cur_wt : "(none)");
    Paint_DrawString_EN(4, 38, line, &Font12, FG_LEVEL, BG_LEVEL);

    /* Row 1: Level (push toggles edit; then rotate adjusts). */
    snprintf(line, sizeof line, "Level: %d%%%s", s_level_pct,
             s_audio_editing ? "  edit" : "");
    draw_row(62, line, s_audio_cursor == 1);

    draw_centered(104, s_audio_editing ? "turn=adjust  push=ok"
                                       : "turn=move  push=open", &Font12,
                 FG_LEVEL, BG_LEVEL);
}

static void render_browse(void)
{
    char line[24];
    Paint_Clear(BG_LEVEL);
    Paint_DrawRectangle(0, 0, 127, 12, FG_LEVEL, DOT_PIXEL_1X1, DRAW_FILL_FULL);
    snprintf(line, sizeof line, "Wavetable %d/%d",
             s_folder_count ? s_browse_sel + 1 : 0, s_folder_count);
    Paint_DrawString_EN(2, 0, line, &Font12, BG_LEVEL, FG_LEVEL);

    if (s_folder_count == 0) {
        draw_centered(56, "no wavetables", &Font12, FG_LEVEL, BG_LEVEL);
        return;
    }

    int top = s_browse_sel - BROWSE_ROWS / 2;
    if (top > s_folder_count - BROWSE_ROWS) top = s_folder_count - BROWSE_ROWS;
    if (top < 0) top = 0;

    for (int i = 0; i < BROWSE_ROWS && top + i < s_folder_count; i++) {
        int idx = top + i;
        int y = 14 + i * 14;
        snprintf(line, sizeof line, "%.18s", s_folders[idx]);
        if (idx == s_browse_sel) {
            Paint_DrawRectangle(0, y - 1, 127, y + 12, FG_LEVEL, DOT_PIXEL_1X1, DRAW_FILL_FULL);
            Paint_DrawString_EN(2, y, line, &Font12, BG_LEVEL, FG_LEVEL);
        } else {
            Paint_DrawString_EN(2, y, line, &Font12, FG_LEVEL, BG_LEVEL);
        }
    }
}

/* ---- main menu + Config submenu (custom render with icons) ---- */

#define MENU_ROW_H  22

/* Right-pointing filled "play" triangle (~8 x 12), top-left at (x, y). */
static void draw_play_icon(int x, int y, UWORD color)
{
    for (int cx = 0; cx <= 8; cx++) {
        int half = (8 - cx) * 6 / 8;
        Paint_DrawLine(x + cx, y + 6 - half, x + cx, y + 6 + half,
                       color, DOT_PIXEL_1X1, LINE_STYLE_SOLID);
    }
}

/* Simple gear centered at (cx, cy): body + hole + eight teeth. */
static void draw_cog_icon(int cx, int cy, UWORD color, UWORD bg)
{
    for (int k = 0; k < 8; k++) {
        float a = (float)k * (3.14159265f / 4.0f);
        int tx = cx + (int)lroundf(6.0f * cosf(a));
        int ty = cy + (int)lroundf(6.0f * sinf(a));
        Paint_DrawRectangle(tx - 1, ty - 1, tx + 1, ty + 1, color,
                            DOT_PIXEL_1X1, DRAW_FILL_FULL);
    }
    Paint_DrawCircle(cx, cy, 5, color, DOT_PIXEL_1X1, DRAW_FILL_FULL);
    Paint_DrawCircle(cx, cy, 2, bg,    DOT_PIXEL_1X1, DRAW_FILL_FULL);
}

static void render_main_menu(void)
{
    Paint_Clear(BG_LEVEL);
    int n = s_menu.count;
    int top = (SCREEN_H - n * MENU_ROW_H) / 2;
    if (top < 0) top = 0;

    for (int i = 0; i < n; i++) {
        int   y   = top + i * MENU_ROW_H;
        int   sel = (i == s_menu.selected);
        UWORD fg  = sel ? BG_LEVEL : FG_LEVEL;
        UWORD bg  = sel ? FG_LEVEL : BG_LEVEL;
        if (sel)
            Paint_DrawRectangle(2, y, 125, y + MENU_ROW_H - 3, FG_LEVEL,
                                DOT_PIXEL_1X1, DRAW_FILL_FULL);
        if (i < N_MODES) draw_play_icon(8, y + 3, fg);                 /* play modes */
        else             draw_cog_icon(13, y + MENU_ROW_H / 2 - 1, fg, bg);  /* Config */
        Paint_DrawString_EN(28, y + 4, s_menu.items[i], &Font12, fg, bg);
    }
}

static void render_config_menu(void)
{
    static const char *const items[] = { "Arp Cfg", "Audio Cfg" };
    Paint_Clear(BG_LEVEL);
    Paint_DrawRectangle(0, 0, 127, 14, FG_LEVEL, DOT_PIXEL_1X1, DRAW_FILL_FULL);
    draw_cog_icon(9, 7, BG_LEVEL, FG_LEVEL);
    Paint_DrawString_EN(20, 1, "Config", &Font12, BG_LEVEL, FG_LEVEL);
    for (int i = 0; i < 2; i++)
        draw_row(30 + i * 20, items[i], i == s_config_sel);
    draw_centered(104, "hold = back", &Font12, FG_LEVEL, BG_LEVEL);
}

void app_init(void)
{
    static const uint8_t triad[] = { 0, 4, 7 };  /* major triad of the key */

    s_screen = APP_SCREEN_MENU;
    menu_init(&s_menu, k_mode_items, (uint8_t)MODE_COUNT);
    s_mode_active = false;        /* silent until a mode is selected */
    s_key_angle = 0.0f;
    for (int i = 0; i < 12; i++) s_marked[i] = 0;
    s_tone_off = 0;
    s_root_semitone = 0;
    s_arp_cursor = 0;
    s_key_tonic = 0;
    s_key_major = true;
    build_chords();
    s_chord_sel = chord_tonic_index();

    arp_init(arp_step_cb, NULL);
    arp_set_chord(triad, sizeof triad);
    arp_set_dir(ARP_UP);
    arp_set_octaves(1);
    arp_set_length(8);
    arp_set_clock(ARP_CLK_INTERNAL);
    arp_set_bpm(120);
    arp_set_enabled(false);

    /* Output level + point the oscillator at the wavetable preloaded by
     * app_preload() (loaded before audio started). */
    wt_osc_set_level((float)s_level_pct / 100.0f);
    if (s_wt_loaded) wt_osc_set_multisample(&s_active_ms);
}

void app_tick(void)
{
    uint32_t now = HAL_GetTick();
    int32_t  detents = encoder_get_delta();
    encoder_btn_event_t ev = encoder_get_button_event();

    /* Drain USB-MIDI note events into the voice pool (USB MIDI mode). */
    midi_drain();

    /* Arp runs while the active engine is Arp (arp_enabled set via set_mode). */
    if (arp_enabled()) {
        arp_tick(now);
        if (arp_clock() == ARP_CLK_EXTERNAL && clock_in_edge())
            arp_clock_pulse(now);
    }

    switch (s_screen) {
    case APP_SCREEN_MENU:
        if (detents) menu_move(&s_menu, detents);
        if (ev == ENC_BTN_SHORT_PRESS) {
            if (s_menu.selected < N_MODES) {
                s_screen = (AppScreen)(APP_SCREEN_FIRST_MODE + s_menu.selected);
                switch (s_screen) {
                case APP_SCREEN_KEY_ARP:
                    s_key_tonic = cof_semitone(centered_note_index());
                    key_arp_apply();
                    set_mode(SRC_WHEEL, ENG_ARP);
                    break;
                case APP_SCREEN_CHORD_ARP:
                    build_chords();
                    s_chord_sel = chord_tonic_index();
                    chord_arp_apply();
                    set_mode(SRC_CHORD, ENG_ARP);
                    break;
                case APP_SCREEN_USB_MIDI:
                    set_mode(SRC_MIDI, ENG_DIRECT);
                    break;
                default: break;
                }
            } else {
                s_screen = APP_SCREEN_CONFIG;       /* Config submenu */
            }
            printf("MENU select: %s\r\n", k_mode_items[s_menu.selected]);
        }
        render_main_menu();
        break;

    case APP_SCREEN_CONFIG:
        if (ev == ENC_BTN_LONG_PRESS) {
            s_screen = APP_SCREEN_MENU;
        } else if (ev == ENC_BTN_SHORT_PRESS) {
            s_screen = (s_config_sel == 0) ? APP_SCREEN_ARP_CFG : APP_SCREEN_AUDIO;
        } else if (detents) {
            int v = s_config_sel + (int)detents;
            if (v < 0) v = 0;
            if (v > 1) v = 1;
            s_config_sel = v;
        }
        render_config_menu();
        break;

    case APP_SCREEN_USB_MIDI:
        if (ev == ENC_BTN_LONG_PRESS) {
            s_screen = APP_SCREEN_MENU;            /* mode persists, keeps sounding */
        } else if (ev == ENC_BTN_SHORT_PRESS) {   /* panic: release every voice */
            wt_osc_all_off();
            midi_reset();
        }
        render_usb_midi();
        break;

    case APP_SCREEN_KEY_ARP:
        if (ev == ENC_BTN_LONG_PRESS) {
            s_screen = APP_SCREEN_MENU;            /* mode persists, keeps sounding */
        } else if (ev == ENC_BTN_SHORT_PRESS) {
            s_key_major = !s_key_major;            /* toggle major / minor */
            key_arp_apply();
        } else if (detents) {
            s_key_angle += (float)detents * 30.0f;
            while (s_key_angle < 0.0f)    s_key_angle += 360.0f;
            while (s_key_angle >= 360.0f) s_key_angle -= 360.0f;
            s_key_tonic = cof_semitone(centered_note_index());
            key_arp_apply();
        }
        cof_render_angle(s_key_angle, s_marked);
        {
            char line[16];
            snprintf(line, sizeof line, "%s %s", k_note_names[s_key_tonic],
                     s_key_major ? "maj" : "min");
            draw_centered(116, line, &Font12, FG_LEVEL, BG_LEVEL);
        }
        break;

    case APP_SCREEN_CHORD_ARP:
        if (ev == ENC_BTN_LONG_PRESS) {
            s_screen = APP_SCREEN_MENU;            /* mode persists, keeps sounding */
        } else if (detents) {
            int v = s_chord_sel + (int)detents;
            if (v < 0) v = 0;
            if (v > 6) v = 6;
            if (v != s_chord_sel) { s_chord_sel = v; chord_arp_apply(); }
        }
        render_chord();
        break;

    case APP_SCREEN_ARP_CFG:
        if (ev == ENC_BTN_LONG_PRESS) {
            if (s_arp_editing) s_arp_editing = false;   /* leave edit first */
            else s_screen = APP_SCREEN_CONFIG;
        } else if (ev == ENC_BTN_SHORT_PRESS) {
            if (s_arp_editing) {
                s_arp_editing = false;                  /* confirm value */
            } else if (s_arp_cursor == AP_CLK) {
                arp_param_adjust((arp_param_t)s_arp_cursor, 1);  /* toggle in place */
            } else {
                s_arp_editing = true;                   /* edit value param */
            }
        } else if (detents) {
            if (s_arp_editing) {
                arp_param_adjust((arp_param_t)s_arp_cursor, detents);
            } else {
                int c = ((int)s_arp_cursor + (int)detents) % AP_COUNT;
                if (c < 0) c += AP_COUNT;
                s_arp_cursor = c;
            }
        }
        arp_render();
        break;

    case APP_SCREEN_AUDIO:
        if (ev == ENC_BTN_LONG_PRESS) {
            if (s_audio_editing) s_audio_editing = false;   /* leave edit first */
            else s_screen = APP_SCREEN_CONFIG;
        } else if (ev == ENC_BTN_SHORT_PRESS) {
            if (s_audio_cursor == 0) {
                enter_browser();                            /* Wavetable -> browse */
            } else {
                s_audio_editing = !s_audio_editing;         /* Level -> edit toggle */
            }
        } else if (detents) {
            if (s_audio_editing) {                          /* adjust level value */
                int v = s_level_pct + (int)detents * 5;
                if (v < 0)   v = 0;
                if (v > 100) v = 100;
                s_level_pct = v;
                wt_osc_set_level((float)v / 100.0f);
            } else {                                        /* move the row cursor */
                int c = s_audio_cursor + (int)detents;
                if (c < 0) c = 0;
                if (c > AUDIO_ROWS - 1) c = AUDIO_ROWS - 1;
                s_audio_cursor = c;
            }
        }
        render_audio_cfg();
        break;

    case APP_SCREEN_BROWSE:
        if (ev == ENC_BTN_LONG_PRESS) {
            s_screen = APP_SCREEN_AUDIO;           /* cancel */
        } else if (ev == ENC_BTN_SHORT_PRESS) {
            if (s_folder_count > 0) {
                app_load_wavetable(s_folders[s_browse_sel]);
                printf("WT load: %s (%s)\r\n", s_cur_wt, s_wt_loaded ? "ok" : "FAIL");
            }
            s_screen = APP_SCREEN_AUDIO;
        } else if (detents && s_folder_count > 0) {
            int v = s_browse_sel + (int)detents;
            if (v < 0) v = 0;
            if (v >= s_folder_count) v = s_folder_count - 1;
            s_browse_sel = v;
        }
        render_browse();
        break;
    }

    /* Audio is on whenever a mode is active; the s_tone_off window gives the arp
     * its per-step gate (the Direct engine holds its note continuously). */
    audio_mute(!s_mode_active);
    if (s_tone_off && (int32_t)(now - s_tone_off) >= 0) {
        wt_osc_gate_off();
        s_tone_off = 0;
    }
}
