# Filters — velocity-driven envelope filter for keyboard play

This document specifies the Tachyon's filter subsystem. The first deliverable
is a classic **envelope filter**: a per-voice lowpass whose cutoff is swept by
a velocity-scaled ADSR, so playing the USB-MIDI keyboard harder opens the
filter (and the note is louder). Later phases extend the same building blocks
to other filter types and modulation sources.

Source references / modules this builds on:
- `Core/IO/audio.c` — I2S render seam: ~192 kHz (`AUDIO_FS_HZ`), 128-frame
  blocks (`HALF_FRAMES`), render callback runs in the DMA Tx IRQ (≈667 µs
  deadline per block, see the `O3` note in `wt_osc.c`)
- `Core/IO/wt_osc.c` — 4-voice wavetable oscillator; the filter and envelope
  live inside its voice loop
- `Core/UI/app.c` — USB-MIDI event ring already carries `vel`, currently
  dropped in `midi_note_on()` ("Velocity is ignored for now")
- `usb-midi.md` §5 — flags per-voice velocity as a future `wt_osc` extension
- `firmware/test/` — host Unity/CTest suite; the new DSP modules are pure C
  and get host tests like `arp.c` and `wav.c` do

---

## 1. Concept

Today a voice is `wavetable → gain → sum`. Note-on resets phase (clean
attack); note-off zeroes `active` (a hard cut, which clicks). This plan makes
each voice:

```
             ┌───────────┐    ┌─────────────┐    ┌───────────┐
  note ───▶  │ wavetable │ ─▶ │  SVF (LP)   │ ─▶ │ VCA gain  │ ─▶ sum
             └───────────┘    └─────────────┘    └───────────┘
                                    ▲                  ▲
                              cutoff = base      level = env
                              × 2^(amt·env·vel)  × vel curve
                                    │                  │
                              ┌─────┴──────────────────┴───┐
  gate/velocity ────────────▶ │      per-voice ADSR        │
                              └────────────────────────────┘
```

- **One ADSR per voice**, triggered by note-on with that note's velocity,
  released by note-off. Phase 1 routes the single envelope to **both** the
  filter cutoff and the voice amplitude (Minimoog-style shared contour). A
  separate filter envelope is a later phase — one envelope already gives the
  target behaviour (velocity → brightness + loudness) and fixes the note-off
  click for free.
- **One SVF per voice**, because velocity is per note: two notes at different
  velocities must sound different brightnesses simultaneously. A single
  filter on the mix (paraphonic) is the documented fallback if the CPU
  budget fails (§6), not the design.
- Envelope and cutoff are evaluated at **block rate** (128 frames ≈ 0.67 ms —
  well above any audible envelope granularity); only the filter state update
  runs per sample.

---

## 2. Modules

Two new pure-C modules, no HAL includes, so they compile host-side unchanged
(same pattern as `arp.c`):

### 2.1 `Core/DSP/adsr.{c,h}`

```c
typedef struct { float a_coef, d_coef, r_coef, sustain; } adsr_params_t;
typedef struct { float level; uint8_t stage; } adsr_t;   /* per voice */

void  adsr_config(adsr_params_t *p, float a_ms, float d_ms,
                  float sustain, float r_ms);            /* main loop */
void  adsr_gate_on(adsr_t *e);        /* (re)attack from current level */
void  adsr_gate_off(adsr_t *e);
float adsr_step(adsr_t *e, const adsr_params_t *p);      /* one block tick */
bool  adsr_idle(const adsr_t *e);     /* fully released — voice reclaimable */
```

- Stages: idle → attack → decay → sustain → release. Exponential segments
  (one-pole toward a target), the standard analog-feel shape:
  `level += coef * (target - level)`, with attack overshooting toward ~1.27
  so it actually reaches 1.0 in the nominal time.
- Coefficients are derived from times at the **block rate**
  (`AUDIO_FS_HZ / HALF_FRAMES` ≈ 1.5 kHz), computed in `adsr_config()` on the
  main loop — the IRQ path never calls `expf`.
- Time ranges log-scaled: A 1 ms–5 s, D/R 5 ms–10 s, S 0–1.
- Retrigger attacks **from the current level** (no reset to 0) so fast
  re-strikes don't click.
- Release-to-idle threshold ≈ −80 dB (1e-4); at idle the voice is silent and
  reclaimable.

### 2.2 `Core/DSP/svf.{c,h}`

Zavalishin/Simper **TPT state-variable filter** — stable under fast cutoff
modulation (an envelope sweep is exactly that), cheap, and gives LP/BP/HP
from the same state for later phases.

```c
typedef struct { float g, k; } svf_coef_t;    /* shared per voice-block */
typedef struct { float ic1, ic2; } svf_t;     /* per voice state */

void  svf_coef(svf_coef_t *c, float fc_hz, float res, float fs_hz);
float svf_lp(svf_t *s, const svf_coef_t *c, float x);  /* per sample */
void  svf_reset(svf_t *s);
```

- `g = tan(pi * fc / fs)`, `k = 2 - 2*res`. At 192 kHz even fc = 20 kHz is
  only fs/9.6, so a short polynomial `tan` approximation is accurate across
  the whole audible range — no lookup table needed. `svf_coef()` runs at
  block rate; verify on the bench whether the approximation is needed at all
  or plain `tanf` fits the budget.
- `res` 0–1, clamped below self-oscillation (k ≥ ~0.06, Q ≈ 16) in phase 1.
- fc clamped to [20 Hz, 20 kHz].
- Per-sample cost ≈ 9 flops. Worst case 4 voices × 128 samples ≈ 4.6 k flops
  per block — tens of µs at 168 MHz under `O3`, comfortably inside the
  667 µs deadline. Confirmed by measurement in phase 3, not assumed (§6).

---

## 3. Voice integration (`wt_osc.c`)

### 3.1 Voice struct additions

`wt_voice_t` gains: `adsr_t env`, `svf_t filt`, `float vel` (0–1), and the
block snapshot in `wt_render()` carries the per-voice gain and `svf_coef_t`
for the block.

### 3.2 Render loop changes

Per block, per active voice (before the sample loop):
1. `e = adsr_step(&v->env, &params)`
2. `fc = base_cutoff * exp2f(env_amount_oct * e * v->vel)` — cutoff in
   octaves above the base, scaled by envelope and velocity
3. `svf_coef(...)` for this block
4. voice gain `= WT_OUT_GAIN * e * vel_curve(v->vel)`
5. if `adsr_idle()` after release → clear `active` (voice reclaimed)

Per sample, per voice: existing table lookup → `svf_lp()` → accumulate with
the block gain. One `exp2f` + one coefficient computation per voice per
block is main-cost-free next to the sample loop.

Block-rate coefficient stepping (rather than per-sample smoothing) is the
phase-1 answer to zipper noise; at a 1.5 kHz update rate with an exponential
envelope this is inaudible for cutoff. If a fast full-range attack sweep
audibly steps, linear-interpolate `g` across the block as a follow-up.

### 3.3 Note lifecycle changes (the semantic shift)

This is the real behavioural change and needs care:

- **`wt_osc_note_off()` no longer silences** — it calls `adsr_gate_off()`;
  the voice keeps sounding through its release and frees itself when the
  envelope idles (render clears `active`).
- **Voice allocation** (`alloc_voice()`): prefer a truly idle voice, then the
  quietest *releasing* voice, then steal the oldest as today. A stolen voice
  hard-retriggers (envelope re-attacks from its current level — no click).
- **`wt_osc_all_off()` / mode switches**: forcing every envelope into a fast
  release (~5 ms) instead of an instant cut kills the mode-change click too;
  `wt_osc_set_multisample()` keeps the hard cut (the table pointer is going
  away under the voice).

### 3.4 API — velocity plumbing

```c
void wt_osc_note_on(int midi_note, float freq_hz, uint8_t vel);  /* 1..127 */
```

- Existing call sites and the mono helpers (`wt_osc_note`,
  `wt_osc_set_pitch`, `wt_osc_chord`) pass a default velocity (100); the arp
  and CV paths get real dynamics in a later phase (arp accent).
- `app.c midi_note_on()` drops its `(void)vel;` and forwards the value —
  update the "velocity is ignored" comments here, in `app.h`, and in
  `usb-midi.md` §5 (this doc supersedes that note).
- The **CV free-run path** (`wt_osc_set_pitch`) must keep gating exactly as
  today: it re-gates only on a fresh note, never per pitch update, so glides
  don't retrigger the envelope.
- A **bypass** (`filter enabled: off`) preserves today's exact render path —
  the safe default until the feature is proven, and the A/B for CPU and
  sound checks.

### 3.5 Velocity mapping

Velocity is normalised (`v = vel/127.0f`) and routed to **exactly one
destination**, selected by the `VelMode` parameter (§4.1 row 8). The modes,
with `S` = the configured sustain level:

| Mode | Velocity drives | Mapping |
|---|---|---|
| **Volume** (default) | voice level | `gain × v^1.5` (perceptual curve) |
| **AtkAmt** | the A/D transient above sustain | `env' = S + (env − S)·v` for `env > S`; sustain & release unchanged |
| **AtkDec** | the whole contour, sustain included | `env' = env·v` |
| **FltEnv** | the cutoff sweep depth | `fc = base × 2^(EnvAmt·env·v)`; level follows the raw envelope |
| **AtkTime** | attack time | `t = attack_ms × (1−v)`, clamped ≥ 1 ms — harder = snappier |

- The routing is exclusive: in every mode but Volume, loudness follows the
  (possibly velocity-shaped) envelope only. AtkAmt/AtkDec shape the shared
  contour, so they are heard on both brightness and level.
- Per-note values (`vel_gain`, attack coef) are baked at note-on on the main
  loop; a mode or attack edit rebakes sounding voices so it is heard
  immediately. AtkAmt/AtkDec/FltEnv shaping runs at block rate in the render.
- Velocity curve selection (soft/linear/hard) is a later phase; linear
  scaling first.

---

## 4. Parameters & UI

### 4.1 Parameters

| # | Param | Range | Default |
|---|---|---|---|
| 0 | Filter | off / LP | LP |
| 1 | Cutoff (base) | 20 Hz – 20 kHz, log | 400 Hz |
| 2 | Resonance | 0 – 100 % | 15 % |
| 3 | Env amount | 0 – +7 oct | +4 oct |
| 4 | Attack | 1 ms – 5 s, log | 5 ms |
| 5 | Decay | 5 ms – 10 s, log | 300 ms |
| 6 | Sustain | 0 – 100 % | 60 % |
| 7 | Release | 5 ms – 10 s, log | 200 ms |
| 8 | VelMode | Volume / AtkAmt / AtkDec / FltEnv / AtkTime (§3.5) | Volume |

Defaults are chosen so plugging in a keyboard immediately gives the classic
"harder = louder" response with the envelope sweeping the filter at full
depth. Live pot → cutoff (à la user-interface.md §3 pot handling) is a
candidate follow-up, not phase 1.

### 4.2 Menu placement

The Config submenu (`render_config_menu()`) gains a third entry:
`Arp Cfg / Audio Cfg / Filter Cfg`, opening a new `APP_SCREEN_FILTER_CFG`.
The envelope lives on this screen too — it is the *filter's* contour (§1),
so a separate "Env Cfg" screen would split one instrument-feature across two
menus for no gain.

### 4.3 Filter Cfg screen

Same anatomy as Arp Cfg: 14 px header bar ("Filter Cfg"), one `Name: value`
row per parameter (Font12, 15 px pitch). Nine parameters don't fit the
~7 rows below the header, so the list **scrolls in a window** exactly like
the wavetable browser (`render_browse()` keeps the cursor centred and clamps
the window at the ends). All nine params — including the four envelope rows —
are plain list rows; nothing modal beyond the shared edit state.

Row rendering follows the Arp Cfg convention:

```
Filter:  LP
Cutoff:  400 Hz
Res:     15%
EnvAmt:  +4.0 oct
Attack: [5 ms]        <- cursored + editing: inverted bar, value in brackets
Decay:   300 ms
Sustain: 60%
                       (Release, VelMode scrolled below)
```

### 4.4 Encoder controls

Identical interaction grammar to Arp Cfg (`APP_SCREEN_ARP_CFG` handling in
`app_tick`), so nothing new to learn:

| Input | Not editing | Editing |
|---|---|---|
| rotate | move cursor (wrap) | adjust value, one step per detent |
| short press | on/off row (Filter): toggle in place, like `AP_CLK`; value rows (VelMode included): enter edit (brackets on) | confirm, leave edit |
| long press | back to Config | leave edit first (stay on screen) |

Value steps per detent:
- **Cutoff**: multiplicative, 12 steps/octave (≈ semitone resolution, ~120
  detents end-to-end; fine enough to dial a formant, coarse enough to sweep).
- **Attack / Decay / Release**: multiplicative ×1.12 per detent (~72/62/62
  detents across their log ranges), snapped for display.
- **Resonance / Sustain / Env amount**: linear — 2 %, 2 %, 0.25 oct.

Display formatting: times < 1 s as integer ms (`5 ms`, `300 ms`), ≥ 1 s as
seconds with one decimal (`1.5 s`); cutoff < 1 kHz as Hz, above as kHz with
one decimal (`2.4 kHz`).

### 4.5 Live edits

Edits apply **immediately** to sounding voices — hold a chord and dial the
release or sweep the cutoff to hear it. Mechanically free with §2's design:
the UI writes the shared `adsr_params_t` / base-cutoff / res from the main
loop; the render reads them at block rate, and single-float writes are
atomic on the M4 so no critical section is needed (same discipline as
`s_level`). The one exception is `Filter: off→on/on→off`, which hard-cuts
all sounding notes and resets per-voice envelope + SVF state under the
usual short `__disable_irq()` — the two render paths must not inherit each
other's state (a note carried across would jump or swell in level), so the
toggle trades a momentary silence for determinism.

### 4.6 Envelope preview (step-4 polish, optional)

When the cursor sits on an envelope row, draw a small ADSR shape
(~120 × 24 px, `Paint_DrawLine` segments) in place of the hint line at the
bottom: attack ramp, decay curve to the sustain plateau, release tail, with
the segment being edited drawn thicker. Cheap to render, and makes the four
time/level numbers legible as one shape. Nice-to-have — the numeric rows are
the deliverable.

---

## 5. Host tests (`firmware/test/`)

`test_adsr.c` and `test_svf.c`, compiled with the modules directly (as
`test_arp` does). Property-style assertions with tolerances — no
sample-exact golden vectors:

**ADSR**: attack reaches ≥0.99 within the configured time ±10 %; decay
settles to sustain; release decays to idle and `adsr_idle()` flips; retrigger
mid-release starts from current level (no discontinuity > step size); output
always in [0, ~1]; zero-length attack doesn't divide by zero.

**SVF**: DC in → DC out unchanged at any cutoff (LP passes DC); signal well
above cutoff attenuated (sine at 8×fc down ≥ 20 dB); silence in → decays to
silence (stability, no NaN/denormal blowup) across the full fc × res corner
grid, including per-block cutoff jumps from 20 Hz to 20 kHz (modulation
stability — the property TPT buys us); resonance boosts a sine at fc
monotonically with `res`.

---

## 6. CPU budget — measured, not assumed

The render already needs `O3` to meet the 667 µs block deadline.

- **Measurement (implemented)**: `wt_render()` is timed with the DWT cycle
  counter; `wt_osc_render_max_us()` returns the worst block since the last
  call. The Filter Cfg screen shows it live, right-aligned in the header —
  play a 4-note chord with the filter on and read the number.
- Budget target: ≤ 50 % of the block period (≤ ~330 µs) at 4 voices, leaving
  room for the chord engine and future mod sources. Record the measured
  worst case here after hardware bring-up: **TBD µs**.
- **Fallback if over budget** (in order): tan approximation instead of
  `tanf`; single paraphonic SVF on the mix (envelope from the newest note's
  velocity); reduce to 2× oversampled internal rate. Decision recorded here.

---

## 7. Later phases (out of scope for the envelope filter)

- **Filter types**: BP / HP outputs of the same SVF (free), LP/BP/HP morph.
- **Separate filter & amp envelopes** (two ADSRs per voice).
- **Keytracking**: cutoff follows note pitch (0 / 50 / 100 %).
- **CV-IN → cutoff**: `analog_in` CV modulates base cutoff — the Eurorack
  move; needs block-rate smoothing of the CV value.
- **Arp accent → velocity**: per-step velocity pattern in `arp.c`.
- **Drive**: soft saturation into the filter input.
- **LFO → cutoff**: shared LFO module, also useful for pitch/wavetable mod.

---

## 8. Implementation order

1. **`adsr.c` + host tests** — pure module, no firmware wiring. ✔ done
2. **`svf.c` + host tests** — pure module, includes the modulation-stability
   corner grid. ✔ done
3. **`wt_osc` integration** — voice struct, render loop, note lifecycle
   (release/steal semantics), velocity API + `app.c` plumbing, bypass
   switch. ✔ done — pending on-hardware verification: no clicks on
   note-off, velocity audibly opens the filter, CV free-run unaffected,
   block time within budget (§6).
4. **Filter Cfg screen** — Config submenu entry, scrolling param list with
   the cursor/edit encoder grammar, live-edit semantics (§4). ✔ done —
   the envelope preview (§4.6) not implemented yet. Note: parameters reset
   to defaults at boot; nothing in the firmware persists settings today
   (FatFs is compiled read-only), matching the arp/audio config screens.
5. Doc sweep: update `usb-midi.md` §5, `wt_osc.h` header comment, README
   feature list. ✔ done
