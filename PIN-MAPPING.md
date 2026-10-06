# Pin Mapping: VERA Module RBL-XE (ESP32-S3FN8)

Target MCU: **ESP32-S3FN8** — QFN56 package, 45 GPIOs, 8 MB in-package Quad SPI flash,
512 KB SRAM, dual-core Xtensa LX7 @ 240 MHz.

> **Documenti collegati:** requisiti e verifiche hardware emersi dal lavoro su driver ed emulatore
> in [`VERA-ATARI-HW-REQUIREMENTS.md`](VERA-ATARI-HW-REQUIREMENTS.md).
>
> **Nota sui riferimenti:** nella netlist (2026-09-01) `U6` è il transceiver Atari→scheda
> (PHI2, R/W, D1XX, S4, S5, CCTL, REFRESH) e `U16` quello scheda→Atari (EXTSEL, MPD, RESET,
> HALT, RD4, RD5, IRQ). Le versioni precedenti di questo file li avevano scambiati.

---

## ⚠ Critical Boot-Time Warnings

| GPIO | QFN56 pin | Signal | Risk |
|------|-----------|--------|------|
| **GPIO 0** | 5 | BOOT0 | Strapping pin (boot mode) — **non è NC**: rete `BOOT0` con `R33` 4,7 kΩ → 3V3, pulsante `SW3`, `C61` 100 nF e `R54` 220 Ω verso `Q4` (auto-reset del programmatore). LOW al reset = modalità download. |
| **GPIO 3** | 8 | RAMBO\_EN | Strapping pin (JTAG source, no internal pull) — usato come **RAMbo hardware enable**: pull-up 10 kΩ → VCC = RAMbo presente; pull-down 10 kΩ → GND = assente. Letto una volta in `setup()`. |
| **GPIO 26–32** | 28, 30–35 | — | Hard-wired to in-package Quad SPI flash (FN8 variant). **Never connect externally**. |
| **GPIO 45** | 51 | A14 | Strapping pin (VDD_SPI select) — internal weak pull-down (~5 kΩ). Connesso ad A14 via 74LVC4245APW_118 U3. Al power-on il 6502 è in reset → A14 alta impedenza → U3 flotta → pull-down → **LOW → VDD_SPI da LDO interno (~1.8 V)**. Sicuro per variante FN8 (flash on-package). |
| **GPIO 46** | 52 | A15 | Strapping pin (ROM messages on UART0) — internal weak pull-down (~5 kΩ). Connesso ad A15 via 74LVC4245APW_118 U3. Stesso scenario di GPIO 45 → **LOW → messaggi ROM disabilitati** (corretto per produzione). |

> **JTAG pins reclaimed (GPIO 39–42):** il firmware chiama `gpio_reset_pin()` su GPIO 39, 40,
> 41, 42 per de-registrarli dal controller JTAG e utilizzarli come GPIO normali.

> **GPIO 19–20 (pin 25–26, USB D−/D+):** USB CDC disabilitato nel firmware
> (`ARDUINO_USB_CDC_ON_BOOT=0`). Questi pin sono usati come A7 e A8 sul bus indirizzi.

> **GPIO 15–16 (pin 21–22, XTAL\_32K\_P/N):** nessun quarzo da 32 kHz presente.
> Utilizzati come normali GPIO (A3 e A4 sul bus indirizzi).

---

## 1. Data Bus D0–D7  (bidirectional, Bank 0 bits 4–11)

Tutti via level shifter **74LVC4245APW_118 U19** (B side = Atari 5 V, A side = ESP32 3.3 V) —
unico canale realmente **bidirezionale** del progetto: la direzione (pin DIR) è commutata
dinamicamente da due invertitori **SN74HCT04DR U23**, pilotati da R/W\_ e PHI2, invece
dell'auto-sensing analogico del vecchio TXS0108E. Vedere § 6.

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

## 2. Address Bus A0–A15

### A0–A7 (Bank 0, GPIO 12–19, via 74LVC4245APW_118 U4)

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

### A8–A15 (Bank 0/1, GPIO 20–21, 35–36, 33–34, 45–46, via 74LVC4245APW_118 U3)

> **Hardware:** un solo chip **U3** (8 canali) copre l'intero bus indirizzi alto A8–A15,
> unificando quello che nella revisione TXS0108E era diviso su due package separati.
> GPIO 33/34 (pin 38–39) e GPIO 45/46 (pin 51–52) restano su lati opposti del QFN56.
> **Routing PCB:** applicare *length matching* — tutte le tracce di A12–A15 portate alla
> lunghezza della traccia più lunga (A14 o A15) tramite serpentine, per eliminare lo skew
> di propagazione tra i quattro bit.

| Atari signal | GPIO | QFN56 pin | IO MUX | Bank bit | Notes |
|---|---|---|---|---|---|
| A8  | 20 | 26 | USB\_D+ | Bank 0 bit 20 | USB disabilitato; pin condiviso con USB D+ |
| A9  | 21 | 27 | GPIO21  | Bank 0 bit 21 | |
| A10 | 35 | 40 | GPIO35  | Bank 1 bit 3  | |
| A11 | 36 | 41 | GPIO36  | Bank 1 bit 4  | |
| A12 | 33 | 38 | GPIO33  | Bank 1 bit 1  | Ex-spare CONN / libre |
| A13 | 34 | 39 | GPIO34  | Bank 1 bit 2  | Ex-spare CONN / libre |
| A14 | 45 | 51 | GPIO45  | Bank 1 bit 13 | Strapping pin (VDD_SPI); pull-down → LOW al boot (sicuro FN8) |
| A15 | 46 | 52 | GPIO46  | Bank 1 bit 14 | Strapping pin (ROM msgs); pull-down → LOW al boot (corretto) |

### Firmware: decode indirizzo completo (16 bit)

```c
/* decode_addr(g_lo, g_hi) — restituisce A0-A15 (16 bit): */
uint16_t a = (g_lo >> 12) & 0x3FF;               /* A0-A9  — Bank 0 bits 12-21 */
a |= ((g_hi >> (35 - 32)) & 1u) << 10;           /* A10    — Bank 1 bit  3     */
a |= ((g_hi >> (36 - 32)) & 1u) << 11;           /* A11    — Bank 1 bit  4     */
a |= ((g_hi >> (33 - 32)) & 1u) << 12;           /* A12    — Bank 1 bit  1     */
a |= ((g_hi >> (34 - 32)) & 1u) << 13;           /* A13    — Bank 1 bit  2     */
a |= ((g_hi >> (45 - 32)) & 1u) << 14;           /* A14    — Bank 1 bit 13     */
a |= ((g_hi >> (46 - 32)) & 1u) << 15;           /* A15    — Bank 1 bit 14     */
```

La modalità operativa è selezionata a compile time tramite `VERA_BOARD_IS_PBI` in `main.cpp`:

| `VERA_BOARD_IS_PBI` | Modalità | Range attivi | Latch selezione |
|---------------------|----------|--------------|-----------------|
| `1` (default) | **PBI**  | $D100–$D1FF, $D600–$D7FF, $D800–$DFFF | write `0x80` → $D1FF |
| `0`           | **CCTL** | $D500–$D5FF                           | write `0x80` → $D5FF |

```c
/* Confronti indirizzi — PBI mode (VERA_BOARD_IS_PBI = 1): */
/* VERA regs  $D100-$D11F   32 B  →  EXTSEL_N + DEV_SEL_N (se selezionato) */
/* PBI latch  $D1FF              →  write 0x80 = seleziona scheda           */
/* RAM        $D600-$D7FF  512 B  →  ram_pbi[addr & 0x1FF]  (ESP32 drive)   */
/* ROM        $D800-$DFFF 2048 B  →  pbi_driver[addr & 0x7FF] (ESP32 drive) */

/* Confronti indirizzi — CCTL mode (VERA_BOARD_IS_PBI = 0): */
/* CCTL range $D500-$D5FE       →  DEV_SEL_N (se selezionato, FPGA drive)  */
/* CCTL latch $D5FF             →  write 0x80 = seleziona scheda            */
```

---

## 3. Control Signals

Il firmware decodifica tutti gli indirizzi **interamente in software**: non esistono segnali
decodificati hardware (D1XX\_N, ROM\_SEL\_N, RAM\_SEL\_N) come ingressi ESP32.
L'ESP32 legge A0–A15, PHI2, R/W\_ ogni ciclo e asserisce i segnali di uscita di conseguenza
tramite il **Dedicated GPIO** (istruzioni TIE Xtensa `ee.get_gpio_in` / `ee.wr_mask_gpio_out`).
Con A0–A15 completi il decode è esatto: nessuna ambiguità di pagina.

> **Perché non i chip-select hardware:** sull'ESP32-S3FN8 non ci sono GPIO liberi da
> dedicare a ingressi per i chip-select hardware disponibili sui connettori Atari
> (`/D1XX`, `/S4`, `/S5`, `/CCTL` — vedere § 7, tutti i GPIO non riservati sono già
> assegnati). Inoltre l'emulazione RAMbo 256 KB richiede comunque di leggere l'intero
> bus indirizzi A0–A15 (per decodificare la finestra $4000–$7FFF) e di intercettare le
> scritture su PORTB ($D301), quindi anche con GPIO disponibili i chip-select hardware
> non risparmierebbero pin sul bus indirizzi: la decodifica software è l'unica strada.

La modalità operativa (PBI o CCTL) è selezionata a compile time con `VERA_BOARD_IS_PBI`
(vedere sezione 2). In PBI mode tutti e tre i segnali (EXTSEL\_N, DEV\_SEL\_N, MPD) sono
attivi; in CCTL mode solo DEV\_SEL\_N è usato. Il RAMbo 256 KB è abilitato a runtime
tramite `RAMBO_EN` (GPIO 3, vedere sezione 2).

| Signal | GPIO | QFN56 pin | IO MUX | Direction | Active | Level shift | Description |
|---|---|---|---|---|---|---|---|
| PHI2 | 1 | 6 | GPIO1 | Input | HIGH | via U6 | Clock fase 2 CPU 6502 — 1.79 MHz |
| R/W\_ | 2 | 7 | GPIO2 | Input | HIGH=read | via U6 | Read / Not-Write |
| RAMBO\_EN | 3 | 8 | GPIO3 | Input | HIGH | **direct** | RAMbo 256 KB enable — pull-up 10 kΩ→VCC = presente; pull-down 10 kΩ→GND = assente. Letto in `setup()`. |
| EXTSEL\_N | 41 | 47 | MTDI | **Output** | LOW | via U16 | Disabilita MMU/Freddie per $D1xx, $D6xx e finestra RAMbo $4000–$7FFF. **Solo PBI mode** per VERA; RAMbo in entrambe le modalità. Ex-JTAG, reclaimed. |
| DEV\_SEL\_N | 40 | 45 | MTDO | **Output** | LOW | **direct** | VERA chip select (3.3 V); PBI: VERA regs $D1xx; CCTL: range $D5xx. Ex-JTAG, reclaimed. |
| MPD | 42 | 48 | MTMS | **Output** | LOW | via U16 | Math Pack Disable — disabilita ROM Atari $D800–$DFFF — **solo PBI mode**. Ex-JTAG, reclaimed. |
| ARESET | 37 | 42 | GPIO37 | **Output** | LOW | via U16 | Atari System Reset — pilota /RESET bus Atari (open-drain + pull-up consigliati). |
| CRESET | 38 | 43 | GPIO38 | **Output** | LOW | **direct** | VERA FPGA Reset — 3.3 V, diretto al chip FPGA. |
| CDONE | 39 | 44 | MTCK | Input | HIGH | **direct** | VERA FPGA configured status — HIGH = configurazione completata. Ex-JTAG, reclaimed. |

### Dedicated GPIO — canali usati

| Pool | Ch | GPIO | QFN56 pin | Segnale |
|------|----|------|-----------|---------|
| Input  | 0 | 1  | 6  | PHI2      |
| Output | 0 | 40 | 45 | DEV\_SEL\_N |
| Output | 1 | 41 | 47 | EXTSEL\_N   |
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

## 5. Crystal Oscillator (40 MHz)

The ESP32-S3FN8 requires an external **40 MHz crystal** to generate the
internal PLL reference that drives the CPU at 240 MHz, the Flash interface,
and the UART baud-rate dividers. Without it the chip does not boot.
The pins are **dedicated analog** — not GPIO, not software-configurable.

| QFN56 pin | Signal  | Notes                                          |
|-----------|---------|------------------------------------------------|
| 53        | XTAL\_N | Crystal − terminal (oscillator amplifier output) |
| 54        | XTAL\_P | Crystal + terminal (oscillator amplifier input)  |

### Schematic

```
           C1 (10 pF)
XTAL_P (54) ──┤├── GND
      │
    [X1  40 MHz]   ← Rs 0–33 Ω optional, in series with crystal
      │
XTAL_N (53) ──┤├── GND
           C2 (10 pF)
```

| Component | Value | Notes |
|-----------|-------|-------|
| X1 — crystal | 40 MHz, SMD 3225 or 2016 | ±10 ppm or better; note C\_L in datasheet |
| C1, C2 — load caps | 10 pF 0402 ±5 % | C\_ext = 2 × C\_L − C\_stray ; C\_stray ≈ 3–5 pF |
| Rs — series resistor | 0–33 Ω (optional) | Limits oscillator drive level; omit if crystal oscillates cleanly |

**PCB layout:**
- Keep XTAL\_P / XTAL\_N traces as short as possible (< 5 mm).
- Place C1 and C2 **at the chip pins**, not near the crystal body.
- Do not route other signals under or parallel to crystal traces.
- Solid ground plane under the entire crystal area.
- Keep away from PHI2 (1.79 MHz), the 240 MHz CPU clock distribution, and D0–D7.

---

## 6. Level Shifting (74LVC4245APW_118 + SN74HCT04DR)

I tre TXS0108E (auto-sensing, 8 canali ciascuno) sono stati sostituiti da **cinque
74LVC4245APW,118** (NXP, dual-supply octal bus transceiver, TSSOP-24, LCSC C6091) più
due unità di un **SN74HCT04DR** (hex inverter) usate solo per generare la direzione
dinamica del bus dati. A differenza del TXS0108E, il 74LVC4245 **non ha auto-sensing**:
richiede che DIR e ~OE\_ siano pilotati esplicitamente.

**Pinout 74LVC4245APW,118 (TSSOP-24):**

| Pin | Nome | Funzione |
|---|---|---|
| 1 | VCCA | Alimentazione lato A — 3.3 V (ESP32-S3) |
| 2 | DIR | Direzione: A→B oppure B→A a seconda del verso cablato |
| 3–10 | A0–A7 | Canali lato A (3.3 V) |
| 11, 12, 13 | GND | Massa |
| 14–21 | B7–B0 | Canali lato B (5 V, ordine invertito rispetto ad A0–A7) |
| 22 | ~OE\_ | Output enable, attivo basso |
| 23, 24 | VCCB | Alimentazione lato B — 5 V (Atari) |

100 nF ceramico su VCCA→GND e VCCB→GND per ciascun chip. Sui bus a **direzione fissa**
(indirizzi, metà dei segnali di controllo) DIR e ~OE\_ sono cablati staticamente al verso
corretto; sul bus dati **bidirezionale** (U19) DIR è invece pilotato dinamicamente.

### U4 — Address bus A0–A7 (fisso, 6502 → MCU)

| Canale | Lato A (3.3 V, ESP32-S3) | Lato B (5 V, Atari) |
|---|---|---|
| A0/B0 | GPIO 12 (pin 17) — A0 | Atari A0 |
| A1/B1 | GPIO 13 (pin 18) — A1 | Atari A1 |
| A2/B2 | GPIO 14 (pin 19) — A2 | Atari A2 |
| A3/B3 | GPIO 15 (pin 21) — A3 | Atari A3 |
| A4/B4 | GPIO 16 (pin 22) — A4 | Atari A4 |
| A5/B5 | GPIO 17 (pin 23) — A5 | Atari A5 |
| A6/B6 | GPIO 18 (pin 24) — A6 | Atari A6 |
| A7/B7 | GPIO 19 (pin 25) — A7 | Atari A7 (pin condiviso USB D−) |

### U3 — Address bus A8–A15 (fisso, 6502 → MCU)

> **Nota PCB:** tutte le tracce A12–A15 sono portate alla lunghezza della
> traccia più lunga (*length matching* con serpentine) per eliminare lo skew.
> Un solo package copre l'intero bus alto, a differenza della vecchia coppia
> di TXS0108E U3/U4.

| Canale | Lato A (3.3 V, ESP32-S3) | Lato B (5 V, Atari) |
|---|---|---|
| A0/B0 | GPIO 20 (pin 26) — A8  | Atari A8 (pin condiviso USB D+) |
| A1/B1 | GPIO 21 (pin 27) — A9  | Atari A9  |
| A2/B2 | GPIO 35 (pin 40) — A10 | Atari A10 |
| A3/B3 | GPIO 36 (pin 41) — A11 | Atari A11 |
| A4/B4 | GPIO 33 (pin 38) — A12 | Atari A12 |
| A5/B5 | GPIO 34 (pin 39) — A13 | Atari A13 |
| A6/B6 | GPIO 45 (pin 51) — A14 | Atari A14 |
| A7/B7 | GPIO 46 (pin 52) — A15 | Atari A15 |

### U6 — Control signals, 6502 → MCU (fisso, input)

| Canale | Lato A (3.3 V, ESP32-S3) | Lato B (5 V, Atari) | Note |
|---|---|---|---|
| — | GPIO 1 (pin 6) — PHI2 | Atari PHI2 | Clock fase 2, 1.79 MHz |
| — | GPIO 2 (pin 7) — R/W\_ | Atari R/W\_ | Read / Not-Write |
| — | — | D1XX\_N, S4\_N, S5\_N, CCTL\_N, REFRESH | Canali liberi, portati a test point — **non collegati a un GPIO in questo progetto** |

### U16 — Control signals, MCU → 6502 (fisso, output)

| Canale | Lato A (3.3 V, ESP32-S3) | Lato B (5 V, Atari) | Note |
|---|---|---|---|
| — | GPIO 41 (pin 47) — EXTSEL\_N | Atari EXTSEL | Output, active LOW |
| — | GPIO 42 (pin 48) — MPD | Atari ECI MPD | Output, active LOW |
| — | GPIO 37 (pin 42) — ARESET | Atari /RESET | Output, active LOW |
| — | — | RD4, RD5, HALT | Canali liberi, portati a test point — **non collegati a un GPIO in questo progetto** |
| — | `~mVIRQ` (FPGA, pin 18 lato 3,3 V) | Atari IRQ (`ECI1` pin B) | Uscita FPGA **push-pull** direttamente sul bus: da sostituire con uno stadio open-drain (74LVC1G07), vedere `VERA-ATARI-HW-REQUIREMENTS.md` §2.3 |

### U19 — Data bus D0–D7 (bidirezionale, direzione dinamica)

DIR è pilotato da due invertitori **SN74HCT04DR U23** a partire da R/W\_ e PHI2 (stessa
logica di un transceiver dati 6502 classico: direzione verso l'Atari durante i cicli di
lettura del 6502, verso l'ESP32 durante i cicli di scrittura), non più dall'auto-sensing
del TXS0108E.

> **`~OE_` (pin 22): da modificare.** Oggi è `NOT(PHI2)` (via `U23`), quindi `U19` pilota il bus Atari in
> ogni lettura, anche di memorie non della scheda. Deve diventare
> `~OE_ = NAND(PHI2, NAND(R/W, AND3(DEV_SEL_N, EXTSEL_N, MPD)))`: attivo in scrittura e nelle
> sole letture a cui la scheda risponde (non va collegato a `CDONE`). Dettaglio, parti e tempi in
> `VERA-ATARI-HW-REQUIREMENTS.md` §2.1. Nella netlist `VCCA` (pin 1) di `U19` è a 5 V: il lato "A" di
> questa tabella (3,3 V) e il lato "B" (5 V) vanno **verificati** sullo schema.

| Canale | Lato A (3.3 V, ESP32-S3) | Lato B (5 V, Atari) |
|---|---|---|
| A0/B0 | GPIO 4  (pin  9) — D0 | Atari D0 |
| A1/B1 | GPIO 5  (pin 10) — D1 | Atari D1 |
| A2/B2 | GPIO 6  (pin 11) — D2 | Atari D2 |
| A3/B3 | GPIO 7  (pin 12) — D3 | Atari D3 |
| A4/B4 | GPIO 8  (pin 13) — D4 | Atari D4 |
| A5/B5 | GPIO 9  (pin 14) — D5 | Atari D5 |
| A6/B6 | GPIO 10 (pin 15) — D6 | Atari D6 |
| A7/B7 | GPIO 11 (pin 16) — D7 | Atari D7 |

**Connessioni dirette (senza level shifter):**

| Signal | GPIO | QFN56 pin | Collegato a |
|---|---|---|---|
| DEV\_SEL\_N | 40 | 45 | VERA FPGA pin /CS (3.3 V) |
| CRESET | 38 | 43 | VERA FPGA pin /RESET (3.3 V) |
| CDONE | 39 | 44 | VERA FPGA pin CDONE (3.3 V) |

---

## 7. GPIO disponibili (ESP32-S3FN8 QFN56)

Pin non assegnati al progetto, esclusi strapping e flash in-package.

| GPIO | QFN56 pin | IO MUX | Note |
|---|---|---|---|
| 22–25 | — | — | Non esistono su ESP32-S3 |

> Tutti i GPIO non riservati sono ora assegnati: GPIO 3 = RAMBO\_EN; GPIO 33, 34, 45, 46 = A12–A15.

**Pin esclusi (non utilizzabili):**

| GPIO / Funzione | QFN56 pin | Motivo |
|---|---|---|
| GPIO 26–32 | 28, 30–35 | Flash in-package — mai connettere esternamente |
| XTAL\_N / XTAL\_P | 53, 54 | Dedicated analog — quarzo 40 MHz. Vedere § 5. |
| VDDA1 / VDDA2 | 55, 56 | Alimentazione analogica — non GPIO, non connettere a segnali digitali |

---

## 8. QFN56 — Pinout completo

Riferimento rapido ESP32-S3FN8 QFN56 (56 pin segnale + pad GND centrale).

| Pin | GPIO / Funzione | Uso in questo progetto |
|-----|-----------------|------------------------|
| 1   | GND | GND |
| 2   | 3V3 | 3.3 V |
| 3   | EN (CHIP\_EN) | Pull-up 10 kΩ a 3.3 V |
| 4   | — | (riservato / NC) |
| 5   | GPIO 0 | BOOT0 (pulsante BOOT / auto-reset, pull-up `R33`) — strapping |
| 6   | GPIO 1 | PHI2 input (via U6) |
| 7   | GPIO 2 | R/W\_ input (via U6) |
| 8   | GPIO 3 | RAMBO\_EN — pull-up 10 kΩ → VCC = RAMbo presente; pull-down 10 kΩ → GND = assente |
| 9   | GPIO 4 | D0 (via U19) |
| 10  | GPIO 5 | D1 (via U19) |
| 11  | GPIO 6 | D2 (via U19) |
| 12  | GPIO 7 | D3 (via U19) |
| 13  | GPIO 8 | D4 (via U19) |
| 14  | GPIO 9 | D5 (via U19) |
| 15  | GPIO 10 | D6 (via U19) |
| 16  | GPIO 11 | D7 (via U19) |
| 17  | GPIO 12 | A0 (via U4) |
| 18  | GPIO 13 | A1 (via U4) |
| 19  | GPIO 14 | A2 (via U4) |
| 20  | VDD3P3\_RTC | Alimentazione RTC — **non GPIO** |
| 21  | GPIO 15 / XTAL\_32K\_P | A3 (via U4) — nessun quarzo 32 kHz |
| 22  | GPIO 16 / XTAL\_32K\_N | A4 (via U4) — nessun quarzo 32 kHz |
| 23  | GPIO 17 | A5 (via U4) |
| 24  | GPIO 18 | A6 (via U4) |
| 25  | GPIO 19 / USB\_D− | A7 (via U4) — USB disabilitato |
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
| 38  | GPIO 33 | A12 (via U3) |
| 39  | GPIO 34 | A13 (via U3) |
| 40  | GPIO 35 | A10 (via U3) |
| 41  | GPIO 36 | A11 (via U3) |
| 42  | GPIO 37 | ARESET output (via U16) |
| 43  | GPIO 38 | CRESET output (diretto VERA) |
| 44  | GPIO 39 / MTCK | CDONE input (diretto VERA) — ex-JTAG |
| 45  | GPIO 40 / MTDO | DEV\_SEL\_N output (diretto VERA) — ex-JTAG |
| 46  | VDD3P3\_CPU | Alimentazione CPU — **non GPIO** |
| 47  | GPIO 41 / MTDI | EXTSEL\_N output (via U16) — ex-JTAG |
| 48  | GPIO 42 / MTMS | MPD output (via U16) — ex-JTAG |
| 49  | GPIO 43 / U0TXD | UART0 TX — debug seriale |
| 50  | GPIO 44 / U0RXD | UART0 RX — debug seriale |
| 51  | GPIO 45 | A14 (via U3) — strapping VDD\_SPI; pull-down → LOW al boot (FN8 safe) |
| 52  | GPIO 46 | A15 (via U3) — strapping ROM msgs; pull-down → LOW al boot |
| 53  | XTAL\_N | Quarzo principale 40 MHz — dedicated analog |
| 54  | XTAL\_P | Quarzo principale 40 MHz — dedicated analog |
| 55  | VDDA1   | Alimentazione analogica — non GPIO |
| 56  | VDDA2   | Alimentazione analogica — non GPIO |
| EP  | GND (pad centrale) | GND |
