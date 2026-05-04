#!/usr/bin/env python3
"""Cross-board interface audit for a multi-board EasyEDA Pro project.

Given a parent directory holding one sub-directory per board, each
containing a `.tel` netlist, report:

  - Net intersection: which net names appear on which boards, with the
    (designator, pin) members on each side.
  - Headers per board: designators whose footprint matches a pin-header
    pattern, with pin -> net mapping.
  - Header-pair candidates: pairs of headers across two different boards
    with the same pin count and high net-name overlap, plus a pin-by-pin
    net diff.

Usage:
    python cross_board_audit.py <parent-dir>            # human-readable
    python cross_board_audit.py <parent-dir> --json     # machine-readable

The script auto-discovers boards: any immediate sub-directory of the
parent that contains a `*.tel` file is treated as a board. The board
name is the directory name. If a sub-directory has multiple `.tel`
files, the first lexicographically wins (this is unusual; warn).

Stdlib only. Imports parse_tel.py from the same directory.
"""

from __future__ import annotations
import argparse
import json
import re
import sys
from pathlib import Path

# Re-use the parser. We're stdlib only, so import the sibling module.
sys.path.insert(0, str(Path(__file__).resolve().parent))
from parse_tel import parse_tel  # type: ignore  # noqa: E402

# Footprints that are pin-header candidates. Patterns are intentionally
# generous; spurious matches get filtered by the pair-matching step
# (which requires the candidates to actually agree on a non-trivial
# number of net names with a header on another board).
HEADER_FOOTPRINT_PATTERNS = [
    r"\bHDR\b",        # HDR-2x10, HDR_1X8 etc.
    r"\bPINHDR\b",     # PINHDR_2x10
    r"\bHEADER\b",     # HEADER-2x10
    r"^\s*1X\d+",      # 1X8, 1x10
    r"^\s*2X\d+",      # 2X8, 2x10
    r"\bP\d+X\d+",     # P2X10
    r"2\.54[_\-]?MM",  # 2.54mm
    r"2\.0[_\-]?MM",   # 2.0mm
    r"\bIDC\b",        # ribbon-cable IDC headers
]
HEADER_RE = re.compile("|".join(HEADER_FOOTPRINT_PATTERNS), re.IGNORECASE)


def _board_name_from_filename(stem: str) -> str:
    """Extract a canonical board id from an EasyEDA Pro export filename.

    Examples:
        'Netlist_io-board-schematic_2026-05-04' -> 'io-board'
        'Netlist_front-board-schematic_2026-05-04' -> 'front-board'
        'Netlist_backing-board-schematic_2026-05-04' -> 'backing-board'
    Falls back to the stem itself if no pattern matches.
    """
    m = re.match(r"^Netlist_(.+?)-schematic", stem)
    if m:
        return m.group(1)
    m = re.match(r"^Netlist_(.+?)_\d{4}-\d{2}-\d{2}", stem)
    if m:
        return m.group(1)
    return stem


def discover_boards(parent: Path) -> list[tuple[str, Path]]:
    """Return [(board_name, tel_path), ...] for the boards under `parent`.

    Tries two layouts in order:

      1. Per-board sub-directories: every immediate child directory of
         `parent` that contains a `*.tel` file. The board name is the
         directory name.
      2. Flat layout: `*.tel` files directly inside `parent`. The board
         name is derived from the filename (typically
         `Netlist_<board>-schematic_<date>.tel` from EasyEDA Pro).

    If both layouts have content we use the per-board sub-directory
    layout and warn that the flat-layout files are being ignored.
    """
    boards: list[tuple[str, Path]] = []
    if not parent.is_dir():
        return boards

    # Layout 1: per-board sub-directories.
    for child in sorted(parent.iterdir()):
        if not child.is_dir():
            continue
        tels = sorted(child.glob("*.tel"))
        if not tels:
            continue
        if len(tels) > 1:
            print(f"warning: {child.name} has {len(tels)} .tel files; "
                  f"using {tels[0].name}", file=sys.stderr)
        boards.append((child.name, tels[0]))

    # Layout 2: flat. Only consult if layout 1 found nothing.
    if not boards:
        for tel in sorted(parent.glob("*.tel")):
            name = _board_name_from_filename(tel.stem)
            boards.append((name, tel))

    return boards


def index_board(parsed: dict) -> dict:
    """Build the per-board index used by the cross-board passes."""
    pkgs = parsed["packages"]
    nets = parsed["nets"]

    # designator -> footprint
    ref_to_footprint: dict[str, str] = {}
    for p in pkgs:
        for ref in p["designators"]:
            ref_to_footprint[ref] = p["footprint"]

    # net_name -> [(ref, pin), ...]
    net_members: dict[str, list[tuple[str, str]]] = {}
    for n in nets:
        if len(n["members"]) <= 1:
            continue  # skip no-connects
        net_members[n["name"]] = list(n["members"])

    # designator -> {pin: net_name}
    ref_pins: dict[str, dict[str, str]] = {}
    for net_name, members in net_members.items():
        for ref, pin in members:
            ref_pins.setdefault(ref, {})[pin] = net_name

    # pin-header candidates (refs whose footprint matches the pattern)
    headers: dict[str, dict] = {}
    for ref, fp in ref_to_footprint.items():
        if HEADER_RE.search(fp):
            pin_map = ref_pins.get(ref, {})
            headers[ref] = {
                "footprint": fp,
                "pin_count": len(pin_map),
                "pins": pin_map,
            }

    return {
        "packages": pkgs,
        "ref_to_footprint": ref_to_footprint,
        "net_members": net_members,
        "ref_pins": ref_pins,
        "headers": headers,
    }


def net_intersection(boards: dict[str, dict]) -> list[dict]:
    """Return shared nets sorted by descending board count then name."""
    name_to_boards: dict[str, list[str]] = {}
    for board_name, idx in boards.items():
        for net_name in idx["net_members"]:
            name_to_boards.setdefault(net_name, []).append(board_name)

    shared = []
    for net_name, where in sorted(name_to_boards.items()):
        if len(where) < 2:
            continue
        per_board = []
        for b in where:
            members = boards[b]["net_members"][net_name]
            per_board.append({"board": b, "members": members})
        shared.append({"net": net_name, "boards": where, "per_board": per_board})

    shared.sort(key=lambda x: (-len(x["boards"]), x["net"]))
    return shared


def header_pair_candidates(boards: dict[str, dict]) -> list[dict]:
    """Find candidate mating header pairs across boards.

    A pair is a candidate when:
      - The two headers are on different boards.
      - They have the same pin count.
      - At least 50% of their net names overlap.
    """
    items: list[tuple[str, str, dict]] = []
    for board_name, idx in boards.items():
        for ref, info in idx["headers"].items():
            items.append((board_name, ref, info))

    pairs = []
    for i in range(len(items)):
        b1, ref1, h1 = items[i]
        nets1 = set(h1["pins"].values())
        for j in range(i + 1, len(items)):
            b2, ref2, h2 = items[j]
            if b1 == b2:
                continue
            if h1["pin_count"] != h2["pin_count"] or h1["pin_count"] == 0:
                continue
            nets2 = set(h2["pins"].values())
            if not nets1 or not nets2:
                continue
            overlap = nets1 & nets2
            score = len(overlap) / max(len(nets1), len(nets2))
            if score < 0.5:
                continue
            pairs.append({
                "boards": [b1, b2],
                "refs":   [ref1, ref2],
                "footprints": [h1["footprint"], h2["footprint"]],
                "pin_count": h1["pin_count"],
                "overlap_score": round(score, 3),
                "pin_diff": _pin_diff(h1["pins"], h2["pins"]),
            })
    pairs.sort(key=lambda p: (-p["overlap_score"], -p["pin_count"]))
    return pairs


def _pin_diff(pins_a: dict[str, str], pins_b: dict[str, str]) -> list[dict]:
    """Per-pin diff. Pins are matched by name (numeric or symbolic)."""
    all_pins = sorted(set(pins_a) | set(pins_b),
                      key=lambda s: (len(s), s))
    out = []
    for p in all_pins:
        a = pins_a.get(p)
        b = pins_b.get(p)
        out.append({
            "pin": p,
            "net_a": a,
            "net_b": b,
            "match": a == b and a is not None,
        })
    return out


def audit(parent: Path) -> dict:
    discovered = discover_boards(parent)
    boards: dict[str, dict] = {}
    for board_name, tel_path in discovered:
        parsed = parse_tel(tel_path)
        boards[board_name] = index_board(parsed)

    return {
        "parent": str(parent),
        "boards_found": [b for b, _ in discovered],
        "shared_nets": net_intersection(boards),
        "headers_per_board": {
            b: {ref: {"footprint": info["footprint"],
                      "pin_count": info["pin_count"],
                      "pins": info["pins"]}
                for ref, info in idx["headers"].items()}
            for b, idx in boards.items()
        },
        "header_pair_candidates": header_pair_candidates(boards),
    }


def _print(report: dict) -> None:
    print(f"Parent: {report['parent']}")
    print(f"Boards found: {report['boards_found']}")

    shared = report["shared_nets"]
    print(f"\nShared net names ({len(shared)}):")
    if not shared:
        print("  (none — every net is local to one board)")
    for s in shared[:50]:
        print(f"  {s['net']}: {', '.join(s['boards'])}")
        for pb in s["per_board"]:
            members = " ".join(f"{r}.{p}" for r, p in pb["members"][:6])
            more = ("" if len(pb["members"]) <= 6
                    else f" (+{len(pb['members'])-6})")
            print(f"    [{pb['board']}] {members}{more}")
    if len(shared) > 50:
        print(f"  ... and {len(shared) - 50} more")

    print("\nPin headers per board:")
    for b, hdrs in report["headers_per_board"].items():
        if not hdrs:
            print(f"  {b}: (none)")
            continue
        print(f"  {b}:")
        for ref, info in sorted(hdrs.items()):
            print(f"    {ref:<8} {info['footprint']:<30} "
                  f"({info['pin_count']} pins)")

    pairs = report["header_pair_candidates"]
    print(f"\nHeader-pair candidates ({len(pairs)}):")
    if not pairs:
        print("  (none — no two boards share a likely-mating header)")
    for p in pairs:
        b1, b2 = p["boards"]
        r1, r2 = p["refs"]
        print(f"  {b1}.{r1} <-> {b2}.{r2}  "
              f"({p['pin_count']} pins, overlap={p['overlap_score']:.2f})")
        for d in p["pin_diff"]:
            mark = "ok" if d["match"] else "**"
            print(f"    {mark} pin {d['pin']:<4} "
                  f"{str(d['net_a']):<20} | {str(d['net_b'])}")


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("parent", type=Path,
                    help="Directory holding one sub-directory per board")
    ap.add_argument("--json", action="store_true")
    args = ap.parse_args()

    if not args.parent.is_dir():
        print(f"error: not a directory: {args.parent}", file=sys.stderr)
        return 1

    report = audit(args.parent)
    if args.json:
        json.dump(report, sys.stdout, indent=2)
        print()
    else:
        _print(report)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
