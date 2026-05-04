---
name: pcb-designer
description: Audit the Tachyon multi-board PCB design (front panel + IO + backing/MCU+audio) by cross-checking the design markdown files (pcb-design.md, power-supply.md, audio-output-dac.md, etc.) against per-board EasyEDA Pro exports — Allegro Telesis netlist (.tel), pick-and-place CSV, and Gerber files. Includes a cross-board interface pass that verifies net continuity across mating pin headers and through-PCB components (pots, 3.5mm jacks, encoders). Use whenever the user asks to "review the layout", "check the PCBs against the design", "verify net X is routed correctly across boards", "see if placement matches", "check the inter-board connector pinout", or after a fresh EasyEDA Pro export. Also use proactively when the user mentions DRC, gerber output, BOM/PnP, layer stackup, ground plane integrity, decoupling placement, board-to-board headers, or wants pcb-design.md updated to match what was actually built.
---

# PCB Designer — design-vs-reality audit

## What this skill is for

The design markdown files (`pcb-design.md`, `power-supply.md`,
`audio-output-dac.md`, `cv-output-dac.md`, `cv-input.md`, `clock-input.md`,
`gate-output.md`, `user-interface.md`, `calibration.md`,
`hardware-design-plan.md`) describe the **intended** Tachyon design:
which rails feed which ICs, where components go, how the ground plane
is treated, how nets are named, what trace widths to use, etc.

The Tachyon module is built from **three PCBs stacked together**:

| Board name        | Role                                                       | Stackup | Has electrical netlist? |
|-------------------|------------------------------------------------------------|---------|-------------------------|
| `front-board`     | Front panel: silkscreen art, OLED window, hardware mounts. Purely mechanical/decorative — no components, no nets. | 2-layer | no  |
| `io-board`        | Pots, 3.5 mm jacks, encoder, panel-side passive op-amp circuitry, four headers up to backing | 4-layer | yes |
| `backing-board` (a.k.a. `audio-mcu-board`) | TPS54202 buck, LDOs, STM32F405 module, PCM5102A, DAC8552, REF5025, OPA1642, four headers down to IO | 4-layer | yes |

**Naming hazard.** Three different conventions for the same board are
in play in this project — handle all of them:

| Source                                                       | front          | io             | backing                |
|--------------------------------------------------------------|----------------|----------------|------------------------|
| Gerber sub-directory (`gerber/<dir>/`)                       | `front-board`  | `io-board`     | `audio-mcu-board`      |
| Telesis netlist filename (`gerber/Netlist_<X>-schematic_*.tel`) | `front-board`  | `io-board`     | `backing-board`        |
| Pick-and-place filename (`gerber/PickAndPlace_<X>_*.csv`)    | `Front`        | `IO`           | `Backing`              |

Treat these as aliases for the same physical board. The cross-board
script keys on the Telesis filename's stem (`front-board`, `io-board`,
`backing-board`); when correlating with gerber dirs, map
`backing-board` ↔ `audio-mcu-board` explicitly.

The boards are mechanically and electrically joined by two distinct
mechanisms:

1. **Front ↔ IO**: through-PCB hardware. Potentiometer shafts, 3.5 mm
   socket nuts, encoder shafts, and LED leads pass through *both* the
   front-board and the io-board. The component is electrically anchored
   on the io-board side; the front-board has the matching footprints
   (pads or NPTH cutouts) for mechanical alignment and panel hardware.
   No pin headers between these two.
2. **IO ↔ audio-mcu**: pin headers (male on one side, female on the
   other). All inter-board electrical signals (power rails, I2S, SPI,
   CV, gate, clock, encoder lines) cross this single connector pair.

For each of the three boards, EasyEDA Pro produces the same triple of
export files:

- **`*.tel`** — Allegro/Telesis netlist. Source of truth for *electrical*
  connectivity and footprint assignment, **per board**.
- **Pick-and-place CSV** (`*Pos*.csv` / `*PnP*.csv`) — XY position,
  rotation, and layer for every component on that board.
- **Gerber zip** (`gerber/Gerber_<Board>_*.zip` and the unpacked
  `gerber/<board>/` directories) — RS-274X copper/mask/silk geometry
  plus Excellon drill files for that board.

The audit therefore has two scopes:

- **Per-board scope** (Passes 1-6) runs against one board's triple at a
  time and checks intra-board rules (stackup, decoupling, zones,
  routing).
- **Cross-board scope** (Pass 7) runs against all three triples
  simultaneously and checks the inter-board interfaces (header pinout
  match, through-PCB component alignment, net continuity).

Design ↔ reality divergences arise any time:

- the EasyEDA Pro project of any board is edited but the markdown is
  not updated, or
- the markdown is updated as a plan but a layout has not caught up, or
- a placement / routing decision silently violates a rule the markdown
  declares (e.g. GND plane gets cut, wrong rail crosses under the DAC,
  decoupling cap moves >2 mm from its IC), or
- an inter-board net is renamed on one side but not the other, breaking
  the header-pair invariant.

This skill's job is to find the divergences, classify each one as
*"markdown is stale"* vs *"layout is wrong"*, and then update
`pcb-design.md` (or the relevant per-block markdown) for the stale
cases. Layout-wrong cases are reported as a list for the user to fix
in EasyEDA Pro — never edit any `.eprj` SQLite database directly.

## When to invoke

Trigger this skill on phrases like:

- "audit the PCB", "check the layout", "review the board",
  "verify the design"
- "does this match `pcb-design.md`?", "is the ground plane intact?",
  "is U12 in Zone A?"
- "regenerate `pcb-design.md` from the gerbers", "update the design doc
  to match the board"
- after the user mentions a fresh export, e.g. "I just re-exported the
  IO board gerbers"
- when the user references the `.tel`, position file, or BOM/PnP

Also trigger when the user asks for a DRC-style sanity pass even if they
don't name the files — the skill should find them.

---

## Inputs and how to obtain them

Actual file layout in this repo (flat — all per-board exports live
side-by-side directly inside `gerber/`):

```
gerber/
├── Gerber_Front_<date>.zip
├── Gerber_IO_<date>.zip
├── Gerber_Backing_<date>.zip
├── Netlist_front-board-schematic_<date>.tel    # may be empty for cosmetic boards
├── Netlist_io-board-schematic_<date>.tel
├── Netlist_backing-board-schematic_<date>.tel
├── PickAndPlace_Front_<date>.csv               # UTF-16 LE BOM, TAB-separated
├── PickAndPlace_IO_<date>.csv
├── PickAndPlace_Backing_<date>.csv
├── front-board/        # unpacked gerbers, dir name follows gerber-dir convention
├── io-board/
└── audio-mcu-board/    # backing board's gerbers live here under the legacy name
```

The gerber zips and unpacked directories are committed. The `.tel` and
pick-and-place files are typically **regenerated each time** the user
exports — they carry the export date in the filename, so older
copies may linger; always pick the most recent timestamp per board.

To enumerate the boards to audit, scan `gerber/Netlist_*-schematic_*.tel`
and pick the newest file per board name. To find the matching gerber
directory and PnP CSV:

| Telesis stem    | Gerber dir         | PnP filename glob                |
|-----------------|--------------------|----------------------------------|
| `front-board`   | `gerber/front-board/`     | `gerber/PickAndPlace_Front_*.csv`     |
| `io-board`      | `gerber/io-board/`        | `gerber/PickAndPlace_IO_*.csv`        |
| `backing-board` | `gerber/audio-mcu-board/` | `gerber/PickAndPlace_Backing_*.csv`   |

If the user invokes the skill with no further qualifier, audit *all*
boards and run the cross-board pass. If they name a single board
("audit the IO board"), run only Passes 1-6 on that board and skip
Pass 7. Boards whose `.tel` parses to zero packages (e.g. the
front-board, which is purely decorative) skip Passes 2/3/5 entirely
with a note "no electrical content".

**Exporting a missing `.tel`**

1. Open `tachyon.eprj` in EasyEDA Pro.
2. Switch to the board to audit.
3. *Design → Export → Netlist* → format *Allegro / Telesis*.
4. Save to `gerber/Netlist_<board>-schematic_<date>.tel`.

**Exporting a missing PnP CSV**

1. *Manufacture → Pick-and-Place* (or *Position File*).
2. Units: mm. Origin: board origin (the parsers tolerate either, but
   prefer corner-origin so the values line up with the gerber outline).
3. Save to `gerber/PickAndPlace_<Board>_<date>.csv`.

**Re-extracting gerbers** if the zip in `gerber/` is newer than its
unpacked directory:

```bash
cd gerber/<dir> && unzip -o ../Gerber_<Board>_*.zip
```

Confirm with the user whether to re-export anything before continuing.
Don't fabricate placeholder data — if a file is missing for a board
and the user declines to export, report which checks become impossible
for that board and run only the ones whose inputs you have. The
cross-board pass needs `.tel` files for every board with electrical
content (so two of three for this design); if any are missing, skip
Pass 7 with an explicit note.

---

## File-format quick reference

The references in `references/` cover the parsers in detail. At a glance:

- **`.tel`** — plain text, two main sections delimited by `$PACKAGES` and
  `$NETS`, terminated by `$END`. Packages map designator → footprint +
  value. Nets list the (ref, pin) pairs they connect. Single-pin "nets"
  are no-connects. See `references/tel-format.md`.

- **PnP CSV** — header row, then one row per component. Columns vary
  slightly between EasyEDA Pro template versions; always look at the
  header. Typical names: `Designator`, `Footprint`, `Mid X`, `Mid Y`,
  `Rotation`, `Layer` (`T`/`B` or `top`/`bottom`). Coordinates are mm
  unless the file says otherwise. See `references/pnp-format.md`.

- **Gerber RS-274X** — text. Each layer is its own file (`.GTL` top
  copper, `.GBL` bottom, `.G1`/`.G2` inner, `.GTO`/`.GBO` silk,
  `.GTS`/`.GBS` mask, `.GTP`/`.GBP` paste, `.GKO` outline). Drill is
  Excellon (`.DRL`). Coordinate format declared in `%FSLAX45Y45*%`
  (here: 4 integer + 5 decimal digits, in mm per `%MOMM*%`). Apertures
  are declared with `%ADD<n><shape>,...*%`. See
  `references/gerber-format.md`.

---

## Audit workflow

Work through these passes in order. Passes 1-6 run **per board**; loop
over each board and accumulate findings under that board's heading.
Pass 7 runs **once across all boards** and looks at the inter-board
interfaces. Pass 8 is the final write-up. Collate everything at the end
into the report described in **Reporting**.

### Pass 1 — Layer / stackup sanity (Gerber only)

For the current board's gerber directory:

1. List the gerber filenames present. Expected per-board stackup:
   - `audio-mcu-board`: 4-layer — `GTL`, `G1`, `G2`, `GBL` plus mask /
     silk / paste / outline / drill.
   - `io-board`: 4-layer — same as above.
   - `front-board`: 2-layer — `GTL`, `GBL` plus mask / silk / outline /
     drill (no inner layers).
   If a board's actual layer file count disagrees, that's a stackup
   change and either `pcb-design.md` or the project needs reconciling.
2. Cross-check against `pcb-design.md` Section 2 ("Stackup"): it
   declares 4 layers (TOP / GND / PWR / BOTTOM) for the audio-mcu
   board (and by extension the io-board, since `pcb-design.md` Rule 1
   is global). The presence and count of `G1`/`G2` files is the first
   evidence.
3. Determine the expected role of each inner layer for this board.
   Codified in `run_audit.py`'s `BOARD_INNER_LAYERS` dict
   (per-board `{".G1": "GND" | "split", ".G2": ...}`); the script
   then checks the gerber against that expectation. The expectation
   itself comes from the design doc — keep them in sync.
4. Plane-integrity rules driven by that expectation:
   - A GND-poured layer with `copper_areas=0` *and* no `G36*` regions
     → the plane was never poured. Layout error.
   - A GND-poured layer with `copper_areas>1` (multiple PlaneZones) →
     plane is split. Layout error if the doc says it should be
     continuous.
   - A "split" power layer with `copper_areas` lower than the rail
     count → some rails are not poured (probably routed as traces).
     Cross-reference top/bottom-layer trace presence to decide if
     intentional.

### Pass 2 — Net inventory and rail names (Telesis netlist)

1. Run `scripts/parse_tel.py <board>.tel` (or invoke through
   `run_audit.py`). The parser surfaces two tables: designators with
   their footprint, and net names with their (designator, pin)
   members.
2. Compare the set of net names to the rail set declared for this
   board in the design markdown. The expected per-board rail set is
   codified in `run_audit.py`'s `EXPECTED_RAILS` dict — keep that in
   sync with whatever the design doc currently says.
   - Extra rails not in the docs → either the doc is stale (add it) or
     the project added a stray net (likely a wiring mistake).
   - Rails declared in the doc but absent from the netlist → either
     the name was renamed in EasyEDA (update the doc) or the rail was
     never wired (layout error).
3. Verify the *one ground net* rule. The standard topology used here
   has exactly one `GND` net; the presence of `AGND` / `DGND` /
   `PGND` is a finding because it implies a split-plane strategy the
   layout doctrine has rejected. (Re-read `pcb-design.md` §3 for the
   reasoning.)
4. Optional: doc-defined per-pin assignments. If the design markdown
   pins down specific net-to-IC-pin assignments (e.g. "rail X feeds
   pin Y of IC Z", "pin Y of IC Z must be tied to GND for PLL mode"),
   those are codified in `run_audit.py`'s `EXPECTED_IC_PINS` dict.
   Mismatches typically point to stale pin numbers in the doc; cross-
   reference with the IC's local datasheet markdown to decide which
   side is wrong.
5. **Eurorack power-bus header check** when the board hosts a 10/16-pin
   IDC bus connector. See `references/eurorack-power.md` for the
   standard pinouts. The script checks pin count, rail-pair adjacency
   at the two ends, that the middle pins are all GND, and that no
   other rail leaks onto the bus connector (a `+5V` or `+3V3` pin
   there would back-power the rack bus). Report the header's PnP
   centre / rotation and ask the user to confirm silk-stripe
   alignment visually in the EasyEDA Pro 2D view.

### Pass 3 — Per-IC decoupling (Telesis + PnP)

For each IC on the board:

1. From the `.tel`, identify caps connected between any of the IC's
   power pins and GND.
2. From the PnP, measure the centre-to-centre distance from each cap
   to the IC. Compare against the threshold from the design doc
   (currently `DECOUPLING_DIST_MAX_MM` in `run_audit.py`, mirroring
   `pcb-design.md` §5's "≤ 2 mm pin-to-cap" rule expressed in
   centre-to-centre form for typical 0805 geometry).
3. Confirm the cap bridges the *correct* rail to GND, not a different
   one.

Sub-modules with onboard decoupling (e.g. a daughter-board MCU
module with its own LDO and bypass network) are exempted via the
`MODULES_INTERNAL_DECOUPLE` set — those don't need per-pin caps on
the main PCB.

### Pass 4 — Placement and spacing (PnP only)

The audit does **not** enforce cardinal-quadrant zone assignments
(e.g. "this IC must be in Zone C"). Those rules go stale the moment
the layout iterates, and they tend to flag false positives whenever
the designer has reorganised for a good reason that the doc hasn't
caught up with.

What the audit *does* enforce — and what the design docs *should*
codify — are **distance / spacing rules** that map to real engineering
invariants (e.g. noisy switch node away from sensitive analog;
precision-reference trace under some maximum length). The list of
rules lives in `run_audit.py`'s `SPACING_RULES`. Each rule is
`(board, ref_a, ref_b, op, threshold_mm, source_citation)`. The
script computes actual centre-to-centre distance from the PnP CSV
and flags failures.

Beyond spacing rules, Pass 4 simply **observes** where each key IC
sits (XY, side) and reports it in the audit MD. If the doc says
something prescriptive about layout (e.g. "MCU in top-right zone")
that contradicts the observation, that's a stale-doc finding
(Pass 8) — fix the doc to describe the actual layout, don't try to
make the script enforce a stale narrative.

Adding a new spacing rule means editing `SPACING_RULES` in
`run_audit.py` *and* writing the rationale into the relevant design
markdown — the script's rules are the operational form of the doc's
prose.

### Pass 5 — Sensitive-corridor occupancy (Telesis + PnP)

When the design doc identifies a "corridor" — a strip of board reserved
for a specific net family (e.g. fast digital crossing into analog
territory, or precision Vref) — verify that no foreign components
sit in that corridor.

Procedure:

1. From the `.tel`, list the endpoints of each corridor net family
   (e.g. I2S = `*BCK`, `*WS`, `*DATA`, `*MCLK`, `*SD`).
2. From the PnP, find non-corridor components whose XY falls inside
   the bounding box between the corridor endpoints.
3. Report endpoints and any intruders.

Whether a corridor needs *additional* mitigations (series termination,
ground-flanking, layer restriction, etc.) is a design-doc question,
not a generic skill check. Trace-length-vs-edge-rate analysis lives
in the design markdown for the specific corridor (see
`pcb-design.md` §5 for the I2S bridge analysis on this project) and
is not a hardcoded check in this skill.

### Pass 6 — Trace width spot-checks (Gerber)

This is the most expensive pass — only run if the user explicitly
asks, or if Pass 2 found a power-rail issue worth confirming.

1. Read the aperture table from `Gerber_TopLayer.GTL` and
   `Gerber_BottomLayer.GBL` (lines beginning `%ADD`). Each circular
   aperture (`C,<diameter>`) corresponds to a track width.
2. Identify which aperture is used for power tracks (typically the
   widest one used outside of pads).
3. Compare against the trace-width table in Section 6 of
   `pcb-design.md`. Note: this design prefers Layer 3 *pours* over
   wide traces, so a thin trace bridging a pour boundary is normal
   and expected.

### Pass 7 — Cross-board interface (all `.tel`s + PnP)

Runs once across all boards. Needs every board's `.tel`. Skip with a
note if any are missing.

Run `scripts/cross_board_audit.py gerber/` and read the report it
emits. The script does three things:

1. **Net intersection.** For every net name, list which boards it
   appears on. Three buckets:
   - On exactly one board → intra-board net (no cross-board concern).
   - On two or more boards → inter-board net. Must be carried by either
     a header pair (IO ↔ audio-mcu) or a through-PCB shared component
     (front ↔ IO). Verify which.
   - On a board that should have no exposure to that net → likely a
     net-name collision (two unrelated nets that happen to share a
     name); flag for renaming. Example: a local `RST` on each board
     that are not actually connected.
2. **Header detection.** For each board, find designators whose
   footprint matches a pin-header pattern (`HDR*`, `PINHDR*`,
   `2.54*`, `2.0mm*`, `1x*`, `2x*`, etc.; the script's pattern list
   is intentionally generous). Report them with pin → net mappings.
3. **Header-pair matching (IO ↔ audio-mcu).** For each candidate
   header pair, compare the per-pin net assignment:
   - Same pin count? If not, the headers don't mate.
   - Net names match position-for-position? If not, the pinout is
     inverted, mirrored, or off-by-one.
   - Considerations the script can't decide on its own and must hand
     to the user:
     - Whether the female connector's pin numbering runs the same
       direction as the male's. EasyEDA Pro's footprint library is
       inconsistent here; a mirrored header is a common bug.
     - Whether 2-row connectors (2x10, 2x20) have the row order
       expected by the mating side.

**Through-PCB shared components (front ↔ IO).** No headers — the
electrical interface is the lead pads of the panel hardware. The audit
here is mechanical-by-proxy:

1. For each through-PCB component family (potentiometer, 3.5 mm jack,
   encoder, panel LED), find the io-board designator that hosts the
   electrical pads.
2. Find the matching footprint on the front-board (usually NPTH holes
   or an unconnected outline-only footprint).
3. Compute the XY centre of each footprint on each board, in the
   board's own frame.
4. For the boards to align when stacked, the centres must match in
   the *common* coordinate frame. The two boards almost certainly
   don't share an origin, so ask the user to specify the alignment
   reference (typically: a fiducial, a mounting screw position, or
   the board edge nearest the audio-mcu connector). Without that
   reference the audit can only check *relative* spacing between
   pots/jacks, not absolute alignment. Report the relative spacing
   table; the user verifies absolute alignment in EasyEDA Pro's
   multi-board view.

**Cross-board rail integrity.** Every rail in the power-supply.md
table must:
- Originate on exactly one board (the one with the regulator), and
- Exit that board on a header pin, and
- Enter the destination board on the corresponding pin, and
- Reach every IC the design doc says it feeds.

The script flags any rail whose source board has no header pin
carrying it but which appears on a downstream board's IC.

### Pass 8 — Documentation reconciliation

Look at the design markdown files holistically. The previous passes
will have surfaced individual discrepancies; this pass asks the
broader question: does each design document still describe a coherent
plan?

Specific checks:

1. **Board roster.** Does `pcb-design.md` (or `hardware-design-plan.md`)
   list the three boards by name and describe their roles? If the
   project has gained or lost a board since the doc was written, the
   doc is stale.
2. **Stackup applicability.** `pcb-design.md` Section 2 declares a
   4-layer stackup. For boards that are 2-layer (the front-board), the
   doc should call out the exception explicitly. If it doesn't, add an
   "Exception: front-board" subsection.
3. **Inter-board interface table.** `pcb-design.md` should contain a
   pinout table for the IO ↔ audio-mcu header pair and a
   shared-hardware table for the front ↔ IO mechanical interface. If
   missing, propose adding them (this skill can generate them from
   Pass 7 output).
4. **Per-block doc consistency.** Every per-block markdown
   (`audio-output-dac.md`, `cv-input.md`, etc.) refers to specific
   designators (U7, U12, etc.). Verify each named designator exists in
   the right board's `.tel`. A reference to `U7` that resolves to two
   different parts on two different boards is a renumbering hazard
   from when the boards were split.

---

## Reporting

After running the passes, write a single report **into the chat**, not
to disk. Use this structure (omit per-board sub-sections for boards
that weren't audited; omit Pass 7 when only one board was requested):

```
# PCB audit — <date>

## front-board

### Layout errors (need EasyEDA Pro fix)
- <one-liner>. Evidence: <file>:<location>. Doc rule:
  pcb-design.md §<N>.

### Stale documentation (will update)
- <one-liner>. Doc says X; board has Y. Proposed edit: <diff snippet>
  in <file.md>.

### Verified clean
- <pass name>: <one-line note>

### Skipped
- <pass name>: <reason>

## io-board
... (same sub-structure)

## audio-mcu-board
... (same sub-structure)

## Cross-board (Pass 7)

### Inter-board net continuity
- <one-liner per discrepancy>

### Header pair: <io-board.J?> ↔ <audio-mcu-board.J?>
- <pin-by-pin diff or "verified clean">

### Through-PCB shared components (front ↔ io)
- <relative-spacing summary or alignment concern>

### Rail integrity
- <one-liner per rail problem>

## Documentation reconciliation (Pass 8)
- <proposed edits to pcb-design.md / per-block docs>
```

Write the report to **`gerber/audit-<board>.md`** alongside the
gerber exports, one file per board (so `gerber/audit-front-board.md`,
`gerber/audit-io-board.md`, `gerber/audit-backing-board.md`). They sit
next to the artefacts they're auditing — same directory as the `.tel`,
PnP, and gerber zips. The cross-board (Pass 7) findings go into each
board's file under a "Cross-board interface" section, with the two
boards' files cross-referencing each other rather than duplicating the
full pinout tables.

These audit MDs are **disposable artefacts** — overwrite them on each
audit run. Do not keep historical audit MDs around; use git history
if the user wants to compare runs.

Then, *only after* the user reviews the report and confirms the
stale-documentation edits, apply those edits with the Edit tool to
the relevant *design* markdown file (`pcb-design.md`,
`power-supply.md`, `audio-output-dac.md`, etc.). Don't pre-emptively
edit on the first pass — the user needs a chance to push back,
because sometimes "stale doc" is actually "the layout drifted and the
doc was right."

---

## What this skill does NOT do

- It does not run a real DRC (clearance, annular ring, soldermask
  expansion). EasyEDA Pro's built-in DRC is the right tool for that.
  This skill is a higher-level *intent-check*: does the layout match
  the documented strategy.
- It does not modify the EasyEDA Pro project file (`tachyon.eprj`).
  That's a SQLite database; treat it as opaque. All layout changes
  must go through EasyEDA Pro itself.
- It does not regenerate Gerbers. If the user wants fresh outputs they
  must export from EasyEDA Pro.
- It does not parse the `.eprj` directly. The Telesis netlist + PnP +
  Gerber exports are the contract; the SQLite schema is undocumented
  and version-coupled.

---

## Bundled resources

- `references/tel-format.md` — Allegro Telesis netlist syntax.
- `references/pnp-format.md` — EasyEDA Pro pick-and-place CSV columns.
- `references/gerber-format.md` — RS-274X subset used by this project.
- `references/eurorack-power.md` — Doepfer 10-pin and 16-pin power
  bus header pinouts; consulted by Pass 2 step 5.
- `scripts/parse_tel.py` — parse `.tel` into a Python dict
  (`{packages, nets}`). Standalone, stdlib only.
- `scripts/parse_pnp.py` — parse PnP CSV into a list of dicts.
  Auto-detects the column layout. Standalone, stdlib only.
- `scripts/inspect_gerber.py` — summarise a gerber directory: layer
  list, aperture counts, copper-area counts per layer, board bounding
  box from the outline file. Standalone, stdlib only.
- `scripts/cross_board_audit.py` — given a parent directory containing
  one subdirectory per board (each holding a `.tel`), report net
  intersections across boards, detected pin-headers per board, and
  candidate header-pair matches with pin-by-pin net diff. Standalone,
  stdlib only.

Read the reference files only when you need format details mid-audit;
the high-level workflow above usually suffices.
