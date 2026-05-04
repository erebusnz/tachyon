#!/usr/bin/env python3
"""Parse an EasyEDA Pro pick-and-place CSV into a normalised list.

Usage:
    python parse_pnp.py <file.csv>            # human-readable summary
    python parse_pnp.py <file.csv> --json     # machine-readable list

Each row becomes:
    {"designator": str, "footprint": str, "value": str,
     "x_mm": float, "y_mm": float, "rotation_deg": float,
     "side": "top" | "bottom"}

Auto-detects column-name variants and the unit suffix on coordinates.
Handles UTF-8 BOM. Stdlib only. See references/pnp-format.md.
"""

from __future__ import annotations
import argparse
import csv
import io
import json
import sys
from pathlib import Path

# Header aliases — first match wins. Lowercased and stripped before lookup.
_ALIASES = {
    "designator": ["designator", "ref", "refdes"],
    "footprint":  ["footprint", "package"],
    "value":      ["value", "comment"],
    "x":          ["mid x", "midx", "posx", "x", "center-x", "centerx"],
    "y":          ["mid y", "midy", "posy", "y", "center-y", "centery"],
    "rotation":   ["rotation", "rot", "angle"],
    "side":       ["layer", "side", "tb"],
}

_TOP_TOKENS    = {"t", "top", "toplayer", "1"}
_BOTTOM_TOKENS = {"b", "bot", "bottom", "bottomlayer", "2"}


def _resolve_columns(fieldnames: list[str]) -> dict:
    norm = {f: f.strip().lower() for f in fieldnames if f}
    resolved: dict[str, str] = {}
    for key, options in _ALIASES.items():
        for f, low in norm.items():
            if low in options:
                resolved[key] = f
                break
    missing = [k for k in ("designator", "x", "y") if k not in resolved]
    if missing:
        raise ValueError(
            f"PnP CSV is missing required columns: {missing}. "
            f"Saw header: {list(norm.values())}"
        )
    return resolved


def _strip_unit(s: str) -> str:
    s = s.strip().strip('"').strip()
    for suf in ("mm", "MM", "mil", "in"):
        if s.endswith(suf):
            return s[: -len(suf)].strip()
    return s


def _classify_side(value: str) -> str:
    v = value.strip().strip('"').lower()
    if v in _TOP_TOKENS:
        return "top"
    if v in _BOTTOM_TOKENS:
        return "bottom"
    return v or "unknown"


def _open_text(path: Path) -> io.StringIO:
    """Read the file as text, auto-detecting BOM-marked encoding.

    EasyEDA Pro's pick-and-place export is sometimes UTF-16 LE with a
    BOM and TAB delimiters (despite the .csv extension), and sometimes
    UTF-8 with comma delimiters. Detect by sniffing the first bytes.
    """
    raw = path.read_bytes()
    if raw.startswith(b"\xff\xfe"):
        text = raw.decode("utf-16-le").lstrip("﻿")
    elif raw.startswith(b"\xfe\xff"):
        text = raw.decode("utf-16-be").lstrip("﻿")
    elif raw.startswith(b"\xef\xbb\xbf"):
        text = raw.decode("utf-8-sig")
    else:
        text = raw.decode("utf-8", errors="replace")
    return io.StringIO(text)


def _sniff_delimiter(sample: str) -> str:
    """Return ',' or '\\t' depending on which the header line uses."""
    first_line = sample.splitlines()[0] if sample else ""
    tabs = first_line.count("\t")
    commas = first_line.count(",")
    return "\t" if tabs > commas else ","


def parse_pnp(path: Path) -> list[dict]:
    rows: list[dict] = []
    f = _open_text(path)
    sample = f.read(4096)
    f.seek(0)
    delimiter = _sniff_delimiter(sample)
    reader = csv.DictReader(f, delimiter=delimiter)
    if reader.fieldnames is None:
        return rows
    cols = _resolve_columns(reader.fieldnames)
    for raw in reader:
        try:
            x = float(_strip_unit(raw.get(cols["x"], "") or "0"))
            y = float(_strip_unit(raw.get(cols["y"], "") or "0"))
        except ValueError:
            continue
        rot_raw = raw.get(cols.get("rotation", ""), "0") or "0"
        try:
            rotation = float(_strip_unit(rot_raw))
        except ValueError:
            rotation = 0.0
        rows.append({
            "designator": (raw.get(cols["designator"], "") or "").strip(),
            "footprint":  (raw.get(cols.get("footprint", ""), "") or "").strip(),
            "value":      (raw.get(cols.get("value", ""), "") or "").strip(),
            "x_mm":       x,
            "y_mm":       y,
            "rotation_deg": rotation,
            "side":       _classify_side(raw.get(cols.get("side", ""), "")),
        })
    return rows


def _safe(s: str) -> str:
    """Replace characters that won't fit in the console encoding."""
    enc = getattr(sys.stdout, "encoding", "utf-8") or "utf-8"
    try:
        s.encode(enc)
        return s
    except UnicodeEncodeError:
        return s.encode(enc, errors="replace").decode(enc, errors="replace")


def _print_summary(rows: list[dict]) -> None:
    if not rows:
        print("(empty)")
        return
    xs = [r["x_mm"] for r in rows]
    ys = [r["y_mm"] for r in rows]
    print(f"Components: {len(rows)}")
    print(f"  X range: {min(xs):.3f} .. {max(xs):.3f} mm")
    print(f"  Y range: {min(ys):.3f} .. {max(ys):.3f} mm")
    neg_x = any(v < 0 for v in xs)
    neg_y = any(v < 0 for v in ys)
    if neg_x and neg_y:
        origin = "centre (both X and Y go negative)"
    elif neg_x or neg_y:
        origin = "corner with one mirrored axis (likely top-left, Y grows downward)"
    else:
        origin = "corner (all positive)"
    print(f"  Origin: {origin}")
    sides: dict[str, int] = {}
    for r in rows:
        sides[r["side"]] = sides.get(r["side"], 0) + 1
    print(f"  Sides: {sides}")
    print()
    for r in rows[:30]:
        print(f"  {_safe(r['designator']):<8} {_safe(r['footprint']):<18} "
              f"{_safe(r['value']):<14} ({r['x_mm']:>8.3f}, {r['y_mm']:>8.3f}) "
              f"rot={r['rotation_deg']:>5.1f}  {r['side']}")
    if len(rows) > 30:
        print(f"  ... and {len(rows) - 30} more")


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("csv_file", type=Path)
    ap.add_argument("--json", action="store_true")
    args = ap.parse_args()

    if not args.csv_file.exists():
        print(f"error: file not found: {args.csv_file}", file=sys.stderr)
        return 1

    rows = parse_pnp(args.csv_file)
    if args.json:
        json.dump(rows, sys.stdout, indent=2)
        print()
    else:
        _print_summary(rows)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
