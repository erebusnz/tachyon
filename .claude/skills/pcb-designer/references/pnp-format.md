# Pick-and-place CSV (EasyEDA Pro)

Despite the `.csv` extension, EasyEDA Pro's pick-and-place export is
typically:

- **UTF-16 LE with BOM** (`0xFF 0xFE` first two bytes). Decode as
  `utf-16-le`, not `utf-8`.
- **TAB-separated**, not comma-separated. Sniff the first line: if it
  contains more tabs than commas, use `\t` as the delimiter.
- Windows line endings (`\r\n`).

Some older / alternative templates do emit UTF-8 with comma delimiters,
so the parser auto-detects: read the first bytes for a BOM, decode
accordingly, then count tabs vs commas in the header to pick the
delimiter.

Column order and naming varies between EasyEDA Pro template versions,
so always read the header — never position-index columns.

## Common column names

A typical EasyEDA Pro header in this project's exports:

```
Designator  Device  Footprint  Mid X  Mid Y  Ref X  Ref Y  Pad X  Pad Y  Pins  Layer  Rotation  SMD  Comment
```

Column-name aliases the parser recognises:

| Concept       | Header variants seen                     |
|---------------|------------------------------------------|
| Designator    | `Designator`, `Ref`, `RefDes`            |
| Footprint     | `Footprint`, `Package`                   |
| Value         | `Value`, `Comment`                       |
| X coordinate  | `Mid X`, `MidX`, `PosX`, `X`, `Center-X` |
| Y coordinate  | `Mid Y`, `MidY`, `PosY`, `Y`, `Center-Y` |
| Rotation      | `Rotation`, `Rot`, `Angle`               |
| Layer / side  | `Layer`, `Side`, `TB`                    |

Note that EasyEDA Pro emits both `Mid X/Y` (component centre) and
`Pad X/Y` (a reference pad position). The audit always uses `Mid X/Y`
for centre-to-centre distance calculations; `Pad X/Y` is for fab
flying-probe alignment and not relevant to the design audit.

The X/Y columns may be quoted with `mm` suffix (`"12.345mm"`); strip
units before float conversion.

## Layer values

- Top: `T`, `top`, `TopLayer`, `1`
- Bottom: `B`, `bot`, `bottom`, `BottomLayer`, `2`

## Coordinate origin

The exporter offers two origins, plus an axis-direction quirk:

1. **Board origin (corner)** — (0, 0) is the upper-left of the board
   outline. X grows to the right (positive), Y grows downward
   (negative). The board outline therefore lies in `x∈[0, +W]`,
   `y∈[-H, 0]`. Component coordinates have positive X and *negative*
   Y. This is the default in the project's current exports.
2. **Board centre** — (0, 0) is the geometric centre of the outline.
   Components have negative coordinates on two sides (both X and Y).

The origin is **not** stored in the CSV. The auditor infers from the
data:

- Negative X and negative Y both seen → centre origin.
- Negative on only one axis → corner origin with the unfortunate
  Y-mirrored convention. Treat the absolute value of Y as a downward
  distance from the top of the board.
- All positive → either corner origin with conventional axes, or a
  centre-origin export for a board whose outline happens not to span
  the centre. Cross-check against the gerber outline's bounding box.

## Rotation convention

- Degrees, 0-360 (sometimes signed; treat -90 and 270 as equivalent).
- 0° = component in its native footprint orientation.
- Positive = counter-clockwise.

EasyEDA Pro's "fab rotation" can differ from the rotation displayed in
the editor by ±90°/180° depending on the footprint origin in the
library. Don't rely on rotation for IC orientation checks unless you
also have the footprint definition.

## Missing rows

Components with `do not place` set in EasyEDA Pro are **omitted** from
the PnP CSV (not marked as DNP). Cross-check designator counts against
the Telesis netlist's `$PACKAGES` section: if the netlist has 142
designators but the PnP has 138, four are DNP — list which ones.

## Worked snippet

```
"Designator","Footprint","Mid X","Mid Y","Rotation","Layer","Comment"
"C1","C0402","12.345","8.762","0","T","100nF"
"C2","C0402","13.45","8.762","180","T","100nF"
"U12","SOT23-6","20.1","15.0","90","T","TPS54202DDCR"
"R5","R0603","-3.2","-1.5","0","B","33"
```

Negative X/Y on R5 → this is a centre-origin export; confirm with the
user.

## Parser pitfalls

- BOM at start of file (`﻿`) breaks naive `csv.DictReader`. Open
  with `encoding='utf-8-sig'`.
- Quoted fields may contain commas; use `csv` module, never split.
- "mm" suffix on coordinates — strip before float.
- Header sometimes localised (Chinese variants) — if you see CJK
  characters, ask the user to re-export with English headers.

`scripts/parse_pnp.py` handles BOM, quoting, the unit suffix, and the
common header aliases.
