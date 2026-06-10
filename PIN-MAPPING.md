# Pin Mapping: VERA Module RBL-XE (ESP32-S3FN8)

Target MCU: **ESP32-S3FN8** — QFN56 package, 45 GPIOs, 8 MB in-package Quad SPI flash,
512 KB SRAM, dual-core Xtensa LX7 @ 240 MHz.

---

## ⚠ Critical Boot-Time Warnings

| GPIO | QFN56 pin | Signal | Risk |
|------|-----------|--------|------|
| **GPIO 3** | 8 | (unused) | Strapping pin (JTAG source) — **no internal pull resistor, floats at reset**. Tie to GND via 10 kΩ. |
| **GPIO 26–32** | 28, 30–35 | — | Hard-wired to in-package Quad SPI flash (FN8 variant). **Never connect externally**. |
| **GPIO 45** | 51 | (unused) | Strapping pin (VDD_SPI select) — internal weak pull-down selects VDD_SPI = 3.3 V. Leave unconnected. |
| **GPIO 46** | 52 | (unused) | Strapping pin (boot mode) — internal weak pull-down. Leave unconnected. |

> **JTAG pins reclaimed (GPIO 39–42):** il firmware chiama `gpio_reset_pin()` su GPIO 39, 40,
> 41, 42 per de-registrarli dal controller JTAG e utilizzarli come GPIO normali.

> **GPIO 19–20 (pin 25–26, USB D−/D+):** USB CDC disabilitato nel firmware
> (`ARDUINO_USB_CDC_ON_BOOT=0`). Questi pin sono usati come A7 e A8 sul bus indirizzi.

> **GPIO 15–16 (pin 21–22, XTAL\_32K\_P/N):** nessun quarzo da 32 kHz presente.
> Utilizzati come normali GPIO (A3 e A4 sul bus indirizzi).

---

## 1. Data Bus D0–D7  (bidirectional, Bank 0 bits 4–11)

Tutti via level shifter **TXS0108E U1** (B side = Atari 5 V, A side = ESP32 3.3 V).

| Atari signal | GPIO | QFN56 pin | IO MUX | Bank-0 bit | Notes |
|---|---|---|---|---|---|
| D0 | 4  | 9  | GPIO4  | 4  | 60 µs LOW glitch al power-up |
| D1 | 5  | 10 | GPIO5  | 5  | |
| D2 | 6  | 11 | GPIO6  | 6  | |
| D3 | 7  | 12 | GPIO7  | 7  | |
| D4 | 8  | 13 | GPIO8  | 8  | |
| D5 | 9  | 14 | GPIO9  | 9  | |
| D6 | 10 | 15 | GPIO10 | 10 | |
| D7 | 11 | 16 | GPIO11 | 11 | |

Firmware bitmask: `DBUS_MASK = 0x00000FF0`
Firmware data decode: `data = (GPIO.in >> 4) & 0xFF`

---

## 2. Address Bus A0–A11

### A0–A7 (Bank 0, GPIO 12–19, via TXS0108E U2)

| Atari signal | GPIO | QFN56 pin | IO MUX | Bank-0 bit | Notes |
|---|---|---|---|---|---|
| A0 | 12 | 17 | GPIO12        | 12 | |
| A1 | 13 | 18 | GPIO13        | 13 | |
| A2 | 14 | 19 | GPIO14        | 14 | |
| A3 | 15 | 21 | XTAL\_32K\_P  | 15 | Pad RTC usato come GPIO; pin 20 = VDD3P3\_RTC |
| A4 | 16 | 22 | XTAL\_32K\_N  | 16 | Pad RTC usato come GPIO |
| A5 | 17 | 23 | GPIO17        | 17 | |
| A6 | 18 | 24 | GPIO18        | 18 | |
| A7 | 19 | 25 | USB\_D−       | 19 | USB disabilitato; pin condiviso con USB D− |

### A8–A9 (Bank 0, GPIO 20–21, via TXS0108E U3)

| Atari signal | GPIO | QFN56 pin | IO MUX | Bank-0 bit | Notes |
|---|---|---|---|---|---|
| A8 | 20 | 26 | USB\_D+       | 20 | USB disabilitato; pin condiviso con USB D+ |
| A9 | 21 | 27 | GPIO21        | 21 | |

### A10–A11 (Bank 1, GPIO 35–36, via TXS0108E U3)

| Atari signal | GPIO | QFN56 pin | IO MUX | Bank-1 bit | Notes |
|---|---|---|---|---|---|
| A10 | 35 | 40 | GPIO35 | 3 | Necessario solo per ROM 2 KB ($D800–$DFFF) |
| A11 | 36 | 41 | GPIO36 | 4 | Necessario solo per ROM 2 KB |

### Firmware: decode indirizzo completo

```c
/* decode_addr(g_lo, g_hi) — restituisce A0-A11 (12 bit): */
uint16_t a = (g_lo >> 12) & 0x3FF;               /* A0-A9  — Bank 0 bits 12-21 */
a |= ((g_hi >> (35 - 32)) & 1u) << 10;           /* A10    — Bank 1 bit  3     */
a |= ((g_hi >> (36 - 32)) & 1u) << 11;           /* A11    — Bank 1 bit  4     */

/* Maschere di decodifica (offset da $D000): */
/* ROM  $D800-$DFFF  2048 B  →  pbi_driver[addr & 0x7FF]  (A0-A10 usati) */
/* RAM  $D600-$D7FF   512 B  →  ram_pbi   [addr & 0x1FF]  (A0-A8  usati) */
/* VERA $D100-$D11F    32 B  →  DEV_SEL_N assert           (A0-A4  usati) */
```

---

## 3. Control Signals

Il firmware decodifica tutti gli indirizzi **interamente in software**: non esistono più segnali
decodificati hardware (D1XX\_N, ROM\_SEL\_N, RAM\_SEL\_N) come ingressi ESP32.
L'ESP32 legge A0–A11, PHI2, R/W\_ ogni ciclo e asserisce i segnali di uscita di conseguenza
tramite il **Dedicated GPIO** (istruzioni TIE Xtensa `ee.get_gpio_in` / `ee.wr_mask_gpio_out`).

| Signal | GPIO | QFN56 pin | IO MUX | Direction | Active | Level shift | Description |
|---|---|---|---|---|---|---|---|
| PHI2 | 1 | 6 | GPIO1 | Input | HIGH | via U3 | Clock fase 2 CPU 6502 — 1.79 MHz |
| R/W\_ | 2 | 7 | GPIO2 | Input | HIGH=read | via U3 | Read / Not-Write |
| EXTSEL\_N | 41 | 46 | MTDI | **Output** | LOW | via U3 | Disabilita MMU/Freddie per $D1xx e $D6xx; asserto per-ciclo. Ex-JTAG, reclaimed. |
| DEV\_SEL\_N | 40 | 45 | MTDO | **Output** | LOW | **direct** | VERA chip select (3.3 V); diretto al FPGA senza level shifter. Ex-JTAG, reclaimed. |
| MPD | 42 | 48 | MTMS | **Output** | LOW | via U3 | Math Pack Disable — disabilita ROM Atari $D800–$DFFF. Ex-JTAG, reclaimed. |
| ARESET | 37 | 42 | GPIO37 | **Output** | LOW | via U3 | Atari System Reset — pilota /RESET bus Atari (open-drain + pull-up consigliati). |
| CRESET | 38 | 43 | GPIO38 | **Output** | LOW | **direct** | VERA FPGA Reset — 3.3 V, diretto al chip FPGA. |
| CDONE | 39 | 44 | MTCK | Input | HIGH | **direct** | VERA FPGA configured status — HIGH = configurazione completata. Ex-JTAG, reclaimed. |

### Dedicated GPIO — canali usati

| Pool | Ch | GPIO | QFN56 pin | Segnale |
|------|----|------|-----------|---------|
| Input  | 0 | 1  | 6  | PHI2      |
| Output | 0 | 40 | 45 | DEV\_SEL\_N |
| Output | 1 | 41 | 46 | EXTSEL\_N   |
| Output | 2 | 42 | 48 | MPD         |

I pool input e output Dedicated GPIO sono **indipendenti** su ESP32-S3: il canale 0 in lettura
e il canale 0 in scrittura puntano a pin fisici diversi.

---

## 4. Debug / Programming

| Function | GPIO | QFN56 pin | IO MUX | Notes |
|---|---|---|---|---|
| UART TX | 43 | 49 | U0TXD | Serial debug output — UART0 hardware pin |
| UART RX | 44 | 50 | U0RXD | Serial debug input  — UART0 hardware pin |

---

## 5. Level Shifting (TXS0108E)

Tre TXS0108E traducono tra bus Atari 5 V (lato B) e ESP32-S3 3.3 V (lato A).

**Cablaggio comune a tutti e tre i chip:**

| Pin | Connessione |
|---|---|
| VCCA (pin 1) | 3.3 V |
| VCCB (pin 20) | 5 V |
| OE (pin 10) | 3.3 V (always enabled) |
| GND (pin 9) | GND |

100 nF ceramico su VCCA→GND e VCCB→GND per ciascun chip.

### U1 — Data bus D0–D7

| Canale | Lato A (3.3 V, ESP32-S3) | Lato B (5 V, Atari) |
|---|---|---|
| A1/B1 | GPIO 4  (pin  9) — D0 | Atari D0 |
| A2/B2 | GPIO 5  (pin 10) — D1 | Atari D1 |
| A3/B3 | GPIO 6  (pin 11) — D2 | Atari D2 |
| A4/B4 | GPIO 7  (pin 12) — D3 | Atari D3 |
| A5/B5 | GPIO 8  (pin 13) — D4 | Atari D4 |
| A6/B6 | GPIO 9  (pin 14) — D5 | Atari D5 |
| A7/B7 | GPIO 10 (pin 15) — D6 | Atari D6 |
| A8/B8 | GPIO 11 (pin 16) — D7 | Atari D7 |

### U2 — Address bus A0–A7

Tutti ingressi B→A (Atari → ESP32).

| Canale | Lato A (3.3 V, ESP32-S3) | Lato B (5 V, Atari) |
|---|---|---|
| A1/B1 | GPIO 12 (pin 17) — A0 | Atari A0 |
| A2/B2 | GPIO 13 (pin 18) — A1 | Atari A1 |
| A3/B3 | GPIO 14 (pin 19) — A2 | Atari A2 |
| A4/B4 | GPIO 15 (pin 21) — A3 | Atari A3 |
| A5/B5 | GPIO 16 (pin 22) — A4 | Atari A4 |
| A6/B6 | GPIO 17 (pin 23) — A5 | Atari A5 |
| A7/B7 | GPIO 18 (pin 24) — A6 | Atari A6 |
| A8/B8 | GPIO 19 (pin 25) — A7 | Atari A7 (pin condiviso USB D−) |

### U3 — A8–A11, PHI2, R/W\_, EXTSEL\_N, MPD

| Canale | Lato A (3.3 V, ESP32-S3) | Lato B (5 V) | Direzione | Note |
|---|---|---|---|---|
| A1/B1 | GPIO 20 (pin 26) — A8  | Atari A8  | B→A | Input (pin condiviso USB D+) |
| A2/B2 | GPIO 21 (pin 27) — A9  | Atari A9  | B→A | Input |
| A3/B3 | GPIO 35 (pin 40) — A10 | Atari A10 | B→A | Input |
| A4/B4 | GPIO 36 (pin 41) — A11 | Atari A11 | B→A | Input |
| A5/B5 | GPIO 1  (pin  6) — PHI2 | Atari PHI2 | B→A | Input |
| A6/B6 | GPIO 2  (pin  7) — R/W\_ | Atari R/W\_ | B→A | Input |
| A7/B7 | GPIO 41 (pin 46) — EXTSEL\_N | Atari EXTSEL | **A→B** | Output, active LOW |
| A8/B8 | GPIO 42 (pin 48) — MPD | Atari ECI MPD | **A→B** | Output, active LOW |

**Connessioni dirette (senza level shifter):**

| Signal | GPIO | QFN56 pin | Collegato a |
|---|---|---|---|
| DEV\_SEL\_N | 40 | 45 | VERA FPGA pin /CS (3.3 V) |
| CRESET | 38 | 43 | VERA FPGA pin /RESET (3.3 V) |
| CDONE | 39 | 44 | VERA FPGA pin CDONE (3.3 V) |

---

## 6. GPIO disponibili (ESP32-S3FN8 QFN56)

Pin non assegnati al progetto, esclusi strapping e flash in-package.

| GPIO | QFN56 pin | IO MUX | Note |
|---|---|---|---|
| 22–25 | — | — | Non esistono su ESP32-S3 |
| 33 | 38 | GPIO33 | Libero |
| 34 | 39 | GPIO34 | Libero |
| 47 | 53 | FSPICLK | GPIO generico (spare CONN1) |
| 48 | 54 | FSPID  | GPIO generico (spare CONN2) |

**Pin esclusi (non utilizzabili):**

| GPIO | QFN56 pin | Motivo |
|---|---|---|
| 3  | 8      | Strapping JTAG — pull-down fisso 10 kΩ a GND |
| 26–32 | 28, 30–35 | Flash in-package — mai connettere esternamente |
| 45 | 51 | Strapping VDD\_SPI — lasciare libero |
| 46 | 52 | Strapping boot mode — lasciare libero |

---

## 7. QFN56 — Pinout completo

Riferimento rapido ESP32-S3FN8 QFN56 (56 pin segnale + pad GND centrale).

| Pin | GPIO / Funzione | Uso in questo progetto |
|-----|-----------------|------------------------|
| 1   | GND | GND |
| 2   | 3V3 | 3.3 V |
| 3   | EN (CHIP\_EN) | Pull-up 10 kΩ a 3.3 V |
| 4   | — | (riservato / NC) |
| 5   | GPIO 0 | NC (strapping, non usato) |
| 6   | GPIO 1 | PHI2 input (via U3) |
| 7   | GPIO 2 | R/W\_ input (via U3) |
| 8   | GPIO 3 | Pull-down 10 kΩ a GND (strapping JTAG) |
| 9   | GPIO 4 | D0 (via U1) |
| 10  | GPIO 5 | D1 (via U1) |
| 11  | GPIO 6 | D2 (via U1) |
| 12  | GPIO 7 | D3 (via U1) |
| 13  | GPIO 8 | D4 (via U1) |
| 14  | GPIO 9 | D5 (via U1) |
| 15  | GPIO 10 | D6 (via U1) |
| 16  | GPIO 11 | D7 (via U1) |
| 17  | GPIO 12 | A0 (via U2) |
| 18  | GPIO 13 | A1 (via U2) |
| 19  | GPIO 14 | A2 (via U2) |
| 20  | VDD3P3\_RTC | Alimentazione RTC — **non GPIO** |
| 21  | GPIO 15 / XTAL\_32K\_P | A3 (via U2) — nessun quarzo 32 kHz |
| 22  | GPIO 16 / XTAL\_32K\_N | A4 (via U2) — nessun quarzo 32 kHz |
| 23  | GPIO 17 | A5 (via U2) |
| 24  | GPIO 18 | A6 (via U2) |
| 25  | GPIO 19 / USB\_D− | A7 (via U2) — USB disabilitato |
| 26  | GPIO 20 / USB\_D+ | A8 (via U3) — USB disabilitato |
| 27  | GPIO 21 | A9 (via U3) |
| 28  | GPIO 26 (FLASH SPICS0) | **Flash in-package — NC esterno** |
| 29  | VDD\_SPI | Alimentazione flash — **non GPIO** |
| 30  | GPIO 27 (FLASH SPIQ)  | **Flash in-package — NC esterno** |
| 31  | GPIO 28 (FLASH SPIWP) | **Flash in-package — NC esterno** |
| 32  | GPIO 29 (FLASH SPICLK)| **Flash in-package — NC esterno** |
| 33  | GPIO 30 (FLASH SPID)  | **Flash in-package — NC esterno** |
| 34  | GPIO 31 (FLASH SPIHD) | **Flash in-package — NC esterno** |
| 35  | GPIO 32 (FLASH SPICS1)| **Flash in-package — NC esterno** |
| 36  | — | (riservato / NC) |
| 37  | — | (riservato / NC) |
| 38  | GPIO 33 | Libero |
| 39  | GPIO 34 | Libero |
| 40  | GPIO 35 | A10 (via U3) |
| 41  | GPIO 36 | A11 (via U3) |
| 42  | GPIO 37 | ARESET output (via U3) |
| 43  | GPIO 38 | CRESET output (diretto VERA) |
| 44  | GPIO 39 / MTCK | CDONE input (diretto VERA) — ex-JTAG |
| 45  | GPIO 40 / MTDO | DEV\_SEL\_N output (diretto VERA) — ex-JTAG |
| 46  | GPIO 41 / MTDI | EXTSEL\_N output (via U3) — ex-JTAG |
| 47  | VDD3P3\_CPU | Alimentazione CPU — **non GPIO** |
| 48  | GPIO 42 / MTMS | MPD output (via U3) — ex-JTAG |
| 49  | GPIO 43 / U0TXD | UART0 TX — debug seriale |
| 50  | GPIO 44 / U0RXD | UART0 RX — debug seriale |
| 51  | GPIO 45 | NC (strapping VDD\_SPI) |
| 52  | GPIO 46 | NC (strapping boot) |
| 53  | GPIO 47 / FSPICLK | Spare CONN1 |
| 54  | GPIO 48 / FSPID  | Spare CONN2 |
| 55  | XTAL\_N | Quarzo principale 40 MHz |
| 56  | XTAL\_P | Quarzo principale 40 MHz |
| EP  | GND (pad centrale) | GND |
