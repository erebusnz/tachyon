# Eurorack power header pinout (Doepfer standard)

The Doepfer A-100 standard defines two power-bus header sizes for
Eurorack modules. Both are 2.54 mm pitch, dual-row, IDC-style.

## 10-pin (2 × 5)

The popular variant for modules that don't need +5 V or CV/Gate from
the bus. Pin pattern:

| Position    | Net    |
|-------------|--------|
| End A (×2)  | -12V   |
| Middle (×6) | GND    |
| End B (×2)  | +12V   |

The cable's coloured stripe (typically red, sometimes blue) marks the
**-12 V** edge. The PCB convention is to mark the same edge with a
silkscreen stripe or arrow, and to sit pin 1 of the IDC footprint at
that edge — so on a typical layout, pins 1 and 2 carry -12 V.

This is convention, not law. Some footprint libraries number from the
opposite corner, in which case -12 V lives on pins 9 and 10. Either is
fine *electrically* — what matters is that the silkscreen stripe and
the cable's red stripe both land on the -12 V edge.

## 16-pin (2 × 8)

Adds the optional +5 V rail and two CV/Gate lines for system-level
modulation. Pin pattern (Doepfer A-100):

| Pins         | Net    | Notes                          |
|--------------|--------|--------------------------------|
| Pin 1, 2     | -12V   | Same red-stripe edge as 10-pin |
| Pin 3, 4     | GND    |                                |
| Pin 5, 6     | GND    |                                |
| Pin 7, 8     | +5V    | Optional; many busses skip it  |
| Pin 9, 10    | +12V   |                                |
| Pin 11       | CV     | Bus CV (rarely used today)     |
| Pin 12       | n/c    |                                |
| Pin 13       | Gate   | Bus Gate (rarely used today)   |
| Pin 14, 15, 16 | varies (often n/c) |                |

The 10-pin connector is essentially the 16-pin connector with the
top six pins (positions 11-16) cut off, so the lower 10 positions
keep the same -12V / GND / +5V / +12V layout — except when no +5V is
present, the pins that *would* be +5V on a 16-pin become GND on a
10-pin to give two extra GND returns.

## Audit logic

For a candidate Eurorack power header in the netlist:

1. Confirm the IC count (10 or 16 pins).
2. For 10-pin: there must be exactly two -12V pins, two +12V pins, and
   six GND pins, and the two -12V pins must be physically adjacent to
   each other (likewise +12V) — i.e., they form contiguous pairs at
   the two ends, not scattered across the connector.
3. For 16-pin: same end-pair check on -12V (pins 1, 2) and +12V (pins
   9, 10), with optional +5V on pins 7, 8.
4. Report the header's PnP centre (XY, rotation) and ask the user to
   manually confirm the silkscreen stripe sits on the -12V edge by
   inspecting `Gerber_TopSilkscreenLayer.GTO` (or the EasyEDA Pro 2D
   view). The skill can't verify silk-to-pin alignment from the
   netlist alone — the IDC footprint's pin-1 corner could be either
   end, depending on which footprint library was used.

A non-standard pinout (e.g. -12V scattered, GND missing from middle,
+5V on a 10-pin) is a layout error and must be flagged immediately:
plugging a standard cable into a non-standard module destroys the
module on power-up.
