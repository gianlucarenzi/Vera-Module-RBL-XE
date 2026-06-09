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

> **Logica di decodifica:** 74HCT138 / 74HCT04 / 74HCT10 operano a 5 V con ingressi
> collegati direttamente al bus Atari. Le loro uscite pilotano uno stadio finale in serie
> **74LVC** alimentato a 3.3 V (ingressi 5V-tolerant). Le uscite 74LVC — `/D1XX_N`,
> `/ROM_SEL_N`, `/RAM_SEL_N` — escono già a 3.3 V e si collegano **direttamente** ai
> GPIO dell'ESP32-S3 senza ulteriori level shifter.
> PHI2, R/W\_ e A8–A10 provengono invece direttamente dal bus Atari a 5 V e passano
> per U3 (TXS0108E).

| Signal | GPIO | QFN56 pin | IO MUX name | Direction | Active | Level shift | Description |
|---|---|---|---|---|---|---|---|
| PHI2 | 2 | 7 | GPIO2 | Input | HIGH | via U3 | CPU clock phase 2 — 60 µs LOW glitch at power-up |
| R/W_ | 17 | 23 | GPIO17 | Input | HIGH=read | via U3 | Read / Not-Write — 60 µs LOW glitch at power-up |
| D1XX_N | 18 | 24 | GPIO18 | Input | LOW | **direct** | $D1xx page — uscita 74LVC a 3.3 V |
| ROM_SEL_N | 21 | 27 | GPIO21 | Input | LOW | **direct** | $D800–$DFFF — uscita 74LVC a 3.3 V |
| RAM_SEL_N | 40 | 45 | MTDO | Input | LOW | **direct** | $D600–$D7FF — uscita 74LVC a 3.3 V (bank-1 bit 8, PBI only) |
| EXTSEL_N | 0 | 5 | GPIO0 | **Output** | LOW | via U3 | Disabilita MMU/Freddie per accesso D1xx ($D100-$D1FE) e RAM $D600-$D7FF — segnale per-ciclo; strapping pin, pull-up 10 kΩ a 3.3 V |
| DEV_SEL_N | 1 | 6 | GPIO1 | **Output** | LOW | **direct** | VERA chip select — VERA a 3.3 V; 60 µs LOW glitch al power-up |
| MPD | 42 | 48 | MTMS | **Output** | LOW | via U3 | Math Pack Disable — disabilita ROM Atari $D800-$DFFF; asserto solo durante accessi ROM con device selezionato (bank-1 bit 10, U3 canale A6/B6) |

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

### U3 — Address A8–A10, PHI2, R/W\_, MPD, EXTSEL\_N

D1XX\_N e ROM\_SEL\_N non passano più per U3: le uscite 74LVC (3.3 V) si collegano
direttamente all'ESP32. I canali A6/B6 e A7/B7 risultano liberi; A6/B6 è ora
assegnato a MPD (GPIO 42, output ESP32-S3 → bus Atari 5 V). A7/B7 è di riserva.

| Channel | A side (3.3 V ESP32-S3) | B side (5 V) | Direction | Note |
|---|---|---|---|---|
| A1/B1 | GPIO 14 — A8 | Atari A8 | B→A | Input |
| A2/B2 | GPIO 15 — A9 | Atari A9 | B→A | Input |
| A3/B3 | GPIO 16 — A10 | Atari A10 | B→A | Input |
| A4/B4 | GPIO 2  — PHI2 | Atari PHI2 | B→A | Input |
| A5/B5 | GPIO 17 — R/W_ | Atari R/W_ | B→A | Input |
| A6/B6 | GPIO 42 — MPD | Atari ECI MPD | **A→B** | Output, active LOW |
| A7/B7 | — | — | — | **Spare** |
| A8/B8 | GPIO 0  — EXTSEL_N | Atari EXTSEL | **A→B** | Output, active LOW |

**Note:** DEV_SEL_N (GPIO 1) va direttamente al chip select di VERA (3.3 V) senza
level shifter. RAM\_SEL\_N, ROM\_SEL\_N, D1XX\_N vanno direttamente all'ESP32 (74LVC a 3.3 V).

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
| Pin 7 (Y7) | → 74LVC → GPIO 21 (ROM_SEL_N, 3.3 V) |
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
