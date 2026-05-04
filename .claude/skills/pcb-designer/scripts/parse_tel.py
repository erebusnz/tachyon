#!/usr/bin/env python3
"""Parse an Allegro / Telesis (.tel) netlist into a JSON summary.

Usage:
    python parse_tel.py <file.tel>            # human-readable summary
    python parse_tel.py <file.tel> --json     # machine-readable dict

Outputs:
    {
      "packages": [
        {"footprint": str, "value": str, "tolerance": str,
         "designators": [str, ...]}, ...
      ],
      "nets": [
        {"name": str, "members": [(ref, pin), ...]}, ...
      ]
    }

Stdlib only. See references/tel-format.md for format details.
"""

from __future__ import annotations
import argparse
import json
import re
import sys
from pathlib import Path


def _unquote(token: str) -> str:
    """Strip surrounding single or double quotes if present."""
    t = token.strip()
    if len(t) >= 2 and t[0] == t[-1] and t[0] in ("'", '"'):
        return t[1:-1]
    return t


def _read_logical_lines(path: Path) -> list[str]:
    """Return logical lines, joining continuations.

    EasyEDA Pro Telesis output uses two continuation conventions, often
    on the same logical line:

      - the previous line ends with ``,``, or
      - the next line begins with ``,`` (indented continuation).

    We greedily merge across both forms.
    """
    text = path.read_text(encoding="utf-8", errors="replace")
    raw = [line.rstrip("\r\n") for line in text.splitlines()]
    out: list[str] = []
    i = 0
    while i < len(raw):
        line = raw[i]
        i += 1
        # Pull in subsequent continuation lines.
        while True:
            ends_with_comma = line.rstrip().endswith(",")
            next_is_continuation = (
                i < len(raw) and raw[i].lstrip().startswith(",")
            )
            if not ends_with_comma and not next_is_continuation:
                break
            if i >= len(raw):
                break
            line = line.rstrip().rstrip(",") + " " + raw[i].lstrip().lstrip(",")
            i += 1
        out.append(line.strip())
    return out


def parse_tel(path: Path) -> dict:
    lines = _read_logical_lines(path)
    section = None
    packages: list[dict] = []
    nets: list[dict] = []

    for raw in lines:
        line = raw.strip()
        if not line or line.startswith("!"):
            continue
        if line == "$PACKAGES":
            section = "packages"
            continue
        if line == "$NETS":
            section = "nets"
            continue
        # Other section markers (e.g. $A_PROPERTIES emitted by EasyEDA
        # Pro) — skip until the next recognised section.
        if line.startswith("$") and line not in ("$PACKAGES", "$NETS", "$END"):
            section = "other"
            continue
        if line == "$END":
            break

        if section == "packages":
            # "<footprint>!<value>!<tol>; D1 D2 ..."
            if ";" not in line:
                continue
            head, _, tail = line.partition(";")
            head_parts = head.split("!")
            footprint = _unquote(head_parts[0]) if head_parts else ""
            value = _unquote(head_parts[1]) if len(head_parts) > 1 else ""
            tolerance = _unquote(head_parts[2]) if len(head_parts) > 2 else ""
            designators = [_unquote(d) for d in re.split(r"[,\s]+", tail) if d.strip()]
            packages.append({
                "footprint": footprint,
                "value": value,
                "tolerance": tolerance,
                "designators": designators,
            })
        elif section == "nets":
            # "<NETNAME>; REF.PIN REF.PIN ..."
            if ";" not in line:
                continue
            name, _, tail = line.partition(";")
            members = []
            for tok in re.split(r"[,\s]+", tail):
                tok = _unquote(tok)
                if "." in tok:
                    ref, pin = tok.split(".", 1)
                    members.append((_unquote(ref), _unquote(pin)))
            nets.append({"name": _unquote(name), "members": members})

    return {"packages": packages, "nets": nets}


def _print_summary(parsed: dict) -> None:
    pkgs = parsed["packages"]
    nets = parsed["nets"]

    n_designators = sum(len(p["designators"]) for p in pkgs)
    print(f"PACKAGES: {len(pkgs)} entries, {n_designators} designators")
    for p in pkgs:
        refs = ", ".join(p["designators"][:6])
        more = "" if len(p["designators"]) <= 6 else f" (+{len(p['designators'])-6} more)"
        val = f" {p['value']}" if p["value"] else ""
        print(f"  {p['footprint']}{val}: {refs}{more}")

    print(f"\nNETS: {len(nets)}")
    no_connects = [n for n in nets if len(n["members"]) <= 1]
    print(f"  ({len(no_connects)} no-connects / single-pin)")
    for n in nets:
        if len(n["members"]) <= 1:
            continue
        members = " ".join(f"{r}.{p}" for r, p in n["members"][:8])
        more = "" if len(n["members"]) <= 8 else f" (+{len(n['members'])-8})"
        print(f"  {n['name']}: {members}{more}")


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("tel_file", type=Path)
    ap.add_argument("--json", action="store_true")
    args = ap.parse_args()

    if not args.tel_file.exists():
        print(f"error: file not found: {args.tel_file}", file=sys.stderr)
        return 1

    parsed = parse_tel(args.tel_file)
    if args.json:
        json.dump(parsed, sys.stdout, indent=2)
        print()
    else:
        _print_summary(parsed)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
