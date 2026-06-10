# Vera-Module-RBL-XE — Firmware MCU

## 1. Introduzione

Firmware per il microcontrollore **ESP32-S3FN8** (QFN-56, 8 MB flash integrata,
dual-core Xtensa LX7 @ 240 MHz) montato sul modulo Vera-Module-RBL-XE.

Il modulo si collega al computer **ATARI XE** tramite il connettore **PBI**
(Parallel Bus Interface) oppure tramite il segnale **CCTL** (Cartridge Control),
a seconda dell'immagine firmware compilata.

---

## 2. Architettura del Firmware

### Hot loop (Core 1, priorità massima FreeRTOS)

Il task `MonitorTask()` gira sul Core 1 con priorità `configMAX_PRIORITIES - 1`.
Ad ogni ciclo di clock del 6502 (PHI2 @ 1,79 MHz, finestra utile ≈ 279 ns a
240 MHz di CPU):

1. Attende il fronte di salita di PHI2 tramite **Dedicated GPIO in**
   (`ee.get_gpio_in`, 1 ciclo CPU anziché ~6 cicli via APB).
2. Legge il bus indirizzi A0–A11 e la linea R/W dai registri `GPIO.in` /
   `GPIO.in1.val` (due letture APB sequenziali).
3. Decodifica l'indirizzo via software (`decode_addr()`) e determina se l'accesso
   riguarda VERA ($D100–$D11F), il range di interrupt ($D120–$D1FE), il range
   esteso ($D600–$D7FF) o la ROM mappata ($D800–$DFFF).
4. Afferma in modo atomico i segnali di controllo attivi-LOW (EXTSEL\_N,
   DEV\_SEL\_N, MPD) tramite **Dedicated GPIO out**
   (`ee.wr_mask_gpio_out`, 1 ciclo CPU — tutti e tre i segnali in un'unica
   istruzione TIE).
5. Se l'accesso è in lettura verso VERA, guida il bus dati (D0–D7) con
   `bus_drive()`, che scrive `GPIO.out` e `GPIO.enable_w1ts` (2 scritture APB,
   ottimizzato rispetto alle 3 scritture precedenti).
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
| A10          | 35          | 40         | Address bit 10                    |
| A11          | 36          | 41         | Address bit 11                    |
| ARESET       | 37          | 42         | System reset ATARI (output)       |
| CRESET       | 38          | 43         | Chip reset VERA (output)          |
| CDONE        | 39          | 44         | VERA programmata (input)          |
| DEV\_SEL\_N  | 40          | 45         | Chip-select VERA (output, dedicated) |
| EXTSEL\_N    | 41          | 46         | Disabilita MMU (output, dedicated)|
| MPD          | 42          | 48         | Disabilita Math Pack (dedicated)  |
| Debug TX     | 43          | 49         | UART0 TX (Serial)                 |
| Debug RX     | 44          | 50         | UART0 RX (Serial)                 |

Per la mappatura completa GPIO→QFN-56 vedere `PIN-MAPPING.md` nella root.

---

## 3. Struttura dei File

```
firmware/MCU/
├── platformio.ini               # Configurazione build (due target: pbi, cctl)
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

Il progetto definisce due ambienti PlatformIO in `platformio.ini`:

| Ambiente         | `BUS_MODE` | Descrizione                                |
|------------------|------------|--------------------------------------------|
| `esp32s3fn8-pbi` | `0`        | Interfaccia PBI (indirizzo completo A0–A11)|
| `esp32s3fn8-cctl`| `1`        | Modalità CCTL (cartuccia)                  |

Entrambi usano `-D USE_DEDICATED_GPIO` e `-D USE_ESP32_REGISTER_ACCESS` per
abilitare l'hot loop ottimizzato.

---

## 5. Compilazione e Flashing

È necessario avere **PlatformIO Core** installato (`pip install platformio`).

```bash
# Compila per modalità PBI
pio run -e esp32s3fn8-pbi

# Compila per modalità CCTL
pio run -e esp32s3fn8-cctl

# Carica il firmware (PBI) — UART0: GPIO43=TX, GPIO44=RX
pio run -e esp32s3fn8-pbi --target upload

# Carica il firmware (CCTL)
pio run -e esp32s3fn8-cctl --target upload

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
