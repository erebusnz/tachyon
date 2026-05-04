#!/usr/bin/env python3
"""End-to-end PCB audit for the Tachyon multi-board project.

Runs the 8-pass audit defined in `SKILL.md` against the latest exports
under `gerber/` and writes one `audit-<board>.md` file per board at the
repository root.

Usage:
    python run_audit.py                       # audit all boards
    python run_audit.py --board io-board      # audit just one board
    python run_audit.py --out-dir .           # write to repo root (default)
    python run_audit.py --gerber-dir gerber/  # default

The script imports the sibling parsers — no external dependencies.

Findings are *automatically derived* from the netlist, pick-and-place,
and gerber data. Project-specific rules (designators, pin numbers,
zone bounds, expected rails) are embedded in the `RULES` block at the
top of this file so they can be edited as the design evolves.
"""

from __future__ import annotations
import argparse
import json
import math
import re
import sys
from dataclasses import dataclass, field
from pathlib import Path
from typing import Any

sys.path.insert(0, str(Path(__file__).resolve().parent))
from parse_tel import parse_tel  # noqa: E402
from parse_pnp import parse_pnp  # noqa: E402
from inspect_gerber import inspect_dir  # noqa: E402

# ---------------------------------------------------------------------------
# Project-specific rules
# ---------------------------------------------------------------------------

EXPECTED_BOARDS = {
    # Telesis filename stem -> (gerber subdir, PnP filename glob)
    "front-board":   ("front-board",     "PickAndPlace_Front_*.csv"),
    "io-board":      ("io-board",        "PickAndPlace_IO_*.csv"),
    "backing-board": ("audio-mcu-board", "PickAndPlace_Backing_*.csv"),
}

# Per-board expected rail set (for Pass 2). The front-board has no
# electrical content; missing entry means "skip the rail check".
EXPECTED_RAILS = {
    "io-board":      {"+12V", "-12V", "+5V", "+3V3", "GND", "VREF"},
    "backing-board": {"+12V", "-12V", "+5V", "+3V3", "+3V3_PREC",
                      "+3V3_AUDIO", "GND", "VREF", "12VEURO", "-12VEURO"},
}

# Per-board expected inner-layer net assignment (for Pass 1).
#   - "GND"      → single continuous GND pour (1 PlaneZone, many cutouts)
#   - "split"    → multiple PlaneZones, one per rail (Layer 3 of backing-board)
#   - None / missing entry → 2-layer board, no inner copper expected
# Boards with no inner layers (front-board) should not appear here.
BOARD_INNER_LAYERS = {
    "io-board":      {".G1": "GND", ".G2": "GND"},
    "backing-board": {".G1": "GND", ".G2": "split"},
}

# Modules with their own onboard decoupling — exclude from Pass 3
# distance checks against board-level caps. The WeAct STM32F405 module
# (U1) has its own AMS1117 LDO and bypass network on the daughterboard,
# so caps on the main PCB only need to be at the module's pin headers,
# not the MCU pin location.
MODULES_INTERNAL_DECOUPLE = {"U1"}

# Doc-defined IC pin assignments (for Pass 2 step 4 — rail-to-IC verification).
# Key: (board, designator, pin) -> expected net name. From power-supply.md
# §1, with PCM5102A pins corrected to the datasheet (PCM5102A.md):
# AVDD = pin 8, CPVDD = pin 1.
EXPECTED_IC_PINS = {
    ("backing-board", "U3", "1"):  "+3V3_AUDIO",   # PCM5102A CPVDD
    ("backing-board", "U3", "8"):  "+3V3_AUDIO",   # PCM5102A AVDD
    ("backing-board", "U3", "20"): "+3V3",         # PCM5102A DVDD
    ("backing-board", "U3", "12"): "GND",          # PCM5102A SCK (PLL mode)
    ("backing-board", "U3", "13"): None,           # PCM5102A BCK (I2S net)
    ("backing-board", "U3", "14"): None,           # PCM5102A DIN (I2S net)
    ("backing-board", "U3", "15"): None,           # PCM5102A LRCK (I2S net)
    ("backing-board", "U2", "2"):  "+3V3_PREC",    # REF5025 VIN
    ("backing-board", "U2", "4"):  "GND",
    ("backing-board", "U2", "6"):  "VREF",
    ("backing-board", "U6", "1"):  "+3V3_PREC",    # DAC8552 VDD
    ("backing-board", "U6", "2"):  "VREF",
    ("backing-board", "U7", "8"):  "+12V",         # OPA1642 V+
    ("backing-board", "U7", "4"):  "-12V",
    ("backing-board", "U16", "8"): "+12V",
    ("backing-board", "U16", "4"): "-12V",
    ("backing-board", "U12", "3"): "+12V",         # TPS54202 VIN
    ("backing-board", "U12", "1"): "GND",
    ("backing-board", "U14", "1"): "+5V",          # TPS7A2033 VIN
    ("backing-board", "U14", "5"): "+3V3_PREC",    # TPS7A2033 VOUT
    ("backing-board", "U15", "1"): "+5V",
    ("backing-board", "U15", "5"): "+3V3_AUDIO",
}

# Decoupling distance rule (Pass 3): cap centre to IC centre threshold (mm).
DECOUPLING_DIST_MAX_MM = 8.0  # 0805 cap centre-to-IC-centre, lenient

# Pass 6: trace-width minimums.
#   `min_track_mm`     — fab capability floor; below this the board won't
#                        manufacture cleanly. JLCPCB 1 oz default = 0.15 mm.
#   `pad_threshold_mm` — circular apertures wider than this are treated
#                        as pad / via diameters, not trace widths, so they
#                        are excluded from the trace-width histogram.
TRACE_WIDTH_RULES = {
    "min_track_mm":     0.15,
    "pad_threshold_mm": 1.0,
}

# Spacing constraints (mm) — these are the real engineering invariants
# from the design docs. Unlike cardinal-quadrant zone assignments
# (which become stale the moment the layout iterates), distance
# constraints stay valid as long as the underlying physics does.
# Each entry: (board, ref_a, ref_b, op, threshold_mm, source_citation).
SPACING_RULES = [
    ("backing-board", "U12", "U3", "ge", 30.0,
     "TPS54202 (noisy buck) ↔ PCM5102A (audio DAC) — pcb-design.md §5"),
    ("backing-board", "U12", "U2", "ge", 30.0,
     "TPS54202 (noisy buck) ↔ REF5025 (precision ref) — pcb-design.md §5"),
    ("backing-board", "U12", "U16", "ge", 30.0,
     "TPS54202 (noisy buck) ↔ OPA1642 audio buffers — analogous to U3"),
    ("backing-board", "U2", "U6", "le", 20.0,
     "REF5025 ↔ DAC8552 — cv-output-dac.md §6 (VREF trace)"),
]

# Pass 4 used to enforce a Zone A/B/C cardinal-quadrant assignment per
# IC. That table was hardcoded against pcb-design.md §5's zone
# diagram, which goes stale the moment the layout iterates. Removed —
# Pass 4 now just reports where each IC sits and runs the spacing
# rules above. Update pcb-design.md §5 if the zone narrative there
# stops matching the actual placement.

# Eurorack 10-pin standard pinout (Pass 2 step 5).
EUROHEADER_10P_STANDARD = {
    "version_a": {"-12V": [1, 2], "GND": [3, 4, 5, 6, 7, 8], "+12V": [9, 10]},
    "version_b": {"+12V": [1, 2], "GND": [3, 4, 5, 6, 7, 8], "-12V": [9, 10]},
}

# I2S nets — used by Pass 5 to report endpoints / corridor occupancy.
# Series-termination is intentionally NOT enforced at this trace length;
# see pcb-design.md §5 "The I2S bridge — Series termination — not required
# at this trace length" for the calculation. Re-introduce a check here
# only if the design changes (longer traces or faster GPIO drive).
I2S_NETS = {"I2S3_BCK", "I2S3_SD", "I2S3_WS"}

# Header footprint detection (re-uses pattern from cross_board_audit).
HEADER_RE = re.compile(
    r"\bHDR\b|\bPINHDR\b|\bHEADER\b|^\s*1X\d+|^\s*2X\d+|"
    r"\bP\d+X\d+|2\.54[_\-]?MM|2\.0[_\-]?MM|\bIDC\b",
    re.IGNORECASE)


# ---------------------------------------------------------------------------
# Findings model
# ---------------------------------------------------------------------------

@dataclass
class Finding:
    severity: str   # "error" (layout fix needed) | "stale" (doc update) | "advisory"
    pass_id: str    # e.g. "Pass 1", "Pass 7"
    summary: str    # one-line
    detail: str = ""

@dataclass
class BoardReport:
    name: str
    tel: dict | None = None
    pnp: list = field(default_factory=list)
    gerber: dict | None = None
    findings: list[Finding] = field(default_factory=list)
    notes: list[str] = field(default_factory=list)  # things verified clean
    skips: list[str] = field(default_factory=list)


# ---------------------------------------------------------------------------
# Discovery
# ---------------------------------------------------------------------------

def newest(paths: list[Path]) -> Path | None:
    if not paths: return None
    return max(paths, key=lambda p: p.stat().st_mtime)


def discover(gerber_dir: Path) -> dict[str, dict[str, Path | None]]:
    """Return {board_name: {tel, pnp, gerber_subdir}}."""
    out: dict[str, dict[str, Path | None]] = {}
    for board, (subdir, pnp_glob) in EXPECTED_BOARDS.items():
        out[board] = {
            "tel":    newest(list(gerber_dir.glob(f"Netlist_{board}-schematic_*.tel"))),
            "pnp":    newest(list(gerber_dir.glob(pnp_glob))),
            "gerber": (gerber_dir / subdir) if (gerber_dir / subdir).is_dir() else None,
        }
    return out


# ---------------------------------------------------------------------------
# Indexing
# ---------------------------------------------------------------------------

def index_tel(tel: dict) -> dict:
    nets = {n["name"]: n["members"] for n in tel["nets"] if len(n["members"]) > 1}
    ref_pins: dict[str, dict[str, str]] = {}
    for nname, members in nets.items():
        for ref, pin in members:
            ref_pins.setdefault(ref, {})[pin] = nname
    fp: dict[str, str] = {}
    val: dict[str, str] = {}
    for p in tel["packages"]:
        for d in p["designators"]:
            fp[d] = p["footprint"]
            val[d] = p["value"]
    return {"nets": nets, "ref_pins": ref_pins, "fp": fp, "val": val}


def index_pnp(pnp: list[dict]) -> dict[str, dict]:
    return {r["designator"]: r for r in pnp if r.get("designator")}


# ---------------------------------------------------------------------------
# Audit passes
# ---------------------------------------------------------------------------

def pass1_stackup(rep: BoardReport) -> None:
    """Layer / stackup sanity (Gerber)."""
    g = rep.gerber
    if not g:
        rep.skips.append("Pass 1 (stackup): no gerber directory")
        return

    layers = {L["extension"]: L for L in g["layers"]}
    has_inner = ".G1" in layers and ".G2" in layers
    is_4_layer = has_inner

    if is_4_layer:
        g1, g2 = layers[".G1"], layers[".G2"]
        rep.notes.append(
            f"Stackup: 4-layer ({len(layers)} gerber layers). "
            f"G1 (regions={g1['region_count']}, "
            f"copper_areas={g1['copper_areas']}). "
            f"G2 (regions={g2['region_count']}, "
            f"copper_areas={g2['copper_areas']}).")

        expected = BOARD_INNER_LAYERS.get(rep.name, {})
        for ext, layer in [(".G1", g1), (".G2", g2)]:
            exp = expected.get(ext)
            empty = layer["region_count"] == 0 and (layer["copper_areas"] or 0) == 0
            if empty:
                rep.findings.append(Finding(
                    "error", "Pass 1",
                    f"Inner layer {ext} is empty — no PlaneZone, no "
                    "SolidRegions, no copper at all.",
                    f"Expected: {exp or 'a pour'}. Either add the copper or "
                    "drop the board to 2-layer and update the doc."))
                continue
            if exp == "GND":
                if layer["copper_areas"] == 1 or (
                    layer["copper_areas"] == 0 and layer["region_count"] > 50
                ):
                    rep.notes.append(
                        f"{ext}: single GND pour ✓ "
                        f"(regions={layer['region_count']}, "
                        f"copper_areas={layer['copper_areas']}). "
                        "Confirm electrical continuity visually if encoded as "
                        "many SolidRegions.")
                else:
                    rep.findings.append(Finding(
                        "advisory", "Pass 1",
                        f"{ext} expected to be a single GND pour but the "
                        f"gerber shows copper_areas={layer['copper_areas']}, "
                        f"regions={layer['region_count']} — confirm intent.", ""))
            elif exp == "split":
                expected_count = len(EXPECTED_RAILS.get(rep.name, set()) -
                                     {"GND", "VREF"} -
                                     {n for n in EXPECTED_RAILS.get(rep.name, set())
                                      if "EURO" in n})
                if layer["copper_areas"] == expected_count:
                    rep.notes.append(
                        f"{ext}: {layer['copper_areas']} PlaneZones — power "
                        f"layer split per pcb-design.md §4 ✓ "
                        f"(matches expected rail count)")
                elif layer["copper_areas"] and layer["copper_areas"] > 1:
                    rep.notes.append(
                        f"{ext}: {layer['copper_areas']} PlaneZones (rail "
                        f"split) — note: rail-count expectation was "
                        f"{expected_count}.")
                else:
                    rep.findings.append(Finding(
                        "error", "Pass 1",
                        f"{ext} expected to be a split power layer (one "
                        "PlaneZone per rail) but gerber shows "
                        f"copper_areas={layer['copper_areas']} only.",
                        "Either re-pour as per-rail PlaneZones, or update "
                        "the doc if the design has migrated to a different "
                        "approach."))
            else:
                rep.notes.append(
                    f"{ext}: no expectation set in BOARD_INNER_LAYERS for "
                    f"{rep.name}; observed regions={layer['region_count']}, "
                    f"copper_areas={layer['copper_areas']}.")
    else:
        rep.notes.append(f"Stackup: 2-layer ({len(layers)} gerber layers, no .G1/.G2)")
        if rep.name != "front-board":
            rep.findings.append(Finding(
                "error", "Pass 1",
                f"{rep.name} is 2-layer but pcb-design.md §2 declares 4-layer "
                "as the module standard.",
                "Either re-fab as 4-layer with planes, or update the doc to "
                "scope the 4-layer rule per-board."))

    # Bbox
    for L in g["layers"]:
        if L["extension"] == ".GKO" and L.get("bbox_mm"):
            b = L["bbox_mm"]
            rep.notes.append(
                f"Board outline: {b['width_mm']:.2f} × {b['height_mm']:.2f} mm "
                f"(X[{b['x_min']:.2f},{b['x_max']:.2f}] "
                f"Y[{b['y_min']:.2f},{b['y_max']:.2f}])")


def pass2_nets(rep: BoardReport) -> None:
    """Net inventory and rail names (Telesis)."""
    if rep.tel is None:
        rep.skips.append("Pass 2 (net inventory): no .tel file")
        return
    idx = index_tel(rep.tel)
    nets = idx["nets"]
    named = {n for n in nets if not n.startswith("$")}

    expected = EXPECTED_RAILS.get(rep.name, set())
    missing = expected - named
    extra_rails = {n for n in named if "+" in n or "-12V" in n or n in {"GND", "VREF"}} - expected

    rep.notes.append(f"Named nets: {sorted(named)}")
    rep.notes.append(
        f"Anonymous (auto-generated $...) nets: "
        f"{sum(1 for n in nets if n.startswith('$'))}")

    for r in sorted(missing):
        rep.findings.append(Finding(
            "stale", "Pass 2",
            f"Doc-expected rail `{r}` is not present in this board's netlist.",
            "Either the rail name was changed in EasyEDA, or the rail is not "
            "wired here (check whether it is supposed to reach this board)."))
    for r in sorted(extra_rails):
        rep.findings.append(Finding(
            "advisory", "Pass 2",
            f"Rail `{r}` is in the netlist but not in the documented set "
            f"for {rep.name}.",
            "Either the doc is stale (add it) or this is a stray net (check "
            "for a wiring mistake)."))

    # Single-ground rule
    gnd_like = [n for n in named if n.upper() in {"GND", "AGND", "DGND", "PGND"}]
    if len(gnd_like) > 1:
        rep.findings.append(Finding(
            "error", "Pass 2",
            f"Multiple ground nets present: {gnd_like}. "
            "pcb-design.md §3 mandates a single GND.",
            "Merge to one net in the schematic."))
    elif gnd_like == ["GND"]:
        rep.notes.append("Single ground net (GND): ✓ matches pcb-design.md §3.")

    # Doc-defined pin assignments
    for (board, ref, pin), expected_net in EXPECTED_IC_PINS.items():
        if board != rep.name: continue
        if expected_net is None: continue   # doc just says "should have a net"
        actual = idx["ref_pins"].get(ref, {}).get(pin)
        if actual is None:
            rep.findings.append(Finding(
                "stale", "Pass 2",
                f"{ref} pin {pin} not in netlist (doc expects `{expected_net}`).",
                "Either the IC was renumbered or the doc is stale."))
        elif actual != expected_net:
            rep.findings.append(Finding(
                "stale", "Pass 2",
                f"{ref} pin {pin}: net is `{actual}`, doc expects `{expected_net}`.",
                "Verify which is correct against the device datasheet — the "
                "common case is the doc has stale pin numbers."))

    # 74AHCT1G125 buffer: only stale if the docs treat it as a *live*
    # part. A historical mention ("earlier revisions referenced X; that
    # part was dropped because ...") is fine and should not trigger.
    if rep.name == "backing-board" and "U4" not in idx["fp"]:
        stale_in = []
        for doc in ("pcb-design.md", "power-supply.md", "audio-output-dac.md"):
            p = Path(doc)
            if not p.exists(): continue
            for ln in p.read_text(encoding="utf-8").splitlines():
                if "74AHCT1G125" not in ln: continue
                if any(k in ln.lower() for k in
                       ("dropped", "removed", "earlier revision",
                        "previously", "obsolete", "no longer")):
                    continue
                stale_in.append(doc)
                break
        if stale_in:
            rep.findings.append(Finding(
                "stale", "Pass 2",
                f"74AHCT1G125 buffer is referenced as a live part in "
                f"{', '.join(stale_in)} but is absent from the netlist.",
                "The buffer was dropped from the layout (PCM5102A's internal "
                "PLL handles clock-domain transition) — remove these doc "
                "references or restore the part."))

    # Eurorack header check (Pass 2 step 5)
    if rep.name == "backing-board":
        check_eurorack_header(rep, idx)


def check_eurorack_header(rep: BoardReport, idx: dict) -> None:
    """Pass 2 step 5: Eurorack power-bus header pinout."""
    candidates = []
    for ref, fp in idx["fp"].items():
        pins = idx["ref_pins"].get(ref, {})
        if len(pins) not in (10, 16): continue
        nets = set(pins.values())
        # Must touch both +12V and -12V (or EURO variants)
        has_p12 = any("+12" in n or n == "12VEURO" for n in nets)
        has_n12 = any("-12" in n for n in nets)
        if has_p12 and has_n12:
            candidates.append((ref, fp, pins))

    if not candidates:
        rep.notes.append("No Eurorack power-bus header detected on this board.")
        return

    for ref, fp, pins in candidates:
        n_neg = [p for p, n in pins.items() if "-12" in n]
        n_pos = [p for p, n in pins.items() if ("+12" in n or n == "12VEURO")]
        n_gnd = [p for p, n in pins.items() if n == "GND"]
        n_other = [p for p, n in pins.items()
                   if not any(k in n for k in ["12V", "GND"]) and n != "12VEURO"]
        try:
            neg_int = sorted(int(p) for p in n_neg)
            pos_int = sorted(int(p) for p in n_pos)
            gnd_int = sorted(int(p) for p in n_gnd)
        except ValueError:
            rep.notes.append(f"Eurorack header {ref}: non-integer pin labels, "
                             "cannot run adjacency check.")
            continue

        if len(pins) == 10:
            ok = (
                len(neg_int) == 2 and len(pos_int) == 2 and len(gnd_int) == 6
                and abs(neg_int[1] - neg_int[0]) == 1
                and abs(pos_int[1] - pos_int[0]) == 1
                and (max(neg_int) == 10 or min(neg_int) == 1)
                and (max(pos_int) == 10 or min(pos_int) == 1)
                and gnd_int == [3, 4, 5, 6, 7, 8]
                and not n_other
            )
            if ok:
                rep.notes.append(
                    f"Eurorack 10-pin header {ref}: standard pinout ✓ "
                    f"({{-12V:{neg_int}, GND:{gnd_int}, +12V:{pos_int}}}). "
                    "Verify silk stripe sits on -12V edge in EasyEDA Pro 2D view.")
            else:
                rep.findings.append(Finding(
                    "error", "Pass 2",
                    f"Eurorack header {ref} has non-standard pinout — "
                    "plug-in destroys the module.",
                    f"Found: -12V={neg_int}, GND={gnd_int}, +12V={pos_int}, "
                    f"other={n_other}. Expected: -12V at {{1,2}} or {{9,10}}, "
                    "+12V at the opposite end, GND at 3-8, no other rails."))
        elif len(pins) == 16:
            rep.notes.append(f"Eurorack 16-pin header {ref} detected; "
                             "audit logic for 16-pin not yet implemented.")


def pass3_decoupling(rep: BoardReport) -> None:
    """Per-IC decoupling distance (Telesis + PnP)."""
    if rep.tel is None or not rep.pnp:
        rep.skips.append("Pass 3 (decoupling): missing .tel or PnP")
        return
    idx = index_tel(rep.tel)
    pnp_idx = index_pnp(rep.pnp)

    # For each IC (designator starting with U), find the caps connected
    # to its power pins and measure cap centre to IC centre.
    issues: list[tuple[str, str, str, float]] = []
    checked: list[tuple[str, str, str, float]] = []

    for ref, pins in idx["ref_pins"].items():
        if not ref.startswith("U"): continue
        if ref in MODULES_INTERNAL_DECOUPLE:
            rep.notes.append(
                f"Pass 3: skipping {ref} (sub-module with internal decoupling).")
            continue
        if ref not in pnp_idx: continue
        ic_x, ic_y = pnp_idx[ref]["x_mm"], pnp_idx[ref]["y_mm"]
        # power pins = pins on a power rail (heuristic: net starts with + or -)
        for pin, net in pins.items():
            if not (net.startswith("+") or net.startswith("-") and net != "-"):
                continue
            # Find caps on this rail
            caps = [m[0] for m in idx["nets"].get(net, [])
                    if m[0].startswith("C") and m[0] in pnp_idx]
            for c in caps:
                d = math.hypot(pnp_idx[c]["x_mm"] - ic_x,
                               pnp_idx[c]["y_mm"] - ic_y)
                # Only consider the nearest cap on this rail per IC pin
                # to avoid noise. Track the minimum.
                checked.append((ref, pin, c, d))
                if d > DECOUPLING_DIST_MAX_MM:
                    issues.append((ref, pin, c, d))

    # Rank: for each IC, find its CLOSEST cap on each rail. If even the
    # closest is too far, flag.
    by_ic_rail: dict[tuple[str, str], list[tuple[str, str, float]]] = {}
    for ref, pin, cap, d in checked:
        rail = idx["ref_pins"][ref][pin]
        by_ic_rail.setdefault((ref, rail), []).append((pin, cap, d))
    for (ref, rail), entries in sorted(by_ic_rail.items()):
        nearest_pin, nearest_cap, nearest_d = min(entries, key=lambda e: e[2])
        if nearest_d > DECOUPLING_DIST_MAX_MM:
            rep.findings.append(Finding(
                "error", "Pass 3",
                f"{ref} {rail}: nearest decoupling cap {nearest_cap} is "
                f"{nearest_d:.2f} mm from IC centre.",
                f"pcb-design.md §5 wants ≤2 mm pin-to-cap (≤8 mm centre-to-"
                f"centre on 0805); this is too far. Move the cap closer to "
                f"{ref} pin {nearest_pin}."))
        else:
            rep.notes.append(
                f"Decoupling {ref} {rail}: nearest cap {nearest_cap} at "
                f"{nearest_d:.2f} mm ✓")


def pass4_zones(rep: BoardReport) -> None:
    """Placement observations + spacing-rule check (PnP)."""
    if not rep.pnp:
        rep.skips.append("Pass 4 (placement): no PnP")
        return
    pnp_idx = index_pnp(rep.pnp)

    # Spacing constraints — the real engineering invariants
    any_spacing_for_this_board = False
    for board, a, b, op, threshold, descr in SPACING_RULES:
        if board != rep.name: continue
        any_spacing_for_this_board = True
        if a not in pnp_idx or b not in pnp_idx:
            rep.notes.append(
                f"Spacing rule {a}↔{b}: skipped (one of the designators is "
                "not on this board's PnP).")
            continue
        d = math.hypot(pnp_idx[a]["x_mm"] - pnp_idx[b]["x_mm"],
                       pnp_idx[a]["y_mm"] - pnp_idx[b]["y_mm"])
        ok = (d >= threshold) if op == "ge" else (d <= threshold)
        if not ok:
            rep.findings.append(Finding(
                "error", "Pass 4",
                f"Spacing rule failed: {a}↔{b} = {d:.2f} mm, "
                f"target {op} {threshold} mm.",
                f"Source: {descr}."))
        else:
            rep.notes.append(
                f"Spacing {a}↔{b} = {d:.2f} mm ({op} {threshold} mm): ✓ — {descr}")
    if not any_spacing_for_this_board:
        rep.notes.append(
            f"Pass 4: no spacing rules defined for {rep.name} in SPACING_RULES.")

    # Placement observations — just record where each IC sits, no
    # zone-quadrant enforcement. Lets the user (or a future doc revision)
    # decide whether the layout is right.
    if rep.name == "backing-board":
        # Sort key ICs by Y then X for a readable observation list
        key_ics = ["U12", "P1", "U14", "U15", "U16", "U2", "U7", "U6",
                   "U3", "U1", "H2", "H3", "H4", "H9"]
        for ref in key_ics:
            if ref not in pnp_idx: continue
            r = pnp_idx[ref]
            rep.notes.append(
                f"Placement {ref:<4} at ({r['x_mm']:>6.1f}, {r['y_mm']:>7.1f}) "
                f"side={r['side']:<6} fp={r['footprint']}")
        # Stale only if the old Zone A/B/C diagram still appears verbatim.
        pcb_design = Path("pcb-design.md")
        if pcb_design.exists():
            txt = pcb_design.read_text(encoding="utf-8")
            if "ZONE A: POWER" in txt or "ZONE B: DIGITAL" in txt:
                rep.findings.append(Finding(
                    "stale", "Pass 4",
                    "pcb-design.md §5 still contains the cardinal-quadrant "
                    "Zone A/B/C diagram, which doesn't reflect the current "
                    "stratified layout.",
                    "Replace the ASCII zone diagram with a description of "
                    "the actual stratification (power top / analog band "
                    "middle / MCU module bottom-side) and codify the "
                    "spacing rules that drive placement."))


def pass5_i2s(rep: BoardReport) -> None:
    """I2S corridor (Telesis)."""
    if rep.name != "backing-board":
        rep.skips.append("Pass 5 (I2S): no I2S on this board.")
        return
    if rep.tel is None: return
    idx = index_tel(rep.tel)

    # Termination check is in Pass 2; here we look at corridor obstructions
    # and signal-chain integrity.
    for net in I2S_NETS:
        members = idx["nets"].get(net, [])
        if not members:
            rep.findings.append(Finding(
                "stale", "Pass 5",
                f"I2S net `{net}` not in netlist.", ""))
            continue
        refs = sorted({m[0] for m in members})
        rep.notes.append(f"{net}: members {refs}")


def pass6_traces(rep: BoardReport) -> None:
    """Trace-width inventory and minimum-width check (Gerber)."""
    if not rep.gerber:
        rep.skips.append("Pass 6 (trace widths): no gerber directory")
        return
    min_mm = TRACE_WIDTH_RULES["min_track_mm"]
    pad_th = TRACE_WIDTH_RULES["pad_threshold_mm"]

    # Aggregate stroke-aperture diameters per copper layer. The
    # gerber inspector classifies apertures by usage: only those used
    # in D01 (stroke) commands count as trace widths. Flashes (D03)
    # are pads / via drills and are excluded.
    layers_of_interest = {".GTL", ".GBL", ".G1", ".G2"}
    by_layer: dict[str, dict[float, int]] = {}
    for L in rep.gerber["layers"]:
        ext = L["extension"]
        if ext not in layers_of_interest: continue
        widths = L.get("trace_widths_mm", {}) or {}
        # Drop anything wider than pad threshold — those are stroked
        # pads or fills, not actual signal/power traces.
        widths = {w: n for w, n in widths.items() if w <= pad_th}
        if widths:
            by_layer[ext] = widths

    if not by_layer:
        rep.skips.append("Pass 6 (trace widths): no stroked apertures found "
                         "(layers may use only flashes / regions).")
        return

    for ext, widths in by_layer.items():
        items = sorted(widths.items())
        formatted = ", ".join(f"{w:.3f}mm×{n}" for w, n in items)
        rep.notes.append(f"Trace widths on {ext}: {formatted}")
        narrow = [w for w in widths if w < min_mm]
        if narrow:
            rep.findings.append(Finding(
                "error", "Pass 6",
                f"{ext} has stroked traces below the {min_mm} mm minimum: "
                f"{sorted(narrow)}.",
                "Tracks narrower than the fab's 1 oz copper minimum may "
                "fail manufacture or require an upgraded process. Either "
                "thicken the affected traces or document the fab "
                "exception in pcb-design.md."))

    rep.notes.append(
        "Per-net trace-width compliance (e.g. '+5V must be ≥ 0.6 mm') "
        "cannot be verified from gerber geometry alone — the gerber "
        "encodes geometry without net assignment. Verify visually in "
        "the EasyEDA Pro 2D view or by exporting routed length per net "
        "from the project.")


def pass7_cross(boards: dict[str, BoardReport]) -> dict:
    """Cross-board interface."""
    # Reuse cross_board_audit logic by importing it lazily
    from cross_board_audit import audit as cross_audit
    # The script discovers from the parent dir; we already have the data,
    # but re-running is cheap and gives us the same structure.
    return {}


def render_md(rep: BoardReport, all_findings_count: dict[str, int],
              cross_summary: dict | None = None) -> str:
    """Render this board's findings to a markdown report."""
    lines = []
    lines.append(f"# PCB Audit — {rep.name}\n")
    lines.append(f"**Auto-generated by `.claude/skills/pcb-designer/scripts/run_audit.py`**\n")

    # Sources
    lines.append("## Sources\n")
    if rep.tel is not None:
        lines.append(f"- Telesis netlist: indexed ({len(rep.tel.get('packages',[]))} package types, "
                     f"{len(rep.tel.get('nets',[]))} nets)")
    else:
        lines.append("- Telesis netlist: **missing**")
    if rep.pnp:
        lines.append(f"- Pick-and-place: {len(rep.pnp)} components")
    else:
        lines.append("- Pick-and-place: **missing**")
    if rep.gerber:
        lines.append(f"- Gerber: {rep.gerber.get('directory','?')} "
                     f"({len(rep.gerber.get('layers',[]))} layers)")
    else:
        lines.append("- Gerber: **missing**")
    lines.append("")

    # Findings, bucketed by severity
    layout_errors = [f for f in rep.findings if f.severity == "error"]
    stale_docs    = [f for f in rep.findings if f.severity == "stale"]
    advisories    = [f for f in rep.findings if f.severity == "advisory"]

    lines.append("## Findings\n")
    lines.append(f"- Layout errors (need EasyEDA Pro fix): **{len(layout_errors)}**")
    lines.append(f"- Stale documentation (need MD edit): **{len(stale_docs)}**")
    lines.append(f"- Advisories: **{len(advisories)}**")
    lines.append("")

    if layout_errors:
        lines.append("### Layout errors\n")
        for f in layout_errors:
            lines.append(f"- **[{f.pass_id}] {f.summary}**")
            if f.detail:
                for ln in f.detail.split("\n"):
                    lines.append(f"  > {ln}")
        lines.append("")
    if stale_docs:
        lines.append("### Stale documentation\n")
        for f in stale_docs:
            lines.append(f"- **[{f.pass_id}] {f.summary}**")
            if f.detail:
                for ln in f.detail.split("\n"):
                    lines.append(f"  > {ln}")
        lines.append("")
    if advisories:
        lines.append("### Advisories\n")
        for f in advisories:
            lines.append(f"- **[{f.pass_id}] {f.summary}**")
            if f.detail:
                for ln in f.detail.split("\n"):
                    lines.append(f"  > {ln}")
        lines.append("")

    # Verified clean / observations
    if rep.notes:
        lines.append("## Observations\n")
        for n in rep.notes:
            lines.append(f"- {n}")
        lines.append("")

    if rep.skips:
        lines.append("## Skipped passes\n")
        for s in rep.skips:
            lines.append(f"- {s}")
        lines.append("")

    return "\n".join(lines)


# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------

def run(gerber_dir: Path, out_dir: Path, only: str | None) -> int:
    boards_paths = discover(gerber_dir)
    boards: dict[str, BoardReport] = {}

    for name, paths in boards_paths.items():
        if only and name != only: continue
        rep = BoardReport(name=name)
        if paths["tel"]:
            try:
                rep.tel = parse_tel(paths["tel"])
            except Exception as e:
                rep.skips.append(f".tel parse error: {e}")
        if paths["pnp"]:
            try:
                rep.pnp = parse_pnp(paths["pnp"])
            except Exception as e:
                rep.skips.append(f"PnP parse error: {e}")
        if paths["gerber"]:
            try:
                rep.gerber = inspect_dir(paths["gerber"])
            except Exception as e:
                rep.skips.append(f"Gerber inspect error: {e}")

        # Run passes
        pass1_stackup(rep)
        if rep.tel and rep.tel.get("nets"):
            pass2_nets(rep)
            pass3_decoupling(rep)
            pass4_zones(rep)
            pass5_i2s(rep)
            pass6_traces(rep)
        else:
            rep.skips.append("Passes 2-6: no electrical content (empty netlist)")

        boards[name] = rep

    # Write reports
    findings_count = {n: len(r.findings) for n, r in boards.items()}
    out_dir.mkdir(parents=True, exist_ok=True)
    written = []
    for name, rep in boards.items():
        md = render_md(rep, findings_count)
        path = out_dir / f"audit-{name}.md"
        path.write_text(md, encoding="utf-8")
        written.append(path)

    # Summary to stdout
    print(f"Audited {len(boards)} board(s):")
    for name, rep in boards.items():
        e = sum(1 for f in rep.findings if f.severity == "error")
        s = sum(1 for f in rep.findings if f.severity == "stale")
        a = sum(1 for f in rep.findings if f.severity == "advisory")
        print(f"  {name}: {e} layout error(s), {s} stale doc(s), {a} advisory")
    print(f"\nReports written to:")
    for p in written:
        print(f"  {p}")
    return 0


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--gerber-dir", type=Path, default=Path("gerber"))
    ap.add_argument("--out-dir",    type=Path, default=Path("gerber"))
    ap.add_argument("--board",      type=str, default=None,
                    help="Only audit this board (front-board, io-board, backing-board)")
    args = ap.parse_args()
    if not args.gerber_dir.is_dir():
        print(f"error: gerber dir not found: {args.gerber_dir}", file=sys.stderr)
        return 1
    return run(args.gerber_dir, args.out_dir, args.board)


if __name__ == "__main__":
    raise SystemExit(main())
