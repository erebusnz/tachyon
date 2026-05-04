#!/usr/bin/env python3
"""Summarise a directory of RS-274X gerber + Excellon files.

Usage:
    python inspect_gerber.py <gerber-dir>            # human-readable summary
    python inspect_gerber.py <gerber-dir> --json     # machine-readable

Reports:
    - which layer files are present (by extension)
    - coordinate format (FSL) and units (MO) per layer
    - aperture inventory per copper layer (count + size for C/R/O)
    - pour-region count per copper layer (G36* directives)
    - board bounding box from *.GKO (in mm)
    - drill-tool inventory per *.DRL file

Stdlib only. See references/gerber-format.md.
"""

from __future__ import annotations
import argparse
import json
import re
import sys
from pathlib import Path

# Maps file extension → human label. Uppercase comparison.
LAYER_LABELS = {
    ".GTL": "Top copper",
    ".GBL": "Bottom copper",
    ".G1":  "Inner layer 1",
    ".G2":  "Inner layer 2",
    ".GTO": "Top silk",
    ".GBO": "Bottom silk",
    ".GTS": "Top mask",
    ".GBS": "Bottom mask",
    ".GTP": "Top paste",
    ".GBP": "Bottom paste",
    ".GTA": "Top assembly",
    ".GBA": "Bottom assembly",
    ".GKO": "Board outline",
    ".GDD": "Drill drawing",
    ".GDL": "Document",
    ".GCL": "Custom",
}
COPPER_EXTS = {".GTL", ".GBL", ".G1", ".G2"}

_ADD = re.compile(r"%ADD(\d+)([A-Za-z][A-Za-z0-9_]*),([^*]+)\*%")
_FSL = re.compile(r"%FSL?A?X(\d)(\d)Y(\d)(\d)\*%")
_MO  = re.compile(r"%MO(MM|IN)\*%")
# EasyEDA Pro emits this comment in every gerber it generates. The number
# is the count of "PlaneZone" pours (copper areas with thermal connections).
# It does NOT include G36/G37 SolidRegion polygons — those are stroked
# directly. Reliable: trust this for plane-integrity checks.
_COPPER_AREAS = re.compile(r"G04\s+Copper\s+Areas:\s*(\d+)\s*\*")


def _scale_factor(fsl_match: re.Match[str] | None, mo_match: re.Match[str] | None) -> float:
    """Return the multiplier to convert raw integer coords to mm."""
    decimals = int(fsl_match.group(2)) if fsl_match else 5
    is_mm = (mo_match.group(1) == "MM") if mo_match else True
    base = 1.0 / (10 ** decimals)
    return base if is_mm else base * 25.4


def _coord_iter(text: str):
    """Yield (x_raw, y_raw) integer pairs from gerber X/Y commands."""
    for m in re.finditer(r"X(-?\d+)Y(-?\d+)", text):
        yield int(m.group(1)), int(m.group(2))


def inspect_gerber_file(path: Path) -> dict:
    text = path.read_text(encoding="utf-8", errors="replace")
    fsl = _FSL.search(text)
    mo  = _MO.search(text)
    apertures: dict[str, dict[str, int]] = {}
    aperture_diameter_by_dcode: dict[int, tuple[str, float | None]] = {}
    for m in _ADD.finditer(text):
        dcode = int(m.group(1))
        shape = m.group(2)
        params = m.group(3).strip()
        key = f"{shape}({params})"
        apertures.setdefault(shape, {})
        apertures[shape][key] = apertures[shape].get(key, 0) + 1
        # Extract first numeric param as the diameter for circles, or
        # leave None for non-circles (we only care about circle widths
        # for trace-width analysis).
        diameter: float | None = None
        if shape == "C":
            try:
                diameter = float(params.split("X")[0])
            except ValueError:
                pass
        aperture_diameter_by_dcode[dcode] = (shape, diameter)

    # Walk the body to attribute usage to each D-code: was it used in a
    # stroke (D01 outside a region) or as a flash (D03)? Stroke usage
    # of a circle aperture means its diameter is a trace width on this
    # layer; flash usage means it's a round pad / via drill. Strokes
    # *inside* G36/G37 region blocks are polygon outlines, not real
    # signal traces, so they're excluded.
    #
    # EasyEDA Pro uses the legacy `G54D<n>*` aperture-select form (not
    # bare `D<n>*`) so we match both.
    stroke_use: dict[int, int] = {}
    flash_use:  dict[int, int] = {}
    current_d = 0
    in_region = False
    select_re = re.compile(r"^(?:G54)?D(\d+)\*$")
    for line in text.splitlines():
        s = line.strip()
        if s == "G36*": in_region = True; continue
        if s == "G37*": in_region = False; continue
        m = select_re.match(s)
        if m:
            current_d = int(m.group(1))
            continue
        if not current_d: continue
        if s.endswith("D01*") and not in_region:
            stroke_use[current_d] = stroke_use.get(current_d, 0) + 1
        elif s.endswith("D03*"):
            flash_use[current_d] = flash_use.get(current_d, 0) + 1

    # Build trace-width inventory: for circular apertures used at least
    # once for stroking, the diameter IS a trace width on this layer.
    trace_widths_mm: dict[float, int] = {}
    for dcode, n_stroke in stroke_use.items():
        shape, dia = aperture_diameter_by_dcode.get(dcode, (None, None))
        if shape == "C" and dia is not None:
            trace_widths_mm[dia] = trace_widths_mm.get(dia, 0) + n_stroke

    region_count = text.count("G36*")
    ca_match = _COPPER_AREAS.search(text)
    copper_areas = int(ca_match.group(1)) if ca_match else None
    info = {
        "path": str(path),
        "extension": path.suffix.upper(),
        "label": LAYER_LABELS.get(path.suffix.upper(), "Unknown"),
        "fsl": fsl.group(0) if fsl else None,
        "mo":  mo.group(0)  if mo  else None,
        "aperture_shapes": {k: sum(v.values()) for k, v in apertures.items()},
        "aperture_detail": apertures,
        "trace_widths_mm": trace_widths_mm,
        "region_count": region_count,
        "copper_areas": copper_areas,
    }
    if path.suffix.upper() == ".GKO":
        scale = _scale_factor(fsl, mo)
        xs, ys = [], []
        for x, y in _coord_iter(text):
            xs.append(x * scale)
            ys.append(y * scale)
        if xs and ys:
            info["bbox_mm"] = {
                "x_min": min(xs), "x_max": max(xs),
                "y_min": min(ys), "y_max": max(ys),
                "width_mm":  max(xs) - min(xs),
                "height_mm": max(ys) - min(ys),
            }
    return info


def inspect_drill_file(path: Path) -> dict:
    text = path.read_text(encoding="utf-8", errors="replace")
    tools: dict[str, dict] = {}
    cur_tool: str | None = None
    is_metric = "METRIC" in text.upper()
    for line in text.splitlines():
        line = line.strip()
        m = re.match(r"^(T\d+)C([\d.]+)", line)
        if m:
            tool, dia = m.group(1), float(m.group(2))
            tools[tool] = {"diameter": dia, "unit": "mm" if is_metric else "in", "holes": 0}
            continue
        m = re.match(r"^(T\d+)$", line)
        if m:
            cur_tool = m.group(1)
            continue
        if cur_tool and line.startswith("X"):
            if cur_tool in tools:
                tools[cur_tool]["holes"] += 1
    return {"path": str(path), "tools": tools}


def inspect_dir(directory: Path) -> dict:
    layers: list[dict] = []
    drills: list[dict] = []
    for p in sorted(directory.iterdir()):
        if not p.is_file():
            continue
        ext = p.suffix.upper()
        if ext in LAYER_LABELS:
            layers.append(inspect_gerber_file(p))
        elif ext == ".DRL":
            drills.append(inspect_drill_file(p))
    return {"directory": str(directory), "layers": layers, "drills": drills}


def _print_summary(report: dict) -> None:
    print(f"Directory: {report['directory']}")
    print(f"\nLayers ({len(report['layers'])}):")
    for L in report["layers"]:
        ca = L.get("copper_areas")
        ca_str = "n/a" if ca is None else str(ca)
        print(f"  {L['extension']:<5} {L['label']:<18} "
              f"apertures={sum(L['aperture_shapes'].values())} "
              f"regions={L['region_count']} "
              f"copper_areas={ca_str}")
        if L.get("bbox_mm"):
            b = L["bbox_mm"]
            print(f"    board bbox: {b['width_mm']:.2f} mm x {b['height_mm']:.2f} mm "
                  f"(X {b['x_min']:.2f}..{b['x_max']:.2f}, "
                  f"Y {b['y_min']:.2f}..{b['y_max']:.2f})")
        if L["extension"] in COPPER_EXTS and L["aperture_detail"]:
            for shape, items in L["aperture_detail"].items():
                top = sorted(items.items(), key=lambda kv: -kv[1])[:5]
                bits = ", ".join(f"{k}x{n}" for k, n in top)
                print(f"    {shape}: {bits}")

    print(f"\nDrill files ({len(report['drills'])}):")
    for d in report["drills"]:
        print(f"  {Path(d['path']).name}")
        for tool, info in d["tools"].items():
            print(f"    {tool}: dia {info['diameter']} {info['unit']}, "
                  f"{info['holes']} holes")


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("gerber_dir", type=Path)
    ap.add_argument("--json", action="store_true")
    args = ap.parse_args()

    if not args.gerber_dir.is_dir():
        print(f"error: not a directory: {args.gerber_dir}", file=sys.stderr)
        return 1

    report = inspect_dir(args.gerber_dir)
    if args.json:
        json.dump(report, sys.stdout, indent=2)
        print()
    else:
        _print_summary(report)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
