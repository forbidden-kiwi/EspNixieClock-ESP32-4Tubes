# Bill of Materials — EspNixie IN-17 (4 tubes)

Parts for the main clock PCB. Source CSV: [`EspNixieIn17.csv`](EspNixieIn17.csv).

The **60× WS2812 (1010)** LEDs belong to the [seconds ring PCB](../Gerber/Neopixel-1010-Ring-Gerber.zip). Tube backlight uses **SK6812MINI** (D1–D4) on the main board.

Mouser links are convenience lookups — verify package and voltage rating before ordering.

## Capacitors

| Ref | Value | Footprint | Qty | Mouser |
|-----|-------|-----------|-----|--------|
| C1, C6, C7, C8 | 100nF 50V | 0805 | 4 | [80-C0805C104Z5U](https://www.mouser.com/ProductDetail/80-C0805C104Z5U) |
| C2, C3 | 1uF | 0805 | 2 | [603-CC805KFX7R7BB105](https://www.mouser.com/ProductDetail/603-CC805KFX7R7BB105) |
| C4, C5 | 100nF | 0805 | 2 | [187-CL21B104KBCNNNC](https://www.mouser.com/ProductDetail/187-CL21B104KBCNNNC) |

## Resistors

| Ref | Value | Footprint | Qty | Mouser |
|-----|-------|-----------|-----|--------|
| R1, R2, R8, R9 | 39k | 0805 | 4 | [708-RMCF0805FT39K0](https://www.mouser.com/ProductDetail/708-RMCF0805FT39K0) |
| R4, R7 | 300k | 0805 | 2 | [603-RC0805JR-07300KL](https://www.mouser.com/ProductDetail/603-RC0805JR-07300KL) |
| R5, R6 | 220k | 0805 | 2 | [652-CR0805JW-224ELF](https://www.mouser.com/ProductDetail/652-CR0805JW-224ELF) |
| R21 | 10K | 0805 | 1 | [71-CRCW0805-10K-E3](https://www.mouser.com/ProductDetail/71-CRCW0805-10K-E3) |

## Semiconductors / ICs

| Ref | Value | Footprint | Qty | Mouser |
|-----|-------|-----------|-----|--------|
| U2, U3 | HV5622PG-G | — | 2 | [689-HV5622PG-G](https://www.mouser.com/ProductDetail/689-HV5622PG-G) |
| U4 | CD4504BM | — | 1 | [595-CD4504BM](https://www.mouser.com/ProductDetail/595-CD4504BM) |

## Displays / indicators

| Ref | Value | Footprint | Qty | Mouser |
|-----|-------|-----------|-----|--------|
| NX1–NX4 | IN-17 Nixie Tube | — | 4 | — |
| DS1, DS2 | A1C | — | 2 | [606-A1C-T](https://www.mouser.com/ProductDetail/606-A1C-T) |
| D1–D4 | SK6812MINI | — | 4 | — |

## Power

| Ref | Value | Footprint | Qty | Mouser |
|-----|-------|-----------|-----|--------|
| T1 | Nixie Power Supply | — | 1 | — |

## Seconds ring (separate PCB)

| Ref | Value | Footprint | Qty | Mouser |
|-----|-------|-----------|-----|--------|
| LED (Seconds Ring) | WS2812 | 1010 | 60 | — |

## Notes

- Controller board: **Waveshare ESP32-C6-LCD-1.47** (not listed above — buy as module).
- Schematics: [`Schematics/Schematics-v1.0-IN-17.pdf`](../Schematics/Schematics-v1.0-IN-17.pdf)
- Gerbers: [`Gerber/EspNixie-IN-17-Gerber.zip`](../Gerber/EspNixie-IN-17-Gerber.zip), [`Gerber/Neopixel-1010-Ring-Gerber.zip`](../Gerber/Neopixel-1010-Ring-Gerber.zip)
