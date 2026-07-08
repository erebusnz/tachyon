# USB-MIDI Device — play the wavetable synth from a USB-MIDI source

This document specifies the **USB MIDI** operating mode: the Tachyon
enumerates over its WeAct USB-C port as a **class-compliant USB-MIDI
device**, and a host (a PC running a DAW, or any USB-MIDI controller
routed through one) plays the polyphonic wavetable voice over USB.

The note sources are the Key wheel, the Chord wheel, and USB MIDI.
CV-IN (`cv-input.md`) is a modulation input only — it is not a harmonic
source. CV *output* (`CV-OUT-A`, gates) is driven by the Key/Chord arp
modes.

Source references / modules this builds on:
- `Core/IO/wt_osc.c` / `wt_osc.h` — polyphonic wavetable voice; the
  `wt_osc_note_on()` / `wt_osc_note_off()` pair is documented for exactly
  this "incremental / MIDI-style" use.
- `harmony-engine.md` — the `mode = (source × engine)` pipeline this
  plugs a new source into.
- `Core/UI/app.c` — the menu / mode state machine.
- `USB_DEVICE/` — the existing ST USB **device** stack (currently CDC).
- `power-supply.md` §5 — the WeAct onboard 3V3 LDO and how the board is
  powered; relevant to the "device, not host" power note below.

---

## 1. Why device-mode (and not host)

Three options were weighed for getting note data in:

| Option | HW change | FW effort | Standalone | Keeps debug |
|---|---|---|---|---|
| **USB-MIDI device → PC** (chosen) | none | small | ✗ needs PC | flag (§3) |
| TRS/DIN MIDI in (UART + optocoupler) | 1 jack + opto | small | ✓ | ✓ |
| USB host → keyboard | VBUS rework | large | ✓ | ✗ |

Device-mode wins on effort: the port is **already** a USB device, so
there is no enumeration code to write (the host enumerates *us*), no
VBUS sourcing problem, and no Type-C CC rework. The cost is that a
computer has to sit in the signal path. TRS-MIDI-in remains the cheapest
path to a *standalone* instrument and is kept on the table as a future
addition; it is orthogonal to this spec (different transport, same voice
routing in §5).

---

## 2. Hardware — no change

The WeAct USB-C port stays wired exactly as drawn (device/sink):

- `PA11`/`PA12` = `USB_OTG_FS_DM`/`DP` — unchanged; the OTG core stays in
  **device** role.
- `CC1`/`CC2` 5.1 kΩ pulldowns (Rd) — **correct** for a device.
- VBUS is an **input**, supplied by the host PC. The board's own power
  comes from the Eurorack buck (`+5 V → WeAct VIN`); the WeAct's `D4`
  Schottky isolates the PC's VBUS from the local 5 V rail, so the two
  5 V sources coexist exactly as they already do today whenever the CDC
  debug console is plugged in. No new power work.

(For contrast: *host* mode would have required driving 5 V out onto the
receptacle VBUS pin to power a keyboard — feeding the buck to the
connector-side VBUS net, not the post-`D4` VCC node — plus Rd→Rp. None
of that applies here.)

---

## 3. USB personality + the `USB_SERIAL_DEBUG` build flag

The OTG-FS port is a **single** USB device — it can be CDC *or* MIDI, not
both (a composite CDC+MIDI device was considered and rejected: the IAD
descriptor and shared endpoint dispatch are the most error-prone part of
the whole feature, and they only buy USB-side debug). The two
personalities are selected at **compile time**:

| `USB_SERIAL_DEBUG` | USB enumerates as | `printf` goes to | Use |
|---|---|---|---|
| **0 (default)** | MIDI device (this spec) | **SWO / ITM** | normal / play |
| **1** | CDC virtual COM port (today's behaviour) | USB CDC | bench debugging |

**CMake option** (in `firmware/CMakeLists.txt`):
```cmake
option(USB_SERIAL_DEBUG "USB enumerates as a CDC serial console instead of the MIDI device" OFF)
if(USB_SERIAL_DEBUG)
    target_compile_definitions(${CMAKE_PROJECT_NAME} PRIVATE USB_SERIAL_DEBUG=1)
endif()
```

**Code gating** — three `#if USB_SERIAL_DEBUG` sites:
- `MX_USB_DEVICE_Init` (`usb_device.c`): register `USBD_CDC` + the CDC
  interface, **else** register `USBD_MIDI`.
- `usbd_desc.c`: device-descriptor class triple
  (`0x02/0x02/0x00` CDC **vs** `0x00/0x00/0x00` "class at interface" for
  MIDI) and the product string (`"…Virtual ComPort"` vs `"Tachyon MIDI"`).
- `_write` (`main.c`): the CDC `while(CDC_Transmit_FS()==BUSY)` path
  **vs** `ITM_SendChar()` (SWO). The CDC retarget must move behind the
  flag regardless, because `CDC_Transmit_FS` does not exist in the MIDI
  build.

**Build-flash step** — a `-UsbSerialDebug` switch on `flash.ps1`:
```powershell
./flash.ps1 -Build                 # MIDI device build (default)
./flash.ps1 -Build -UsbSerialDebug # CDC serial-console build
```
The switch configures the flag and builds into a dedicated
`build/Debug-usbserial` directory so the two variants don't thrash each
other's CMake cache, then flashes that ELF over SWD. (If the `build/`
dirs turn out to be CubeIDE-configured rather than plain-`cmake`, the
flag falls back to a one-line `#define` in a small `usb_personality.h`
header — same compile-time switch, no reconfigure needed.)

**Debug pin:** SWO is `PB3` (TRACESWO); confirm it is broken out on the
WeAct header and that the ST-Link supports SWV. If not, the SWO branch
can instead retarget to a spare UART, or be stubbed (debugger-only).

---

## 4. MIDI device descriptors (USB-MIDI 1.0, MIDI-only)

A standard class-compliant USB-MIDI 1.0 device. Endpoint budget on
OTG-FS FS (EP0 control + EP1) — only one bulk pair is needed, so this
fits comfortably:

| Endpoint | Dir | Type | Use |
|---|---|---|---|
| EP0 | IN/OUT | Control | enumeration / standard requests |
| EP1 | OUT | Bulk (64 B) | host → device MIDI (**the notes we play**) |
| EP1 | IN | Bulk (64 B) | device → host MIDI (declared for compliance; unused initially) |

**Config descriptor layout** (~101 bytes):
```
Configuration
├─ Interface 0  AudioControl (class 0x01, subclass 0x01)
│   └─ CS AC header → references the MIDIStreaming interface
└─ Interface 1  MIDIStreaming (class 0x01, subclass 0x03)
    ├─ CS MS header
    ├─ MIDI IN  Jack  (Embedded)   id 1
    ├─ MIDI IN  Jack  (External)   id 2
    ├─ MIDI OUT Jack  (Embedded)   id 3  ← sourced from External-IN  (2)
    ├─ MIDI OUT Jack  (External)   id 4  ← sourced from Embedded-IN  (1)
    ├─ Bulk OUT endpoint (EP1 OUT) + CS-endpoint → Embedded-IN jack 1
    └─ Bulk IN  endpoint (EP1 IN)  + CS-endpoint → Embedded-OUT jack 3
```

Device descriptor: `bDeviceClass/SubClass/Protocol = 0x00/0x00/0x00`
(class defined per-interface), VID `0x0483` (ST), a project PID,
product string `"Tachyon MIDI"`. The class lives in a new
`USB_DEVICE/App/usbd_midi.c` + `.h` implementing `USBD_ClassTypeDef`
(`Init`/`DeInit`/`Setup`/`DataIn`/`DataOut`/`GetCfgDesc`), registered in
place of `USBD_CDC` when the flag is off.

---

## 5. MIDI parsing → voice routing

USB-MIDI delivers **32-bit USB-MIDI Event Packets** on the bulk-OUT
endpoint — each packet is 4 bytes, and a single bulk transfer may carry
several:

```
byte 0:  [ Cable Number (4b) | Code Index Number (4b) ]
byte 1:  MIDI status   (e.g. 0x90 = Note On ch 1)
byte 2:  MIDI data 1   (note number)
byte 3:  MIDI data 2   (velocity)
```

The OUT callback (in `usbd_midi.c`) walks the buffer in 4-byte strides
and dispatches by **Code Index Number** (low nibble of byte 0):

| CIN | Meaning | Action |
|---|---|---|
| `0x9` | Note On | `vel==0` → note-off, else **note-on** |
| `0x8` | Note Off | **note-off** |
| `0xB` | Control Change | (future: sustain, mod) |
| `0xE` | Pitch Bend | (future) |
| other | — | ignored |

```c
for (; len >= 4; p += 4, len -= 4) {
    uint8_t cin = p[0] & 0x0F;
    uint8_t note = p[2], vel = p[3];
    if      (cin == 0x9 && vel) midi_evt_push(MIDI_NOTE_ON,  note, vel);
    else if (cin == 0x9)        midi_evt_push(MIDI_NOTE_OFF, note, 0);
    else if (cin == 0x8)        midi_evt_push(MIDI_NOTE_OFF, note, 0);
}
```

**Threading.** The OUT callback runs in the **OTG IRQ**. Rather than call
the oscillator directly from that context (it takes a short critical
section that also guards the audio render IRQ), parsed events are pushed
into a small **lock-free SPSC ring** and drained in `app_tick()` on the
main loop:

```c
/* app.c, in app_tick() before render */
midi_evt_t e;
while (midi_evt_pop(&e)) {
    if (e.kind == MIDI_NOTE_ON)  app_midi_note_on(e.note, e.vel);
    else                         app_midi_note_off(e.note);
}
```

**The app seam** (declared in `app.h`, called only from the drain loop):
```c
void app_midi_note_on(uint8_t note, uint8_t vel);
void app_midi_note_off(uint8_t note);
```
Both are **gated**: they act only while `s_mode_active && s_src ==
SRC_MIDI`. Mapping:
- `app_midi_note_on(n,v)` → `wt_osc_note_on(n, midi_freq(n), v)` (the
  voice pool reuses the note's own voice on a re-strike, else allocates
  a free voice, else the quietest releasing one, else steals the oldest).
- `app_midi_note_off(n)`  → `wt_osc_note_off(n)` (starts the voice's
  envelope release; with the filter off, an immediate cut).

**Velocity** drives the per-voice envelope filter (filters.md §3.5): it
scales the cutoff sweep depth and — when `Vel→Amp` is on — the voice
level via `(vel/127)^1.5`.

Outside the mode (or before a mode is picked), events are still drained
and discarded, so stale notes never leak when you switch into USB MIDI.

---

## 6. App / menu integration

The mode matrix (`harmony-engine.md` §2) carries a USB-MIDI source on
the **Direct** engine, polyphonic:

| Source \ Engine | **Direct** | **Arp** |
|---|---|---|
| **Key wheel** | (freebie) | **Key Arp** |
| **Chord wheel** | — | **Chord Arp** |
| **USB MIDI** | **Midi Synth — poly, play-as-received** | — |

Target `app.c` / `app.h` structure:

- `pitch_src_t` includes `SRC_MIDI`.
- `AppScreen` / `k_mode_items[]` carry `APP_SCREEN_USB_MIDI` /
  `"USB MIDI"`. Modes stay contiguous so the menu-index → screen mapping
  is `APP_SCREEN_FIRST_MODE + selected`.
- `engine_tick()`: `if (s_src == SRC_MIDI) return;` — notes are
  event-driven from the drain loop, not polled from a root.
- Entering the mode calls `set_mode(SRC_MIDI, ENG_DIRECT)` (arp disabled,
  `wt_osc_all_off()` for a clean start).
- `render_usb_midi()` screen: title, USB connect state (enumerated /
  not), and a live count of sounding voices (or the last note name).

The USB connect state for the screen comes from a flag the class sets in
its `Init`/`DeInit` callbacks (`usbd_midi` exposes `usb_midi_connected()`).

---

## 7. Phased build plan

1. **App/menu refactor** — drop CV VCO/CV Arp, add the `USB MIDI` mode,
   `SRC_MIDI`, the `app_midi_note_on/off` seam, the SPSC event ring, and
   the `render_usb_midi()` screen. Compiles and is UI-testable with **no
   USB code yet** (ring simply stays empty).
2. **Flag plumbing** — CMake `option`, `flash.ps1 -UsbSerialDebug`, and
   the conditional `_write` → SWO/ITM retarget. After this the default
   build still enumerates CDC (until step 3) but `printf` is on SWO.
3. **MIDI device class** — `usbd_midi.c/.h` (descriptors §4 + the
   bulk-OUT parser §5 feeding the ring), registered under the flag in
   `MX_USB_DEVICE_Init`. Default build now enumerates as `Tachyon MIDI`.

Each step builds green; the feature is end-to-end after step 3.

---

## 8. Future / out of scope

- **Velocity → per-voice gain** (needs a `wt_osc` voice-gain extension).
- **CC / pitch-bend** (sustain pedal = CC64; pitch bend = CIN `0xE`).
- **MIDI channel filter** (play only a selected channel; today: omni).
- **MIDI clock in** (CIN `0xF`, 0xF8) driving the arp as an external
  clock source, unifying with `clock-input.md`.
- **TRS/DIN MIDI-in** (UART + optocoupler) as a standalone transport
  feeding the same `app_midi_note_on/off` seam — no PC required.
- **Device → host MIDI** (the declared EP1-IN): echo played notes / arp
  steps back to the DAW.
