# Pin Mapping: VERA Module RBL-XE (ESP32 — unified with BOBOARD-5V)

Pin assignments aligned with the ESP32-BOBOARD-5V reference firmware.
The same GPIO numbers apply to both boards; only the physical package differs
(PICO-D4 vs WROOM/NodeMCU-32S).

---

## ⚠ Critical Boot-Time Warnings

| GPIO | Signal | Risk |
|------|--------|------|
| **GPIO 0** | EXTSEL_N | Bootstrap pin — if LOW at power-on, ESP32 enters download mode. Add a **10 kΩ pull-up** to 3.3 V so the Atari bus cannot hold it LOW during ESP32 reset. |
| **GPIO 3** | DEV_SEL_N (VERA CS) | UART0 RX pin. Firmware disables serial RX: `Serial.begin(115200, SERIAL_8N1, -1, 1)`. Do **not** connect a USB-UART adapter to GPIO 3 while the Atari is powered. |
| **GPIO 12** | A9 | Strapping pin — selects 1.8 V flash VDD if HIGH at boot. The A9 line must be LOW (or floating with internal pull-down) during ESP32 reset. |
| **GPIO 34, 35, 36, 39** | A2, A3, A4, A5 | Hardware input-only — no output driver, no internal pull resistors. Correct for address-bus inputs. |

**PICO-D4 additional restriction:** GPIOs 6, 7, 8, 9, 10, 11 are wired internally to the SPI flash. They must not be connected externally.

---

## 1. Data Bus D0–D7  (bidirectional, GPIO bank 0)

| Atari signal | GPIO | GPIO_IN bit | Notes |
|---|---|---|---|
| D0 | 18 | 18 | Bidirectional via TXS0108E |
| D1 | 19 | 19 | |
| D2 | 21 | 21 | |
| D3 | 22 | 22 | |
| D4 | 23 | 23 | |
| D5 | 25 | 25 | |
| D6 | 26 | 26 | |
| D7 | 27 | 27 | |

Firmware bitmask: `DBUS_MASK = (1<<18)|(1<<19)|(1<<21)|(1<<22)|(1<<23)|(1<<25)|(1<<26)|(1<<27)`

---

## 2. Address Bus A0–A10

### Bank 1 (GPIO_IN1 register, GPIO 32–39)

| Atari signal | GPIO | GPIO_IN1 bit | Notes |
|---|---|---|---|
| A0 | 32 | 0 | Via TXS0108E |
| A1 | 33 | 1 | |
| A2 | 34 | 2 | Input-only pin |
| A3 | 35 | 3 | Input-only pin |
| A4 | 36 | 4 | Input-only pin (VP) |
| A5 | 39 | 7 | Input-only pin (VN) |

### Bank 0 (GPIO_IN register)

| Atari signal | GPIO | GPIO_IN bit | Notes |
|---|---|---|---|
| A6 | 16 | 16 | Via TXS0108E |
| A7 | 17 | 17 | |
| A8 | 14 | 14 | |
| A9 | 12 | 12 | Strapping pin — see warning above |
| A10 | 13 | 13 | |

---

## 3. Control Signals

| Signal | GPIO | Direction | Active | Description |
|---|---|---|---|---|
| PHI2 | 2 | Input | HIGH | CPU clock phase 2 |
| R/W_ | 15 | Input | HIGH=read | Read / Not-Write |
| D1XX_N | 5 | Input | LOW | $D1xx page selected (external decoder) |
| ROM_SEL_N | 4 | Input | LOW | $D800–$DFFF from 74HC138 Y7 |
| EXTSEL_N | 0 | **Output** | LOW | Disables Atari floating-point ROM |
| DEV_SEL_N | 3 | **Output** | LOW | VERA chip select |

---

## 4. Debug / Programming

| Function | GPIO | Notes |
|---|---|---|
| UART TX | 1 | Serial debug output |
| UART RX | 3 | **Repurposed as DEV_SEL_N** — serial RX disabled in firmware |

---

## 5. Level Shifting (TXS0108E)

Three TXS0108E chips translate between the 5 V Atari bus (B side) and 3.3 V ESP32 (A side).

**Common wiring for all three chips:**

| Pin | Connection |
|---|---|
| VCCA (pin 1) | 3.3 V |
| VCCB (pin 20) | 5 V |
| OE (pin 10) | 3.3 V (always enabled) |
| GND (pin 9) | GND |

Place 100 nF ceramic capacitors on VCCA→GND and VCCB→GND of each chip.

### U1 — Data bus D0–D7

| Channel | A side (3.3 V ESP32) | B side (5 V Atari) |
|---|---|---|
| A1/B1 | GPIO 18 — D0 | Atari D0 |
| A2/B2 | GPIO 19 — D1 | Atari D1 |
| A3/B3 | GPIO 21 — D2 | Atari D2 |
| A4/B4 | GPIO 22 — D3 | Atari D3 |
| A5/B5 | GPIO 23 — D4 | Atari D4 |
| A6/B6 | GPIO 25 — D5 | Atari D5 |
| A7/B7 | GPIO 26 — D6 | Atari D6 |
| A8/B8 | GPIO 27 — D7 | Atari D7 |

### U2 — Address bus A0–A7

| Channel | A side (3.3 V ESP32) | B side (5 V Atari) |
|---|---|---|
| A1/B1 | GPIO 32 — A0 | Atari A0 |
| A2/B2 | GPIO 33 — A1 | Atari A1 |
| A3/B3 | GPIO 34 — A2 | Atari A2 |
| A4/B4 | GPIO 35 — A3 | Atari A3 |
| A5/B5 | GPIO 36 — A4 | Atari A4 |
| A6/B6 | GPIO 39 — A5 | Atari A5 |
| A7/B7 | GPIO 16 — A6 | Atari A6 |
| A8/B8 | GPIO 17 — A7 | Atari A7 |

### U3 — Address A8–A10, control signals, EXTSEL_N

| Channel | A side (3.3 V ESP32) | B side (5 V) | Direction | Source |
|---|---|---|---|---|
| A1/B1 | GPIO 14 — A8 | Atari A8 | B→A | Atari |
| A2/B2 | GPIO 12 — A9 | Atari A9 | B→A | Atari |
| A3/B3 | GPIO 13 — A10 | Atari A10 | B→A | Atari |
| A4/B4 | GPIO 2 — PHI2 | Atari PHI2 | B→A | Atari |
| A5/B5 | GPIO 15 — R/W_ | Atari R/W_ | B→A | Atari |
| A6/B6 | GPIO 5 — D1XX_N | External decoder | B→A | External |
| A7/B7 | GPIO 4 — ROM_SEL_N | 74HC138 Y7 | B→A | 74HC138 |
| A8/B8 | GPIO 0 — EXTSEL_N | Atari EXTSEL | **A→B** | **ESP32** |

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
| Pin 7 (Y7) | U3 B7 → GPIO 4 |
| Pin 16 (VCC) | 5 V |

Decode: A15=1, A14=1, A13=0, A12=1, A11=1 → Y7 LOW → **$D800–$DFFF** ✓
