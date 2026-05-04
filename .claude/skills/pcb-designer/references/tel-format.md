# Allegro / Telesis netlist (`.tel`) format

Plain ASCII text. The file is divided into sections, each opened by a
keyword on its own line. The minimal set is `$PACKAGES`, `$NETS`,
`$END`, but EasyEDA Pro also emits these:

- `$PACKAGES` — opens the package (component) section.
- `$A_PROPERTIES` — attribute / property metadata (skip).
- `$NETS` — opens the net (connectivity) section.
- `$SCHEDULE` — bus/group scheduling info (skip).
- `$END` — closes the file. Always last.

Treat any unrecognised `$<KEYWORD>` line as the start of a section to
skip until the next recognised marker. Lines beginning with `!` are
comments and may be ignored.

**Quoted tokens.** EasyEDA Pro single-quotes any token that contains a
character it considers special: net names with `+`/`-` prefixes,
values with units (`'1kΩ'`, `'2.2nF'`), and so on. The skill's parser
strips matching surrounding `'` or `"` from every token. A net named
`'+5V'` in the file means the net `+5V`; do not preserve the quotes.

## $PACKAGES section

One logical entry per component. Each entry is one or more physical
lines, joined when a line ends with a continuation marker (`,` followed
by whitespace and a continuation on the next line).

Format:

```
<footprint-name>!<value>!<tolerance>; <DESIGNATOR> [, <DESIGNATOR>] ...
```

Notes:

- Multiple designators may share one entry when they have identical
  footprint + value (e.g. all 100 nF 0402 caps).
- Footprint names sometimes contain spaces, so the `!` separator
  matters more than whitespace.
- Value and tolerance may be empty (just `!!`) for connectors and
  modules that don't have a numeric value.

Parse strategy: read until `$NETS`, split on `;` to separate the
footprint header from the designator list, then split the designator
list on `,`.

## $NETS section

One entry per net. A net spans one or more physical lines; lines that
end with `,` continue on the next line. Format:

```
<NETNAME>; <REF>.<PIN> [<REF>.<PIN>] ...
```

Where each `<REF>.<PIN>` is a connection point. `<REF>` is a designator
from `$PACKAGES`; `<PIN>` is the pin name on that footprint (may be a
number like `1` or a name like `VOUT`).

Special cases:

- A net with exactly one `<REF>.<PIN>` is an unconnected pin and is
  often emitted as `N_<random>` or similar — these are **no-connects**
  and should be ignored when checking connectivity.
- Power and ground nets are named exactly as in the schematic
  (`+5V`, `GND`, etc.). EasyEDA Pro preserves the `+` and `-`
  prefixes verbatim.
- Bus members are flattened — `D[0..7]` becomes eight nets `D0`..`D7`.

## Worked example

```
$PACKAGES
C0402!100nF!10%; C1, C2, C3, C4, C5
SOT23-5!TPS54202DDCR!; U12
SOIC-8!OPA1642!; U7
$NETS
+5V; U12.4 C1.1 C2.1 U14.1 U15.1
GND; U12.5 U12.7 C1.2 C2.2 U7.4 U14.2 U15.2,
     C3.2 C4.2 C5.2
SW; U12.6 L3.1
$END
```

Reads as:

- C1..C5 are 100 nF 0402 caps; U12 is the TPS54202; U7 is the OPA1642.
- The `+5V` net connects pin 4 of U12 to pins 1 of C1, C2, U14, U15.
- The `GND` net spans two lines (continuation `,` on the first).
- `SW` is the buck switch node, connecting U12 pin 6 to L3 pin 1.

## Continuation forms

EasyEDA Pro Telesis output uses two continuation conventions, often
mixed within the same logical entry:

- **Trailing comma**: a line ending in `,` continues on the next line.
- **Leading comma**: a line beginning with `,` (often after several
  spaces of indentation) is a continuation of the previous line. A
  bare line containing only `,` is the same.

Both forms must be merged before tokenising, or the entry's first
designator/member will be lost. Worked example:

```
HDR-TH_8P-P2.54-V-F ! HDR-TH_8P-P2.54-V-F !  ; H3 H4
,
        Jack_3.5mm_QingPu_WQP-PJ301M-12_Vertical ! Jack_3.5mm_QingPu_WQP-PJ301M-12_Vertical !  ,
        ; U8 U9 U17 U18 U19 U20 U21 U22 U24
```

Reads as two package entries: the 8-pin header (H3, H4), then a long
jack package (U8..U24). The bare `,` line is the leading-comma form.

## Parser pitfalls

- Do not split lines on whitespace alone: footprint names contain
  spaces. Use the `;` and `!` delimiters as anchors.
- Strip surrounding single/double quotes from every token before use.
- Skip every `$<KEYWORD>` section other than `$PACKAGES` and `$NETS`.
- EasyEDA Pro emits Windows line endings (`\r\n`) — strip them.
- The format is case-sensitive for net names but designators are
  conventionally uppercase.

`scripts/parse_tel.py` handles all of these.
