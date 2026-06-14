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
#include "analog_in.h"
#include "multisample.h"
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
#define PREVIEW_MS       250          /* one-shot key-preview blip length */
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

static float note_freq(int idx) { return note_freq_st(cof_semitone(idx)); }
static float note_cv(int idx)   { return note_cv_st(cof_semitone(idx)); }

/* CV VCO free-run: oscillator pitch tracks CV-IN-A at 1 V/oct, 0 V = C3.
 * CV-IN is a modulation input (uncalibrated, ~1.6 kHz filtered, see
 * cv-input.md), so the CV-derived pitch is QUANTIZED to the nearest semitone —
 * the imprecision snaps to clean in-tune notes instead of a detuned glide. */
#define CV_VCO_BASE_MIDI   48        /* C3 at 0 V */

/* Operating modes, in menu order. APP_SCREEN_BROWSE is not a menu item — it is
 * reached from the Audio Cfg screen. */
typedef enum {
    APP_SCREEN_MENU = 0,
    APP_SCREEN_KEY_SELECT,
    APP_SCREEN_ARP,
    APP_SCREEN_CV,
    APP_SCREEN_AUDIO,
    APP_SCREEN_BROWSE,
} AppScreen;

/* Menu item indices map to the screens above (offset by one: index 0 -> the
 * first non-menu screen). Keep the labels in sync with that mapping. */
static const char *const k_mode_items[] = {
    "Key Select",
    "Arp",
    "CV VCO",
    "Audio Cfg",
};
#define MODE_COUNT  (sizeof(k_mode_items) / sizeof(k_mode_items[0]))

static AppScreen s_screen;
static Menu      s_menu;
static float     s_key_angle;     /* circle-of-fifths rotation, degrees */
static uint8_t   s_marked[12];    /* per-note mark flags */
static uint32_t  s_tone_off;      /* HAL tick to silence the current tone (0 = none) */
static int       s_root_semitone; /* chromatic root for pitch mapping (live) */

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

/* ---- Key Select preview (only used while the arp is off) ---- */

static void key_preview(int idx)
{
    cv_out_write_volts(CV_OUT_A, note_cv(idx));
    wt_osc_note(60 + cof_semitone(idx), note_freq(idx));
    gate_out_pulse(GATE_A, PREVIEW_MS);
    gate_out_pulse(GATE_B, PREVIEW_MS);
    s_tone_off = HAL_GetTick() + PREVIEW_MS;
    printf("KEY pitch: idx=%d st=%d cv=%.3fV %.1fHz\r\n",
           idx, cof_semitone(idx), note_cv(idx), note_freq(idx));
}

/* ---- Arp settings screen ---- */

typedef enum {
    AP_ENABLED = 0, AP_DIR, AP_OCT, AP_LEN, AP_CLK, AP_TEMPO, AP_COUNT
} arp_param_t;

static int s_arp_cursor;

static const char *const arp_names[AP_COUNT] = {
    "Enable", "Dir", "Octave", "Length", "Clock", "Tempo",
};

/* Adjust the parameter at the cursor by the encoder delta. */
static void arp_param_adjust(arp_param_t p, int32_t d)
{
    switch (p) {
    case AP_ENABLED:
        arp_set_enabled(!arp_enabled());
        break;
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
    case AP_ENABLED: snprintf(b, n, "%s", arp_enabled() ? "On" : "Off"); break;
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

/* ---- CV VCO free-run screen ---- */

static const char *const k_note_names[12] = {
    "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"
};

static int   s_cv_midi = -1;   /* current quantized MIDI note (-1 = unset) */
static uint8_t s_cv_src = ANALOG_CV_A;   /* CV VCO input: CV-IN-A or CV-IN-B */

/* Read CV-IN-A (1 V/oct, 0 V = C3), quantize to the nearest semitone (with a
 * little hysteresis so noise near a step boundary doesn't flutter between
 * notes), and drive the free-run oscillator at the exact note frequency. */
static void cv_vco_update(void)
{
    float v = analog_in_cv_volts(s_cv_src);
    float midi_f = (float)CV_VCO_BASE_MIDI + v * 12.0f;

    if (s_cv_midi < 0 || fabsf(midi_f - (float)s_cv_midi) > 0.6f)
        s_cv_midi = (int)lroundf(midi_f);
    if (s_cv_midi < 0)   s_cv_midi = 0;
    if (s_cv_midi > 127) s_cv_midi = 127;

    float freq = 440.0f * powf(2.0f, (float)(s_cv_midi - 69) / 12.0f);
    wt_osc_set_pitch(s_cv_midi, freq);
}

static void render_cv(void)
{
    char line[24];
    Paint_Clear(BG_LEVEL);
    Paint_DrawRectangle(0, 0, 127, 14, FG_LEVEL, DOT_PIXEL_1X1, DRAW_FILL_FULL);
    snprintf(line, sizeof line, "CV VCO   in: %c",
             s_cv_src == ANALOG_CV_B ? 'B' : 'A');
    Paint_DrawString_EN(4, 1, line, &Font12, BG_LEVEL, FG_LEVEL);

    if (s_cv_midi >= 0) {
        /* Note + octave is the whole point — big and centered. */
        snprintf(line, sizeof line, "%s%d", k_note_names[s_cv_midi % 12],
                 s_cv_midi / 12 - 1);
        draw_centered(44, line, &Font24, FG_LEVEL, BG_LEVEL);
    }
    draw_centered(86, "push = A/B", &Font12, FG_LEVEL, BG_LEVEL);
    draw_centered(104, "hold = back", &Font12, FG_LEVEL, BG_LEVEL);
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

void app_init(void)
{
    static const uint8_t triad[] = { 0, 4, 7 };  /* major triad of the key */

    s_screen = APP_SCREEN_MENU;
    menu_init(&s_menu, k_mode_items, (uint8_t)MODE_COUNT);
    s_key_angle = 0.0f;
    for (int i = 0; i < 12; i++) s_marked[i] = 0;
    s_tone_off = 0;
    s_root_semitone = 0;
    s_arp_cursor = 0;

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

    /* Arpeggiator runs globally whenever enabled. */
    if (arp_enabled()) {
        arp_tick(now);
        if (arp_clock() == ARP_CLK_EXTERNAL && clock_in_edge())
            arp_clock_pulse(now);
    }

    switch (s_screen) {
    case APP_SCREEN_MENU:
        if (detents) {
            menu_move(&s_menu, detents);
        }
        if (ev == ENC_BTN_SHORT_PRESS) {
            s_screen = (AppScreen)(APP_SCREEN_KEY_SELECT + s_menu.selected);
            if (s_screen == APP_SCREEN_KEY_SELECT) {
                int idx = centered_note_index();
                s_root_semitone = cof_semitone(idx);
                if (!arp_enabled()) key_preview(idx);
            }
            printf("MENU select: %s\r\n", k_mode_items[s_menu.selected]);
        }
        menu_render(&s_menu);
        break;

    case APP_SCREEN_KEY_SELECT: {
        int idx_before = centered_note_index();
        if (detents) {
            s_key_angle += (float)detents * 30.0f;
            while (s_key_angle < 0.0f)    s_key_angle += 360.0f;
            while (s_key_angle >= 360.0f) s_key_angle -= 360.0f;
        }
        int idx = centered_note_index();

        if (ev == ENC_BTN_SHORT_PRESS) {
            /* Toggle the centered note's mark (note at top = angle 0). */
            s_marked[idx] = !s_marked[idx];
            printf("KEY mark toggle: idx=%d marked=%u\r\n", idx, s_marked[idx]);
        } else if (ev == ENC_BTN_LONG_PRESS) {
            s_screen = APP_SCREEN_MENU;
            printf("KEY -> menu\r\n");
        }

        /* Key changed: track the root (transposes a running arp) and, if the
         * arp is off, sound the one-shot preview. */
        if (s_screen == APP_SCREEN_KEY_SELECT && idx != idx_before) {
            s_root_semitone = cof_semitone(idx);
            if (!arp_enabled()) key_preview(idx);
        }

        cof_render_angle(s_key_angle, s_marked);
        break;
    }

    case APP_SCREEN_ARP:
        if (ev == ENC_BTN_LONG_PRESS) {
            s_screen = APP_SCREEN_MENU;
        } else if (ev == ENC_BTN_SHORT_PRESS) {
            s_arp_cursor = (s_arp_cursor + 1) % AP_COUNT;
        }
        if (detents) {
            arp_param_adjust((arp_param_t)s_arp_cursor, detents);
        }
        arp_render();
        break;

    case APP_SCREEN_CV:
        if (ev == ENC_BTN_LONG_PRESS) {
            s_screen = APP_SCREEN_MENU;
            wt_osc_gate_off();          /* stop the free-run voice */
        } else {
            if (ev == ENC_BTN_SHORT_PRESS)     /* toggle CV-IN-A / CV-IN-B */
                s_cv_src = (s_cv_src == ANALOG_CV_A) ? ANALOG_CV_B : ANALOG_CV_A;
            cv_vco_update();            /* track the selected CV input -> pitch */
            render_cv();
        }
        break;

    case APP_SCREEN_AUDIO:
        if (ev == ENC_BTN_LONG_PRESS) {
            if (s_audio_editing) s_audio_editing = false;   /* leave edit first */
            else s_screen = APP_SCREEN_MENU;
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

    /* Central audio control. CV VCO sounds continuously; otherwise sound while
     * the arp runs or in Key Select (previews), and end elapsed tones. */
    if (s_screen == APP_SCREEN_CV) {
        audio_mute(false);
    } else {
        audio_mute(!(arp_enabled() || s_screen == APP_SCREEN_KEY_SELECT));
        if (s_tone_off && (int32_t)(now - s_tone_off) >= 0) {
            wt_osc_gate_off();
            s_tone_off = 0;
        }
    }
}
