# BLM21AG601SN1D -- 600 ohm Chip Ferrite Bead EMI Suppression Filter (0805)

## BOM Details

| Field | Value |
|---|---|
| MPN | BLM21AG601SN1D |
| Manufacturer | muRata |
| Package/Footprint | L0805 (0805 / 2012 metric) |
| Designator | L1, L2 |
| Quantity | 2 |
| Supplier Part | C85837 (LCSC) |

## Description

The BLM21AG601SN1D is a chip ferrite bead from muRata's EMIFIL (inductor type) BLM21A series, designed for EMI suppression in general electronics equipment. It provides 600 ohm impedance at 100 MHz in a compact 0805 (2.0 x 1.25 mm) package. The resistive element becomes dominant at high frequencies, converting noise energy into heat rather than reflecting it back into the circuit.

## Key Electrical Characteristics

| Parameter | Min | Typ | Max | Unit | Condition |
|---|---|---|---|---|---|
| Impedance (Z) | 450 | 600 | 750 | ohm | 100 MHz, 20 deg C (±25%) |
| Rated Current | -- | -- | 600 | mA | -- |
| DC Resistance | -- | -- | 0.21 | ohm | -- |
| Operating Temperature | -55 | -- | +125 | deg C | -- |
| Number of Circuits | -- | 1 | -- | -- | -- |

## Pinout

This is a simple 2-terminal passive device (single circuit).

| Pin | Function |
|---|---|
| 1 | Input |
| 2 | Output |

The device is non-polarized; pins 1 and 2 are interchangeable.

## Absolute Maximum Ratings

| Parameter | Value | Unit |
|---|---|---|
| Rated Current | 600 | mA |
| Operating Temperature Range | -55 to +125 | deg C |

Do not exceed the rated current. Exceeding it may create excessive heat and deteriorate the insulation resistance.

## Application Notes

- **EMI filtering purpose:** The BLM21AG601SN1D is intended for high-frequency noise suppression on signal and power lines. At 600 ohm impedance at 100 MHz, it is well suited for filtering USB, digital I/O, and other moderate-speed signal lines where broadband noise attenuation is needed.
- **Placement guidance:** Place ferrite beads in series on the signal or power line, as close to the noise source or connector as possible. Ensure the trace carrying current through the bead is short to minimize parasitic inductance from the PCB layout.
- **Rated current derating:** Do not use the product beyond its 600 mA rated current. Exceeding this limit may cause excessive self-heating and degrade insulation resistance. When operating near the current limit or at elevated ambient temperatures, consider derating accordingly.
- **Soldering notice:** Solderability of the tin-plated terminations may be degraded when using low-temperature soldering profiles where the peak solder temperature is below the tin melting point. Verify solderability before production use.
