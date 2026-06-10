# Vera-Module-RBL-XE — Firmware MCU

## 1. Introduzione

Firmware per il microcontrollore **ESP32-S3FN8** (QFN-56, 8 MB flash integrata,
dual-core Xtensa LX7 @ 240 MHz) montato sul modulo Vera-Module-RBL-XE.

Il modulo si collega al computer **ATARI XE** tramite il connettore **PBI**
(Parallel Bus Interface) oppure tramite il segnale **CCTL** (Cartridge Control).
La modalità operativa è selezionata a **tempo di compilazione** tramite la macro
`VERA_BOARD_IS_PBI` in `main.cpp`: `1` per PBI (default), `0` per CCTL.
L'emulatore RAMbo 256 KB è abilitato separatamente tramite la macro `USE_RAMBO_256K`
e funziona indipendentemente dalla modalità PBI/CCTL.
Il bus indirizzi A0–A15 è decodificato completamente via software in entrambe
le modalità.

---

## 2. Architettura del Firmware

### Hot loop (Core 1, priorità massima FreeRTOS)

Il task `MonitorTask()` gira sul Core 1 con priorità `configMAX_PRIORITIES - 1`.
Ad ogni ciclo di clock del 6502 (PHI2 @ 1,79 MHz, finestra utile ≈ 279 ns a
240 MHz di CPU):

1. Attende il fronte di salita di PHI2 tramite **Dedicated GPIO in**
   (`ee.get_gpio_in`, 1 ciclo CPU anziché ~6 cicli via APB).
2. Legge il bus indirizzi A0–A15 e la linea R/W dai registri `GPIO.in` /
   `GPIO.in1.val` (due letture APB sequenziali).
3. Decodifica l'indirizzo via software (`decode_addr()`) e determina il range
   attivo in base alla modalità selezionata.
4. Afferma in modo atomico i segnali di controllo attivi-LOW tramite
   **Dedicated GPIO out** (`ee.wr_mask_gpio_out`, 1 ciclo CPU — tutti i segnali
   in un'unica istruzione TIE).
5. **Solo PBI:** se l'accesso è in lettura verso la ROM ($D800–$DFFF) o la RAM
   ($D600–$D7FF), guida il bus dati D0–D7 con `bus_drive()` (2 scritture APB).
   In CCTL mode l'ESP32 non tocca mai il bus dati — è la FPGA a rispondere.
6. Attende il fronte di discesa di PHI2, rilascia il bus e de-afferma i
   segnali di controllo con un'altra `ee.wr_mask_gpio_out`.

### Dedicated GPIO (modulo ESP32-S3)

Il modulo Dedicated GPIO dell'ESP32-S3 assegna fino a 8 canali in ingresso e
8 in uscita per core. L'accesso avviene tramite le istruzioni Xtensa TIE:

| Istruzione TIE              | Helper C (IDF ≤ 5.0)                             | Latenza |
|-----------------------------|--------------------------------------------------|---------|
| `ee.get_gpio_in`            | `cpu_ll_read_dedic_gpio_in()`                    | 1 ciclo |
| `ee.wr_mask_gpio_out`       | `cpu_ll_write_dedic_gpio_mask(mask, val)`        | 1 ciclo |

I canali configurati nel firmware:

| Bundle  | Canale | GPIO           | Segnale     | Nota                  |
|---------|--------|----------------|-------------|-----------------------|
| Input   | bit 0  | GPIO 1 (pin 6) | PHI2        | Clock 6502            |
| Output  | bit 0  | GPIO 40 (pin 45)| DEV\_SEL\_N | Chip-select VERA      |
| Output  | bit 1  | GPIO 41 (pin 46)| EXTSEL\_N   | Disabilita MMU ATARI  |
| Output  | bit 2  | GPIO 42 (pin 48)| MPD         | Disabilita Math Pack  |

I pin GPIO 39–42 erano originariamente riservati a JTAG; vengono liberati in
`setup()` tramite `gpio_reset_pin()` prima della creazione del bundle.

### Mappatura del bus (riepilogo)

| Segnale      | GPIO        | QFN-56 pin | Note                              |
|--------------|-------------|------------|-----------------------------------|
| PHI2         | 1           | 6          | Input dedicated                   |
| R/W          | 2           | 7          | Input                             |
| D0–D7        | 4–11        | 9–16       | Bus dati bidirezionale            |
| A0–A9        | 12–21       | 17–27      | GPIO 19=USB D−, 20=USB D+ (USB disabilitato) |
| A10          | 35          | 40         | Address bit 10 (via U3)           |
| A11          | 36          | 41         | Address bit 11 (via U3)           |
| A12          | 33          | 38         | Address bit 12 (via U4)           |
| A13          | 34          | 39         | Address bit 13 (via U4)           |
| A14          | 47          | 53         | Address bit 14 (via U4)           |
| A15          | 48          | 54         | Address bit 15 (via U4)           |
| ARESET       | 37          | 42         | System reset ATARI (output)       |
| CRESET       | 38          | 43         | Chip reset VERA (output)          |
| CDONE        | 39          | 44         | VERA programmata (input)          |
| DEV\_SEL\_N  | 40          | 45         | Chip-select VERA (output, dedicated) |
| EXTSEL\_N    | 41          | 46         | Disabilita MMU (output, dedicated)|
| MPD          | 42          | 48         | Disabilita Math Pack (dedicated)  |
| Debug TX     | 43          | 49         | UART0 TX (Serial)                 |
| Debug RX     | 44          | 50         | UART0 RX (Serial)                 |

`decode_addr()` restituisce un indirizzo **16 bit** completo: nessuna ambiguità di
pagina. I bit A12–A15 sono catturati nella stessa lettura `GPIO.in1.val` già
eseguita per A10–A11 — zero cicli APB aggiuntivi nel hot loop.

Per la mappatura completa GPIO→QFN-56 e lo schema level shifter U4 vedere
`PIN-MAPPING.md` nella root.

---

## 3. Struttura dei File

```
firmware/MCU/
├── platformio.ini               # Configurazione build (target unificato: esp32s3fn8)
├── pre_build_6502.py            # Script pre-build PlatformIO: esegue make in 6502/
├── include/
│   ├── pbi-driver.h             # Strutture dati e costanti del protocollo PBI
│   └── vera_pbi_handler.h       # ROM 6502 compilata come array C (generato da make)
├── src/
│   └── main.cpp                 # Sorgente principale del firmware ESP32
└── 6502/
    ├── Makefile                 # Compila il driver e genera vera_pbi_handler.h
    ├── pbi-driver.ld            # Script linker (indirizzo base $D800, 2 KB)
    ├── inc/
    │   ├── vera_common.inc      # Simboli condivisi VERA/PBI (registri, costanti)
    │   └── pbi-driver.h        # Header interfaccia driver
    └── src/
        └── vera_pbi_handler.s   # Driver ROM PBI per la scheda FPGA VERA (ca65)
```

---

## 4. Modalità di Build

Il progetto compila un unico ambiente PlatformIO (`esp32s3fn8`). Le macro di
configurazione si trovano nella sezione **Compile-time Configuration** di `main.cpp`:

```cpp
#define VERA_BOARD_IS_PBI 0x01   /* 1 = PBI (default), 0 = CCTL */
#define USE_RAMBO_256K           /* definire per abilitare RAMbo 256 KB */
```

### Modalità PBI (`VERA_BOARD_IS_PBI = 1`)

| Range         | Segnali affermati                       | Dati bus               |
|---------------|-----------------------------------------|------------------------|
| $D100–$D11F   | EXTSEL\_N + DEV\_SEL\_N (se selezionato) | VERA FPGA risponde     |
| $D120–$D1FE   | EXTSEL\_N (se selezionato)               | non decodificato       |
| $D1FF         | latch VCS — write `0x80` per selezionare| —                      |
| $D600–$D7FF   | EXTSEL\_N (se selezionato)               | ESP32 → `ram_pbi[]`    |
| $D800–$DFFF   | MPD (se selezionato)                    | ESP32 → `pbi_driver[]` |

`build_drive_lut()` precalcola al boot le bitmask per D0–D7, rendendo
`bus_drive()` una sola scrittura APB a `GPIO.out`. Viene chiamata in PBI mode, e
anche in CCTL mode se `USE_RAMBO_256K` è definita (RAMbo richiede `bus_drive()`).

### Modalità CCTL (`VERA_BOARD_IS_PBI = 0`)

| Range         | Segnali affermati                       | Dati bus               |
|---------------|-----------------------------------------|------------------------|
| $D500–$D5FE   | DEV\_SEL\_N (se selezionato)             | VERA FPGA risponde     |
| $D5FF         | latch CCTL — write `0x80` per selezionare | —                    |

L'ESP32 non guida D0–D7 per i propri registri in CCTL mode; nessuna RAM
(`ram_pbi`) né ROM (`pbi_driver`) esposta al 6502. Con `USE_RAMBO_256K` attiva,
`bus_drive()` viene comunque usata per la finestra RAMbo $4000–$7FFF.

### Emulatore RAMbo 256 KB (`USE_RAMBO_256K`)

L'ESP32 emula una scheda **RAMbo 256 KB** bank-switched a $4000–$7FFF.
Funziona sia in PBI mode sia in CCTL mode — è indipendente da `VERA_BOARD_IS_PBI`.
Il meccanismo è identico all'hardware originale:

| PORTB bit | Significato |
|-----------|-------------|
| 4 = **0** | RAMbo **attivo** — finestra $4000–$7FFF → banco selezionato |
| 4 = **1** | RAMbo disabilitato — RAM interna Atari risponde normalmente |
| 6,5,3,2   | Selezione banco (0–15): `bank = ((portb>>2)&0x03)\|((portb>>3)&0x0C)` |

Tabella dei 16 valori PORTB ($D301):

| Banco | PORTB (hex) | PORTB (bin) | Bit attivi |
|-------|-------------|-------------|------------|
| 0  | $83 | 10000011 | — |
| 1  | $87 | 10000111 | bit 2 |
| 2  | $8B | 10001011 | bit 3 |
| 3  | $8F | 10001111 | bit 2,3 |
| 4  | $A3 | 10100011 | bit 5 |
| 5  | $A7 | 10100111 | bit 2,5 |
| 6  | $AB | 10101011 | bit 3,5 |
| 7  | $AF | 10101111 | bit 2,3,5 |
| 8  | $C3 | 11000011 | bit 6 |
| 9  | $C7 | 11000111 | bit 2,6 |
| 10 | $CB | 11001011 | bit 3,6 |
| 11 | $CF | 11001111 | bit 2,3,6 |
| 12 | $E3 | 11100011 | bit 5,6 |
| 13 | $E7 | 11100111 | bit 2,5,6 |
| 14 | $EB | 11101011 | bit 3,5,6 |
| 15 | $EF | 11101111 | bit 2,3,5,6 |

Il firmware:
1. **Snoopa** ogni write a $D301 nel hot loop e aggiorna `portb_rambo`.
2. Quando `rambo_active` (bit 4 = 0) e l'indirizzo è in $4000–$7FFF:
   - Asserisce **EXTSEL\_N** per silenziare la RAM interna dell'Atari.
   - **Lettura**: guida D0–D7 con il byte dal banco corrispondente in `extended_rambo_256k`.
   - **Scrittura**: memorizza il dato nel banco corrispondente.

`portb_rambo` è inizializzato a `0xFF` (bit 4 = 1 → RAMbo disabilitato) finché il
sistema operativo non esegue il primo write a $D301.

### Area RAM PBI

`ram_pbi[512]` — esposta a $D600–$D7FF in PBI mode — è l'area di
scambio dati tra ESP32 e 6502.

`extended_rambo_256k[256 KB]` — in IRAM BSS (zero Flash cost) — compilato solo
quando `USE_RAMBO_256K` è definita; funziona in entrambe le modalità PBI e CCTL.

### Pre-build automatico del driver 6502

`platformio.ini` dichiara `extra_scripts = pre:pre_build_6502.py`. Lo script
viene eseguito da PlatformIO **prima di qualsiasi compilazione**, garantendo che
`include/vera_pbi_handler.h` sia sempre aggiornato rispetto ai sorgenti 6502.
Il `make` è incrementale: se i sorgenti non sono cambiati non rigenera nulla.

---

## 5. Compilazione e Flashing

È necessario avere **PlatformIO Core** installato (`pip install platformio`).

```bash
# Compila (firmware unificato PBI + CCTL)
pio run -e esp32s3fn8

# Carica il firmware — UART0: GPIO43=TX, GPIO44=RX
pio run -e esp32s3fn8 --target upload

# Monitor seriale (115200 baud)
pio device monitor
```

> **Nota:** USB CDC è disabilitato (`ARDUINO_USB_CDC_ON_BOOT=0`) perché
> GPIO 19 (USB D−) e GPIO 20 (USB D+) sono riutilizzati come linee indirizzo
> A7 e A8. Usare un adattatore UART esterno collegato ai pin GPIO 43/44.

---

## 6. Driver 6502 per ATARI

Il driver ROM risiede in `6502/src/vera_pbi_handler.s` ed è scritto in
assembly 6502 per la toolchain **ca65/ld65** (suite `cc65`). Occupa
$D800–$DFFF (2 KB) quando la scheda è selezionata tramite il latch PBI $D1FF.

Il `Makefile` nella directory `6502/`:
1. Assembla `vera_pbi_handler.s` (con `vera_common.inc` come dipendenza)
2. Linka il binario a $D800 tramite `pbi-driver.ld`
3. Converte il `.bin` in un array C `vera_pbi_handler[]` e lo copia in
   `../include/vera_pbi_handler.h`, pronto per essere incluso nel firmware ESP32

```bash
cd 6502
make

# Pulizia degli artefatti intermedi
make clean
```

> **Dipendenze**: `ca65`, `ld65` (pacchetto `cc65`) e `hexdump` devono essere
> nel `PATH`.
