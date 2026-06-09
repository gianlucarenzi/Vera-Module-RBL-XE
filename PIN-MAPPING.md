# Pin Mapping: VERA Module RBL-XE (ESP32-S3FN8)

Target MCU: **ESP32-S3FN8** — QFN56 package, 45 GPIOs, 8 MB in-package Quad SPI flash,
512 KB SRAM, dual-core Xtensa LX7 @ 240 MHz.

---

## ⚠ Critical Boot-Time Warnings

| GPIO | QFN56 pin | Signal | Risk |
|------|-----------|--------|------|
| **GPIO 0** | 5 | EXTSEL_N | Strapping pin — if LOW at power-on, ESP32-S3 enters download mode. Add a **10 kΩ pull-up** to 3.3 V so the Atari bus cannot hold it LOW during reset. |
| **GPIO 3** | 8 | (unused) | Strapping pin (JTAG source) — **no internal pull resistor, floats at reset**. Must be externally pulled; tie to GND via 10 kΩ resistor. |
| **GPIO 15–16** | 21–22 | A9, A10 | XTAL_32K_P/N pads reused as GPIO. **Do not connect a 32 kHz crystal.** |
| **GPIO 26–32** | 28, 30–35 | — | Hard-wired to in-package Quad SPI flash (FN8 variant). **Never connect externally**. |
| **GPIO 45** | 51 | (unused) | Strapping pin (VDD_SPI select) — internal weak pull-down selects VDD_SPI = 3.3 V. Leave unconnected. |
| **GPIO 46** | 52 | (unused) | Strapping pin (boot mode) — internal weak pull-down. Leave unconnected. |
| **GPIO 19–20** | 25–26 | (unused) | USB D−/D+. USB CDC disabled in firmware. Leave unconnected or use carefully. |

---

## 1. Data Bus D0–D7  (bidirectional, GPIO bank 0, bits 4–11)

| Atari signal | GPIO | QFN56 pin | Bank-0 bit | Notes |
|---|---|---|---|---|
| D0 | 4 | 9 | 4 | Bidirectional via TXS0108E U1 — 60 µs LOW glitch at power-up |
| D1 | 5 | 10 | 5 | 60 µs LOW glitch at power-up |
| D2 | 6 | 11 | 6 | 60 µs LOW glitch at power-up |
| D3 | 7 | 12 | 7 | 60 µs LOW glitch at power-up |
| D4 | 8 | 13 | 8 | 60 µs LOW glitch at power-up |
| D5 | 9 | 14 | 9 | 60 µs LOW glitch at power-up |
| D6 | 10 | 15 | 10 | 60 µs LOW glitch at power-up |
| D7 | 11 | 16 | 11 | 60 µs LOW glitch at power-up |

Firmware bitmask: `DBUS_MASK = 0x00000FF0`
Firmware data decode: `data = (GPIO.in >> 4) & 0xFF`

---

## 2. Address Bus A0–A10

### Bank 1 (GPIO.in1.val, GPIO 33–38, bits 1–6)

| Atari signal | GPIO | QFN56 pin | Bank-1 bit | Notes |
|---|---|---|---|---|
| A0 | 33 | 38 | 1 | Via TXS0108E U2 |
| A1 | 34 | 39 | 2 | |
| A2 | 35 | 40 | 3 | |
| A3 | 36 | 41 | 4 | |
| A4 | 37 | 42 | 5 | |
| A5 | 38 | 43 | 6 | |

Firmware A0–A5 decode: `a = (GPIO.in1.val >> 1) & 0x3F`

### Bank 0 (GPIO.in, GPIO 12–16) — PBI mode only

| Atari signal | GPIO | QFN56 pin | IO MUX name | Bank-0 bit | Notes |
|---|---|---|---|---|---|
| A6 | 12 | 17 | GPIO12 | 12 | Via TXS0108E U2 — 60 µs LOW glitch at power-up |
| A7 | 13 | 18 | GPIO13 | 13 | 60 µs LOW glitch at power-up |
| A8 | 14 | 19 | GPIO14 | 14 | 60 µs LOW glitch at power-up |
| A9 | 15 | 21 | **XTAL_32K_P** | 15 | Do **not** connect a 32 kHz crystal |
| A10 | 16 | 22 | **XTAL_32K_N** | 16 | Do **not** connect a 32 kHz crystal |

> **Note:** GPIO15 and GPIO16 share the 32 kHz crystal pads (XTAL_32K_P / XTAL_32K_N).
> When used as regular GPIOs, no external 32 kHz oscillator may be connected.
> Physical pin 20 between GPIO14 (pin 19) and XTAL_32K_P (pin 21) is VDD3P3_RTC (power, no signal).

---

## 3. Control Signals

| Signal | GPIO | QFN56 pin | IO MUX name | Direction | Active | Description |
|---|---|---|---|---|---|---|
| PHI2 | 2 | 7 | GPIO2 | Input | HIGH | CPU clock phase 2 — 60 µs LOW glitch at power-up |
| R/W_ | 17 | 23 | GPIO17 | Input | HIGH=read | Read / Not-Write — 60 µs LOW glitch at power-up |
| D1XX_N | 18 | 24 | GPIO18 | Input | LOW | $D1xx page selected (external decoder) — 60 µs LOW+HIGH glitch |
| ROM_SEL_N | 21 | 27 | GPIO21 | Input | LOW | $D800–$DFFF from 74HC138 Y7 |
| EXTSEL_N | 0 | 5 | GPIO0 | **Output** | LOW | Disables Atari floating-point ROM — strapping pin, add 10 kΩ pull-up |
| DEV_SEL_N | 1 | 6 | GPIO1 | **Output** | LOW | VERA chip select (direct, no level shifter) — 60 µs LOW glitch |

---

## 4. Debug / Programming

| Function | GPIO | QFN56 pin | IO MUX name | Notes |
|---|---|---|---|---|
| UART TX | 43 | 49 | U0TXD | Serial debug output (S3 UART0 hardware pin) |
| UART RX | 44 | 50 | U0RXD | S3 UART0 hardware pin — RX disabled in firmware |

---

## 5. Level Shifting (TXS0108E)

Three TXS0108E chips translate between the 5 V Atari bus (B side) and 3.3 V ESP32-S3 (A side).

**Common wiring for all three chips:**

| Pin | Connection |
|---|---|
| VCCA (pin 1) | 3.3 V |
| VCCB (pin 20) | 5 V |
| OE (pin 10) | 3.3 V (always enabled) |
| GND (pin 9) | GND |

Place 100 nF ceramic capacitors on VCCA→GND and VCCB→GND of each chip.

### U1 — Data bus D0–D7

| Channel | A side (3.3 V ESP32-S3) | B side (5 V Atari) |
|---|---|---|
| A1/B1 | GPIO 4  — D0 | Atari D0 |
| A2/B2 | GPIO 5  — D1 | Atari D1 |
| A3/B3 | GPIO 6  — D2 | Atari D2 |
| A4/B4 | GPIO 7  — D3 | Atari D3 |
| A5/B5 | GPIO 8  — D4 | Atari D4 |
| A6/B6 | GPIO 9  — D5 | Atari D5 |
| A7/B7 | GPIO 10 — D6 | Atari D6 |
| A8/B8 | GPIO 11 — D7 | Atari D7 |

### U2 — Address bus A0–A7

| Channel | A side (3.3 V ESP32-S3) | B side (5 V Atari) |
|---|---|---|
| A1/B1 | GPIO 33 — A0 | Atari A0 |
| A2/B2 | GPIO 34 — A1 | Atari A1 |
| A3/B3 | GPIO 35 — A2 | Atari A2 |
| A4/B4 | GPIO 36 — A3 | Atari A3 |
| A5/B5 | GPIO 37 — A4 | Atari A4 |
| A6/B6 | GPIO 38 — A5 | Atari A5 |
| A7/B7 | GPIO 12 — A6 | Atari A6 |
| A8/B8 | GPIO 13 — A7 | Atari A7 |

### U3 — Address A8–A10, control signals, EXTSEL_N

| Channel | A side (3.3 V ESP32-S3) | B side (5 V) | Direction | Source |
|---|---|---|---|---|
| A1/B1 | GPIO 14 — A8 | Atari A8 | B→A | Atari |
| A2/B2 | GPIO 15 — A9 | Atari A9 | B→A | Atari |
| A3/B3 | GPIO 16 — A10 | Atari A10 | B→A | Atari |
| A4/B4 | GPIO 2  — PHI2 | Atari PHI2 | B→A | Atari |
| A5/B5 | GPIO 17 — R/W_ | Atari R/W_ | B→A | Atari |
| A6/B6 | GPIO 18 — D1XX_N | External decoder | B→A | External |
| A7/B7 | GPIO 21 — ROM_SEL_N | 74HC138 Y7 | B→A | 74HC138 |
| A8/B8 | GPIO 0  — EXTSEL_N | Atari EXTSEL | **A→B** | **ESP32-S3** |

**Note:** DEV_SEL_N (GPIO 1) connects directly from ESP32-S3 to VERA chip select
without level shifting — the FPGA operates at 3.3 V.

---

## 6. 74HC138 Address Decoder

Generates ROM_SEL_N (active LOW) for $D800–$DFFF.

| 74HC138 pin | Connected to |
|---|---|
| Pin 1 (A) | Atari A11 |
| Pin 2 (B) | Atari A12 |
| Pin 3 (C) | Atari A15 |
| Pin 4 (G2A, active LOW) | Atari A13 |
| Pin 5 (G2B, active LOW) | GND |
| Pin 6 (G1, active HIGH) | Atari A14 |
| Pin 7 (Y7) | U3 B7 → GPIO 21 |
| Pin 16 (VCC) | 5 V |

Decode: A15=1, A14=1, A13=0, A12=1, A11=1 → Y7 LOW → **$D800–$DFFF** ✓

---

## 7. ESP32-S3FN8 vs ESP32-PICO-D4 — Migration Notes

| Parameter | ESP32-PICO-D4 | ESP32-S3FN8 |
|---|---|---|
| CPU | Dual-core LX6 @ 240 MHz | Dual-core LX7 @ 240 MHz |
| SRAM | 520 KB | 512 KB |
| In-package Flash | 4 MB (Quad SPI) | 8 MB (Quad SPI) |
| Package | QFN48 (7×7 mm) | QFN56 (7×7 mm) |
| GPIO count | 34 usable | 45 total (38 usable excl. flash) |
| Input-only pins | GPIO34–39 (input only) | None — all GPIOs bidirectional |
| Bluetooth | BT 4.2 | BT 5 (LE) |
| USB | None | Full-speed USB OTG + Serial/JTAG |
| UART0 pins | TX=GPIO1, RX=GPIO3 | TX=GPIO43, RX=GPIO44 |
| Strapping pins | GPIO0, GPIO2, GPIO5, GPIO12, GPIO15 | GPIO0, GPIO3, GPIO45, GPIO46 |
| Flash-reserved pins | GPIO6–11 | GPIO26–32 |
