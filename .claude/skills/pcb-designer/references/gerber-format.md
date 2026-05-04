# Gerber RS-274X — subset used in this project

EasyEDA Pro emits standard RS-274X (extended Gerber) — text, one file
per layer, with format and aperture macros at the top.

## Layer file extensions (this project)

| Extension | Layer                                       |
|-----------|---------------------------------------------|
| `.GTL`    | Top copper                                  |
| `.GBL`    | Bottom copper                               |
| `.G1`     | Inner layer 1 (this project: GND plane)     |
| `.G2`     | Inner layer 2 (this project: power pours)   |
| `.GTO`    | Top silkscreen                              |
| `.GBO`    | Bottom silkscreen                           |
| `.GTS`    | Top solder mask                             |
| `.GBS`    | Bottom solder mask                          |
| `.GTP`    | Top paste mask (stencil)                    |
| `.GBP`    | Bottom paste mask                           |
| `.GTA`    | Top assembly drawing                        |
| `.GBA`    | Bottom assembly drawing                     |
| `.GKO`    | Board outline                               |
| `.GDD`    | Drill drawing (informational)               |
| `.GDL`    | Document layer                              |
| `.GCL`    | Custom layer (often courtyards, fab notes)  |

Drill files use Excellon, not Gerber:

| Extension              | Contents                            |
|------------------------|-------------------------------------|
| `Drill_PTH_Through.DRL`     | Plated through-holes (component)|
| `Drill_PTH_Through_Via.DRL` | Plated through-vias             |
| `Drill_NPTH_Through.DRL`    | Non-plated holes (mounting)     |

## Header anatomy

A typical EasyEDA Pro header:

```
G04 Layer: TopLayer*
G04 EasyEDA Pro v2.2.47.7, 2026-04-30 22:47:58*
G04 Dimensions in millimeters*
G04 Leading zeros omitted, absolute positions, 4 integers and 5 decimals*
%FSLAX45Y45*%
%MOMM*%
%AMRoundRect*1,1,$1,$2,$3*1,1,$1,$4,$5*...*%
%ADD10C,0.2032*%
%ADD11C,0.254*%
%ADD12R,1.13254X1.37701*%
G75*
```

Key directives:

- `G04 ... *` — comment.
- `%FSLAX<n><m>Y<n><m>*%` — coordinate format. Here `4.5` means 4
  integer digits and 5 decimal digits, with leading zeros omitted.
  A coordinate `X175260` therefore reads as `1.75260` mm.
- `%MOMM*%` — units are millimetres (alternative `%MOIN*%` is inches;
  EasyEDA Pro defaults to mm).
- `%AM<name>*...*%` — aperture macro definition (parametric shapes).
- `%ADD<n><type>,<params>*%` — aperture definition. Number `n` is the
  D-code referenced later.
  - `C` — circle (one parameter: diameter).
  - `R` — rectangle (X by Y).
  - `O` — obround / stadium (X by Y).
  - `RoundRect` — rounded rectangle, named macro defined above.
- `G75*` — multi-quadrant arc mode.

## Body draw commands

After the header, draw operations select an aperture and stroke or flash
shapes:

- `D<n>*` — switch to aperture `n`.
- `X<value>Y<value>D01*` — draw to (interpolate) — produces a line of
  the current aperture's width.
- `X<value>Y<value>D02*` — move to (no draw).
- `X<value>Y<value>D03*` — flash — places one copy of the current
  aperture at this point.
- `G36*` ... `G37*` — region (filled polygon) start / end.

For copper-area accounting:

- A draw from D02 to D01 in `G01*` (linear) interp mode produces a
  trace.
- A flash (`D03*`) of a circular aperture is a pad or via.
- A `G36*..G37*` block is a polygon pour (one closed region) — in
  EasyEDA Pro terminology this is a "SolidRegion".
- A "PlaneZone" (auto-flooded pour) is rendered as many small stroked
  polygons rather than a single `G36/G37` block; use the
  `G04 Copper Areas: N*` header comment to count them.

## What to extract for an audit

1. **Layer presence** — which extensions exist in the directory.
2. **Coordinate format** — read the `%FSL...*%` line; needed to scale
   raw integers back to mm.
3. **Board bounding box** — from `Gerber_BoardOutlineLayer.GKO`,
   collect every `X<value>Y<value>` pair, scale to mm, take min/max.
4. **Aperture inventory** — read every `%ADD...*%` and bin by shape +
   size. Useful for trace-width audits.
5. **Pour count per layer** — EasyEDA Pro emits a header comment
   `G04 Copper Areas: N*` on every layer; this is the count of
   "PlaneZone" pours (auto-flooded copper with thermals). It is
   **independent** of `G36*` directives, which encode static
   "SolidRegion" polygons drawn manually. Use `Copper Areas` for
   plane-integrity checks: a GND plane should show 1 (the whole
   plane); a power layer with N rails should show ~N. A G1/G2 layer
   that shows `Copper Areas: 0` and zero `G36*` regions has **no
   pour at all** — flag immediately.

## Excellon (`.DRL`) format

Plain text, similar in spirit to Gerber:

```
M48
;DRILL file
INCH,LZ                  -- or METRIC,LZ / METRIC,TZ
T1C0.011                 -- tool 1, 0.011 in (or mm) diameter
T2C0.020
%
T1
X<value>Y<value>         -- one hole per line, current tool
T2
X<value>Y<value>
M30                      -- end
```

For an audit, you mostly want to count holes per tool to verify the
project's drill table didn't gain a stray exotic size. Drill scaling
follows the same `LZ`/`TZ` (leading-zero / trailing-zero) convention
as Gerber.

## Pitfalls

- The coordinate format is **not** a fixed convention — always read
  `%FSL...*%`. EasyEDA Pro defaults to `4.5` but `4.4` and `3.6` are
  also legal.
- A `G36`/`G37` region may visually look "split" because of cutouts
  inside it (an outer contour with an inner reverse-wound contour).
  Counting raw `G36*` matches the *number of pour outlines*, not the
  number of disjoint copper islands. For a strict islands-count, you
  need geometry; the heuristic count is good enough to flag obvious
  splits but not subtle ones.
- File order in the zip is alphabetical; do not rely on it to identify
  layer purpose — use the extension.

`scripts/inspect_gerber.py` produces the layer/aperture/region summary
needed for Pass 1 of the audit.
