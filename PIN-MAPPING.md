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
| **GPIO 0** | 5 | BOOT0 | Strapping pin (boot mode) — **non è NC**: rete `BOOT0` con `R33` 4,7 kΩ → 3V3, pulsante `SW3`, `C61` 100 nF e `R54` 220 Ω verso `Q4` (auto-reset del programmatore). LOW al reset = modalità download (richiede anche GPIO 46 LOW). CHIP_PU ha `R31` 4,7 kΩ + `C25` **1 µF** (4,7 ms) contro 0,47 ms di BOOT0: GPIO 0 è già alto quando il reset viene rilasciato. |
| **GPIO 3** | 8 | — | Strapping pin (JTAG source, no internal pull) — **non usato dal firmware**. Pull-up **`R71` 10 kΩ → 3V3**, come richiesto dal datasheet (§3.4: senza pull interne, non può restare flottante). HIGH = JTAG sull'USB Serial/JTAG anche se un giorno si programmasse `EFUSE_STRAP_JTAG_SEL`: GPIO 39–42 non diventano mai pin JTAG. Con gli eFuse di fabbrica il valore è comunque ignorato. |
| **GPIO 26–32** | 28, 30–35 | — | SPI0/1 pins of the in-package memory (datasheet Table 2-14). On the FN8 the Quad SPI flash uses GPIO 27–32 (pins 30–35): **never connect externally**. GPIO 26 (pin 28, SPICS1) is the in-package PSRAM chip select, absent on the FN8; Espressif still advises against other uses. |
| **GPIO 45** | 51 | A14 | Strapping pin (VDD_SPI select) — pull-down interno debole (45 kΩ). Connesso ad A14 via `U3` (74LVC4245). Il 6502 **pilota il bus indirizzi anche in reset**, quindi il livello non può dipendere dall'Atari: `~OE` di `U3` (e di `U4`) è la rete **`~ABUS_OE`**, pilotata da **GPIO 47** e tenuta alta da `R7`/`R34` (10 kΩ → 3V3) mentre l'ESP32 è in reset. Con `U3` spento il pull-down interno dà **LOW → VDD_SPI = 3,3 V da VDD3P3_RTC** (Table 3-4), il valore richiesto dalla flash in-package della FN8. Vale a ogni reset del chip (accensione, CHIP_PU, programmatore). Il firmware abilita `U3`/`U4` in `setup()`, molto dopo i 3 ms di hold. Gli eFuse `VDD_SPI_FORCE`/`TIEH` non servono. |
| **GPIO 46** | 52 | A15 | Strapping pin (ROM messages on UART0, download mode) — pull-down interno debole (45 kΩ). Connesso ad A15 via `U3`, stesso gating di GPIO 45 → **LOW**: messaggi ROM disabilitati e, con GPIO 0 LOW, download boot valido (GPIO 0 LOW + GPIO 46 HIGH è una combinazione non valida): la programmazione funziona anche con l'Atari acceso. |

> **JTAG pins reclaimed (GPIO 39–42):** il firmware chiama `gpio_reset_pin()` su GPIO 39, 40,
> 41, 42 per de-registrarli dal controller JTAG e utilizzarli come GPIO normali.

> **GPIO 19–20 (pin 25–26, USB D−/D+):** USB CDC disabilitato nel firmware
> (`ARDUINO_USB_CDC_ON_BOOT=0`). Questi pin sono usati come A7 e A8 sul bus indirizzi.

> **GPIO 15–16 (pin 21–22, XTAL\_32K\_P/N):** nessun quarzo da 32 kHz presente.
> Utilizzati come normali GPIO (A3 e A4 sul bus indirizzi).

---

## 1. Data Bus D0–D7  (bidirectional, Bank 0 bits 4–11)

Tutti via level shifter **74LVC4245APW_118 U19** (A side = Atari 5 V, B side = ESP32 3,3 V) —
unico canale realmente **bidirezionale** del progetto: la direzione (pin DIR) è commutata
dinamicamente da un invertitore **SN74HCT04DR U23** pilotato da R/W\_, invece dell'auto-sensing
analogico del vecchio TXS0108E; `~OE` viene dal gating di `U24`/`U25`. Vedere § 6.

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

### A8–A15 (Bank 0/1, GPIO 20–21, 33–36, 45–46, via 74LVC4245APW_118 U3)

> **Hardware:** un solo chip **U3** (8 canali) copre l'intero bus indirizzi alto A8–A15,
> unificando quello che nella revisione TXS0108E era diviso su due package separati.
> A10–A13 sono su GPIO 33–36 (pin 38–41) in ordine naturale (A10 = pin 38 … A13 = pin 41),
> scelto per semplificare il routing; GPIO 45/46 (pin 51–52) restano sul lato opposto del QFN56.
> **Routing PCB:** applicare *length matching* — tutte le tracce di A12–A15 portate alla
> lunghezza della traccia più lunga (A14 o A15) tramite serpentine, per eliminare lo skew
> di propagazione tra i quattro bit.

| Atari signal | GPIO | QFN56 pin | IO MUX | Bank bit | Notes |
|---|---|---|---|---|---|
| A8  | 20 | 26 | USB\_D+ | Bank 0 bit 20 | USB disabilitato; pin condiviso con USB D+ |
| A9  | 21 | 27 | GPIO21  | Bank 0 bit 21 | |
| A10 | 33 | 38 | GPIO33  | Bank 1 bit 1  | Ex-spare CONN / libre |
| A11 | 34 | 39 | GPIO34  | Bank 1 bit 2  | Ex-spare CONN / libre |
| A12 | 35 | 40 | GPIO35  | Bank 1 bit 3  | |
| A13 | 36 | 41 | GPIO36  | Bank 1 bit 4  | |
| A14 | 45 | 51 | GPIO45  | Bank 1 bit 13 | Strapping pin (VDD_SPI); `U3` spento durante il reset (`~ABUS_OE`) → pull-down → LOW |
| A15 | 46 | 52 | GPIO46  | Bank 1 bit 14 | Strapping pin (ROM msgs / download); `U3` spento durante il reset → pull-down → LOW |

### Firmware: decode indirizzo completo (16 bit)

Il loop del bus usa la pagina A8–A15 come indice di tabelle precalcolate
(`firmware/MCU/include/bus_decode.h`) e A0–A7 come offset nella pagina. PHI2, R/W e
A10–A15 sono collegati anche agli otto canali di ingresso **Dedicated GPIO**, letti in un
ciclo; A0–A9 vengono dall'unica lettura APB di `GPIO.in`.

```c
/* din = ee.get_gpio_in: bit 0 = PHI2, bit 1 = R/W, bit 2..7 = A10..A15      */
/* lo  = GPIO.in:        bit 12..21 = A0..A9                                 */
uint32_t page = ((lo >> 20) & 0x03) | (din & 0xFC);   /* A15..A8 */
uint32_t off8 = (lo >> 12) & 0xFF;                    /* A7..A0  */
```

La modalità operativa è selezionata a compile time tramite `VERA_BOARD_IS_PBI` in `main.cpp`:

| `VERA_BOARD_IS_PBI` | Modalità | Range attivi | Latch selezione |
|---------------------|----------|--------------|-----------------|
| `1` (default) | **PBI**  | $D100–$D1FF, $D600–$D7FF, $D800–$DFFF | write `0x80` → $D1FF |
| `0`           | **CCTL** | $D500–$D5FF                           | write `0x80` → $D5FF |

```c
/* Pagine gestite — PBI mode (VERA_BOARD_IS_PBI = 1), tabelle in bus_decode.h: */
/* VERA regs  $D100-$D11F   32 B  →  EXTSEL_N + DEV_SEL_N (sempre)         */
/* PBI latch  $D1FF              →  write 0x80 = seleziona scheda           */
/* RAM        $D600-$D7FF  512 B  →  ram_pbi[addr & 0x1FF]  (ESP32 drive)   */
/* ROM        $D800-$DFFF 2048 B  →  pbi_driver[addr & 0x7FF] (ESP32 drive) */

/* Pagine gestite — CCTL mode (VERA_BOARD_IS_PBI = 0): */
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

> **Perché non i chip-select hardware:** sull'ESP32-S3FN8 resta libero un solo GPIO
> (GPIO 48, § 7; GPIO 47 è `ABUS_OE_N`), non abbastanza per i quattro chip-select hardware disponibili sui
> connettori Atari (`/D1XX`, `/S4`, `/S5`, `/CCTL`). Inoltre l'emulazione RAMbo 256 KB richiede comunque di leggere l'intero
> bus indirizzi A0–A15 (per decodificare la finestra $4000–$7FFF) e di intercettare le
> scritture su PORTB ($D301), quindi anche con GPIO disponibili i chip-select hardware
> non risparmierebbero pin sul bus indirizzi: la decodifica software è l'unica strada.

La modalità operativa (PBI o CCTL) è selezionata a compile time con `VERA_BOARD_IS_PBI`
(vedere sezione 2). In PBI mode tutti e tre i segnali (EXTSEL\_N, DEV\_SEL\_N, MPD) sono
attivi; in CCTL mode solo DEV\_SEL\_N è usato. La RAMbo 320K (256 KB a `$4000–$7FFF`)
è sempre attiva, in entrambe le modalità.

| Signal | GPIO | QFN56 pin | IO MUX | Direction | Active | Level shift | Description |
|---|---|---|---|---|---|---|---|
| PHI2 | 1 | 6 | GPIO1 | Input | HIGH | via U6 | Clock fase 2 CPU 6502 — 1.79 MHz |
| R/W\_ | 2 | 7 | GPIO2 | Input | HIGH=read | via U6 | Read / Not-Write |
| EXTSEL\_N | 41 | 47 | MTDI | **Output** | LOW | via U16 | Disabilita MMU/Freddie per $D1xx, $D6xx e finestra RAMbo $4000–$7FFF. **Solo PBI mode** per VERA; RAMbo in entrambe le modalità. Ex-JTAG, reclaimed. |
| DEV\_SEL\_N | 40 | 45 | MTDO | **Output** | LOW | **direct** | VERA chip select (3.3 V); PBI: VERA regs $D1xx; CCTL: range $D5xx. Ex-JTAG, reclaimed. |
| MPD | 42 | 48 | MTMS | **Output** | LOW | via U16 | Math Pack Disable — disabilita ROM Atari $D800–$DFFF — **solo PBI mode**. Ex-JTAG, reclaimed. |
| ARESET | 37 | 42 | GPIO37 | **Output** | LOW | via U16 | Atari System Reset — pilota /RESET bus Atari (open-drain + pull-up consigliati). |
| CRESET | 38 | 43 | GPIO38 | **Output** | LOW | **direct** | VERA FPGA Reset — 3.3 V, diretto al chip FPGA. |
| CDONE | 39 | 44 | MTCK | Input | HIGH | **direct** | VERA FPGA configured status — HIGH = configurazione completata. Ex-JTAG, reclaimed. |
| ABUS\_OE\_N | 47 | 37 | SPICLK\_P | **Output** | LOW | **direct** | Rete `~ABUS_OE`: `~OE` di `U3` (A8–A15) e `U4` (A0–A7), pull-up `R7`/`R34` 10 kΩ → 3V3. Alto (traslatori spenti) durante il reset dell'ESP32, così GPIO 45/46 leggono LOW; il firmware lo porta basso in `setup()`. Dominio VDD\_SPI = 3,3 V. |

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

> **Programmazione e USB solo a scheda scollegata dall'Atari.** La rete 5 V della scheda è unita al +5 V dell'Atari (`CART1` pin 13, `ECI1` pin 7) senza diodo: con il cavo USB (`J4`) o il jack (`J5`) collegati mentre la scheda è inserita, la scheda alimenterebbe l'Atari (spento) o lavorerebbe in parallelo al suo alimentatore (acceso). Vale anche per il monitor seriale: con la scheda nell'Atari usare un adattatore senza VBUS. Vedere `VERA-ATARI-HW-REQUIREMENTS.md` §2.11.

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

## 6. Level Shifting (74LVC4245APW_118, SN74HCT04DR, SN74LVC00A)

I tre TXS0108E (auto-sensing, 8 canali ciascuno) sono stati sostituiti da **cinque
74LVC4245APW,118** (NXP, dual-supply octal bus transceiver, TSSOP-24, LCSC C6091) più
un'unità di un **SN74HCT04DR** (hex inverter, `U23`) per la direzione dinamica del bus dati e
due **SN74LVC00A** (`U24`, `U25`) per l'abilitazione (`~OE`) di U19. A differenza del TXS0108E, il 74LVC4245 **non ha auto-sensing**:
richiede che DIR e ~OE\_ siano pilotati esplicitamente.

**Pinout 74LVC4245APW,118 (TSSOP-24):**

| Pin | Nome | Funzione |
|---|---|---|
| 1 | VCCA | Alimentazione lato A — 5 V (Atari); `DIR` e `~OE_` sono riferiti a VCCA (soglia TTL) |
| 2 | DIR | Direzione: A→B oppure B→A a seconda del verso cablato |
| 3–10 | A0–A7 | Canali lato A (5 V, Atari) |
| 11, 12, 13 | GND | Massa |
| 14–21 | B7–B0 | Canali lato B (3,3 V, ESP32/FPGA; ordine invertito rispetto ad A0–A7) |
| 22 | ~OE\_ | Output enable, attivo basso |
| 23, 24 | VCCB | Alimentazione lato B — 3,3 V (entrambi i pin a 3V3) |

100 nF ceramico su VCCA→GND e VCCB→GND per ciascun chip. Sui bus a **direzione fissa**
(indirizzi, metà dei segnali di controllo) DIR è cablato staticamente al verso corretto; sul bus
dati **bidirezionale** (U19) DIR è invece pilotato dinamicamente. `~OE_` è fisso a GND (`R64`, `R66`
da 0 Ω) solo su U6 e U16: su U3 e U4 è la rete `~ABUS_OE` (GPIO 47, § 3), su U19 il gating di
`U24`/`U25`.

### U4 — Address bus A0–A7 (fisso, 6502 → MCU)

`~OE_` (pin 22) = `~ABUS_OE` (GPIO 47, pull-up `R34` 10 kΩ → 3V3), come U3: spento durante il reset
dell'ESP32. Oltre agli strapping di U3, evita che U4 piloti GPIO 12–19 durante i loro glitch di uscita
all'accensione (datasheet Table 2-2).

| Canale | Lato B (3,3 V, ESP32-S3) | Lato A (5 V, Atari) |
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

`~OE_` (pin 22) = `~ABUS_OE` (GPIO 47, pull-up `R7` 10 kΩ → 3V3). Durante il reset dell'ESP32
GPIO 47 è in alta impedenza (nessuna pull interna, nessun glitch), `R7` spegne U3 e GPIO 45/46
(A14/A15) leggono LOW dai pull-down interni: VDD\_SPI = 3,3 V e download boot valido a ogni reset
(§ Critical Boot-Time Warnings). Il firmware abilita U3/U4 in `setup()`.

> **Nota PCB:** tutte le tracce A12–A15 sono portate alla lunghezza della
> traccia più lunga (*length matching* con serpentine) per eliminare lo skew.
> Un solo package copre l'intero bus alto, a differenza della vecchia coppia
> di TXS0108E U3/U4.

| Canale | Lato B (3,3 V, ESP32-S3) | Lato A (5 V, Atari) |
|---|---|---|
| A0/B0 | GPIO 20 (pin 26) — A8  | Atari A8 (pin condiviso USB D+) |
| A1/B1 | GPIO 21 (pin 27) — A9  | Atari A9  |
| A2/B2 | GPIO 33 (pin 38) — A10 | Atari A10 |
| A3/B3 | GPIO 34 (pin 39) — A11 | Atari A11 |
| A4/B4 | GPIO 35 (pin 40) — A12 | Atari A12 |
| A5/B5 | GPIO 36 (pin 41) — A13 | Atari A13 |
| A6/B6 | GPIO 45 (pin 51) — A14 | Atari A14 |
| A7/B7 | GPIO 46 (pin 52) — A15 | Atari A15 |

### U6 — Control signals, 6502 → MCU (fisso, input)

| Canale | Lato B (3,3 V, ESP32-S3) | Lato A (5 V, Atari) | Note |
|---|---|---|---|
| — | GPIO 1 (pin 6) — PHI2 | Atari PHI2 | Clock fase 2, 1.79 MHz |
| — | GPIO 2 (pin 7) — R/W\_ | Atari R/W\_ | Read / Not-Write |
| — | — | D1XX\_N, S4\_N, S5\_N, CCTL\_N, REFRESH | Canali liberi, portati a test point — **non collegati a un GPIO in questo progetto** |

### U16 — Control signals, MCU → 6502 (fisso, output)

| Canale | Lato B (3,3 V, ESP32-S3) | Lato A (5 V, Atari) | Note |
|---|---|---|---|
| — | GPIO 41 (pin 47) — EXTSEL\_N | Atari EXTSEL | Output, active LOW |
| — | GPIO 42 (pin 48) — MPD | Atari ECI MPD | Output, active LOW |
| — | GPIO 37 (pin 42) — ARESET | Atari /RESET | Output, active LOW |
| — | GND con una 0 Ω ciascuno (pin 19 `R73`, 20 `R76`, 21 `R77`, 14 `R72`, 15 `R75`) | non collegati (pin 5, 4, 3, 9, 10) | Canali di riserva. RD4, RD5 e HALT non passano da `U16`: hanno test point (`RD4`, `RD5`, `~HALT1`) direttamente sulle reti lato Atari (`VERA-ATARI-HW-REQUIREMENTS.md` §2.10) |
| — | `~mVIRQ` (FPGA `U2` pin 32) | Atari IRQ (`ECI1` pin B) | **Non passa da `U16`**: buffer open-drain `U26` SN74LVC1G07DBVR (A pin 2 ← `~mVIRQ`, Y pin 4 → `~IRQ`), 3V3 con `C69` 100 nF. Vedere `VERA-ATARI-HW-REQUIREMENTS.md` §2.3 |

### U19 — Data bus D0–D7 (bidirezionale, direzione dinamica)

DIR è pilotato da un invertitore **SN74HCT04DR U23** (pin 1→2) a partire da R/W\_ (stessa
logica di un transceiver dati 6502 classico: direzione verso l'Atari durante i cicli di
lettura del 6502, verso l'ESP32 durante i cicli di scrittura), non più dall'auto-sensing
del TXS0108E.

> **`~OE_` (pin 22) = `NAND(PHI2, NAND4(R/W, DEV_SEL_N, EXTSEL_N, MPD))`**, realizzato con due
> SN74LVC00A (`U24`, `U25`): `U19` è acceso in scrittura e nelle sole letture a cui la scheda risponde,
> spento in φ1 e nelle letture di RAM/ROM interne (prima era `NOT(PHI2)` da `U23` e faceva contesa in
> ogni lettura). Dettaglio, piedinatura e tempi in `VERA-ATARI-HW-REQUIREMENTS.md` §2.1.

| Canale | Lato B (3,3 V, ESP32-S3) | Lato A (5 V, Atari) |
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
| 48 | 36 | SPICLK\_N | Libero. Dominio VDD\_SPI (3.3 V sulla FN8, datasheet Table 1-1); usato dalla memoria solo in Octal SPI |
| 22–25 | — | — | Non esistono su ESP32-S3 |

> Resta libero solo GPIO 48 (pin 36); GPIO 47 (pin 37) è `ABUS_OE_N`, tutti gli altri GPIO non
> riservati sono assegnati: GPIO 33–36 = A10–A13, GPIO 45, 46 = A14–A15. GPIO 3 non è usato ma è un pin di
> strapping: lo fissa `R71` (10 kΩ → 3V3).

**Pin esclusi (non utilizzabili):**

| GPIO / Funzione | QFN56 pin | Motivo |
|---|---|---|
| GPIO 26–32 | 28, 30–35 | Pin SPI0/1 della memoria in-package: GPIO 27–32 = flash della FN8, mai connettere esternamente; GPIO 26 = CS# PSRAM (assente sulla FN8), sconsigliato da Espressif |
| XTAL\_N / XTAL\_P | 53, 54 | Dedicated analog — quarzo 40 MHz. Vedere § 5. |
| VDDA1 / VDDA2 | 55, 56 | Alimentazione analogica — non GPIO, non connettere a segnali digitali |

---

## 8. QFN56 — Pinout completo

Riferimento rapido ESP32-S3FN8 QFN56 (56 pin + pad centrale 57 = GND). Nomi e numeri da
*ESP32-S3 Series Datasheet* v2.2, Table 2-1 "Pin Overview" (pag. 17); funzioni dei pin della
memoria da Table 2-14 "Pin Mapping Between Chip and Flash or PSRAM" (pag. 31).

| Pin | GPIO / Funzione | Uso in questo progetto |
|-----|-----------------|------------------------|
| 1   | LNA\_IN | Ingresso RF (Wi-Fi/Bluetooth) — non usato dal firmware |
| 2   | VDD3P3 | Alimentazione analogica 3.3 V |
| 3   | VDD3P3 | Alimentazione analogica 3.3 V |
| 4   | CHIP\_PU (EN) | `R31` 4,7 kΩ a 3V3 + `C25` 1 µF a GND (4,7 ms), reset `SW1`, auto-reset `Q3` |
| 5   | GPIO 0 | BOOT0 (pulsante BOOT / auto-reset, pull-up `R33`) — strapping |
| 6   | GPIO 1 | PHI2 input (via U6) |
| 7   | GPIO 2 | R/W\_ input (via U6) |
| 8   | GPIO 3 | Non usato — strapping JTAG source, pull-up `R71` 10 kΩ → 3V3 |
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
| 28  | GPIO 26 (SPICS1) | CS# della PSRAM in-package (assente sulla FN8) — non usato |
| 29  | VDD\_SPI | Alimentazione flash — **non GPIO** |
| 30  | GPIO 27 (SPIHD)  | **Flash in-package (HOLD#) — NC esterno** |
| 31  | GPIO 28 (SPIWP)  | **Flash in-package (WP#) — NC esterno** |
| 32  | GPIO 29 (SPICS0) | **Flash in-package (CS#) — NC esterno** |
| 33  | GPIO 30 (SPICLK) | **Flash in-package (CLK) — NC esterno** |
| 34  | GPIO 31 (SPIQ)   | **Flash in-package (DO) — NC esterno** |
| 35  | GPIO 32 (SPID)   | **Flash in-package (DI) — NC esterno** |
| 36  | GPIO 48 (SPICLK\_N) | GPIO libero — usato dalla memoria solo in Octal SPI; non usato in questo progetto |
| 37  | GPIO 47 (SPICLK\_P) | ABUS\_OE\_N output → `~OE` di U3/U4 (rete `~ABUS_OE`, pull-up `R7`/`R34`) |
| 38  | GPIO 33 | A10 (via U3) |
| 39  | GPIO 34 | A11 (via U3) |
| 40  | GPIO 35 | A12 (via U3) |
| 41  | GPIO 36 | A13 (via U3) |
| 42  | GPIO 37 | ARESET output (via U16) |
| 43  | GPIO 38 | CRESET output (diretto VERA) |
| 44  | GPIO 39 / MTCK | CDONE input (diretto VERA) — ex-JTAG |
| 45  | GPIO 40 / MTDO | DEV\_SEL\_N output (diretto VERA) — ex-JTAG |
| 46  | VDD3P3\_CPU | Alimentazione CPU — **non GPIO** |
| 47  | GPIO 41 / MTDI | EXTSEL\_N output (via U16) — ex-JTAG |
| 48  | GPIO 42 / MTMS | MPD output (via U16) — ex-JTAG |
| 49  | GPIO 43 / U0TXD | UART0 TX — debug seriale |
| 50  | GPIO 44 / U0RXD | UART0 RX — debug seriale |
| 51  | GPIO 45 | A14 (via U3) — strapping VDD\_SPI; U3 spento durante il reset → LOW (3,3 V) |
| 52  | GPIO 46 | A15 (via U3) — strapping ROM msgs / download; U3 spento durante il reset → LOW |
| 53  | XTAL\_N | Quarzo principale 40 MHz — dedicated analog |
| 54  | XTAL\_P | Quarzo principale 40 MHz — dedicated analog |
| 55  | VDDA1   | Alimentazione analogica — non GPIO |
| 56  | VDDA2   | Alimentazione analogica — non GPIO |
| EP  | GND (pad centrale) | GND |
