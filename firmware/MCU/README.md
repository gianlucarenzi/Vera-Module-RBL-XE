# Vera-Module-RBL-XE — Firmware MCU

## 1. Introduzione

Firmware per il microcontrollore **ESP32-S3FN8** (QFN-56, 8 MB flash integrata,
dual-core Xtensa LX7 @ 240 MHz) montato sul modulo Vera-Module-RBL-XE.

> **Requisito hardware:** il chip richiede un **quarzo esterno 40 MHz** collegato
> ai pin dedicati **XTAL\_P** (QFN56 pin 54) e **XTAL\_N** (QFN56 pin 53).
> Senza di esso il chip non si avvia. Vedere `PIN-MAPPING.md` § 5 per il
> circuito consigliato (condensatori di carico, resistenza serie, layout PCB).

Il modulo si collega al computer **ATARI XE** tramite il connettore **PBI**
(Parallel Bus Interface) oppure tramite il segnale **CCTL** (Cartridge Control).
La modalità operativa è selezionata a **tempo di compilazione** tramite la macro
`VERA_BOARD_IS_PBI` in `main.cpp`: `1` per PBI (default), `0` per CCTL.
L'emulazione **RAMbo 320K** (256 KB in 16 banchi a $4000–$7FFF) è sempre attiva:
non c'è un pin di abilitazione. Funziona indipendentemente dalla modalità PBI/CCTL.
Il bus indirizzi A0–A15 è decodificato completamente via software in entrambe
le modalità.

---

## 2. Architettura del Firmware

### Sequenza di startup

`setup()` (Core 1) abbassa **ARESET** e **CRESET** immediatamente dopo il reclaim
dei pin JTAG, garantendo che Atari e FPGA VERA rimangano in reset per tutta la
durata dell'inizializzazione ESP32. Poi:

1. crea `LogTask` sul **Core 0**, che apre `Serial` (l'interrupt della UART va
   sul core che chiama `Serial.begin`);
2. costruisce le tabelle di decodifica e configura i GPIO;
3. **CRESET → HIGH**: la FPGA VERA esce dal reset e carica il bitstream dalla
   flash SPI on-board;
4. **Poll CDONE** (GPIO 39) finché il pin va HIGH — ICE40UP5K tipicamente
   < 100 ms. Timeout 5 s con log di errore;
5. **Attesa `VERA_POST_CDONE_DELAY_US`** (default 1000 µs): copre il power-on
   reset interno della VERA, che parte solo dopo `CDONE`;
6. crea `MonitorTask` sul **Core 1** con priorità massima: il task prende il core
   e `setup()` non riprende più. `MonitorTask` crea i bundle Dedicated GPIO,
   disattiva il watchdog delle interruzioni, maschera le interruzioni e porta
   **ARESET → HIGH**: da questo momento il bus è live.

### Divisione dei core

| Core | Cosa gira | Interrupt |
|------|-----------|-----------|
| **0** | `LogTask`: svuota `log_ring` e stampa su `Serial`; tick FreeRTOS | tick, UART0 |
| **1** | solo `MonitorTask` (loop del bus, priorità `configMAX_PRIORITIES - 1`, non cede mai la CPU) | **nessuno** (mascherati) |

Il loop del bus gira a interruzioni mascherate (`portDISABLE_INTERRUPTS()`): il
tick di FreeRTOS a 1 kHz gli faceva perdere alcuni cicli di bus ogni millisecondo.
Il contatore dei tick resta aggiornato dal Core 0. Il watchdog delle interruzioni
(MWDT1) controlla anche il Core 1 tramite il suo tick hook, che non gira più:
`MonitorTask` lo disattiva prima di mascherare le interruzioni.

Il loop passa gli eventi (cambio di latch, registri VERA con
`-DVERA_TRACE_REGS=1`) a `LogTask` tramite `log_ring`, un ring buffer
single-producer/single-consumer senza lock: il push costa poche istruzioni e
non chiama FreeRTOS; il timestamp è aggiunto da `LogTask` quando stampa. Se il
ring è pieno l'evento viene scartato e `LogTask` stampa quanti ne sono stati persi.

### Loop del bus (Core 1, interruzioni mascherate)

Il 6502 presenta indirizzo e R/W al più `t_ADS` = 140 ns dopo l'inizio di PHI1,
cioè circa 140 ns **prima** della salita di PHI2, e campiona il dato in lettura
50 ns prima della discesa di PHI2. Il loop quindi decodifica durante PHI1:

1. Alla discesa di PHI2 (rilevata con **Dedicated GPIO in**, `ee.get_gpio_in`,
   1 ciclo CPU) chiude il ciclo precedente: rilascia il bus dati e i segnali di
   controllo, e applica gli effetti dell'eventuale scrittura (RAM PBI, banco
   RAMbo, latch `$D1FF`/`$D5FF`, snoop di PORTB/PBCTL).
2. Attende `BUS_ADDR_SETTLE_NS` (default 150 ns) dalla discesa di PHI2, contati
   con `CCOUNT`, perché indirizzo e R/W siano garantiti stabili.
3. Campiona l'indirizzo con **una sola lettura APB**: PHI2, R/W e A10–A15 sono
   anche sui canali Dedicated GPIO in, letti in un ciclo; `GPIO.in` dà A0–A9.
   La pagina A8–A15 indicizza la tabella dello stato corrente
   (`include/bus_decode.h`): livelli di DEV\_SEL\_N / EXTSEL\_N / MPD e azioni.
   Solo `$D1xx` (PBI) e `$D5xx` (CCTL) guardano anche A0–A7. Le tabelle sono 8
   (latch selezionato × RAMbo attiva × ROM di autotest visibile) e si cambiano
   per puntatore quando cambia lo stato.
4. Afferma i segnali di controllo con **Dedicated GPIO out**
   (`ee.wr_mask_gpio_out`) intorno alla salita di PHI2, come un decodificatore
   combinatorio, invece che oltre 150 ns dopo.
5. **Lettura:** se la pagina lo prevede, guida D0–D7 con `bus_drive()` (ROM PBI
   `$D800–$DFFF`, RAM PBI `$D600–$D7FF`, banco RAMbo) senza attendere la salita
   di PHI2: U19 porta il dato al bus Atari solo mentre PHI2 è alto. Poi attende
   la discesa di PHI2.
6. **Scrittura:** campiona `GPIO.in` `BUS_WR_SAMPLE_NS` (default 120 ns) dopo la
   salita di PHI2 (il dato del 6502 è valido da `t_MDS` = 100 ns fino a 30 ns
   dopo la discesa) e attende la discesa con Dedicated GPIO.

Dal disassemblato: dopo l'attesa di assestamento servono 18 cicli più una
lettura APB per i segnali di controllo, e altri 22 cicli più due scritture APB
per il dato. Il bilancio temporale completo è in `firmware/tesi/` (cap. 6 e 8);
in sintesi, il dato rispetta il setup del 6502 nel caso peggiore se la latenza
di un accesso APB è al massimo ~10 cicli CPU, da misurare (programma di prova
nella tesi, §9.1). `BUS_ADDR_SETTLE_NS` e `BUS_WR_SAMPLE_NS` si possono
regolare con `-D` dopo aver misurato il bus reale.

La logica di decodifica non dipende da ESP-IDF ed è verificata sul PC contro
una versione a confronti di range, per tutti gli indirizzi e stati:

```bash
cd test/host
cc -O2 -Wall -I../../include test_bus_decode.c -o test_bus_decode && ./test_bus_decode
```

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
| Input   | bit 1  | GPIO 2 (pin 7) | R/W         |                       |
| Input   | bit 2–7 | GPIO 35, 36, 33, 34, 45, 46 | A10–A15 | Anche in `GPIO.in1` |
| Output  | bit 0  | GPIO 40 (pin 45)| DEV\_SEL\_N | Chip-select VERA      |
| Output  | bit 1  | GPIO 41 (pin 47)| EXTSEL\_N   | Disabilita MMU ATARI  |
| Output  | bit 2  | GPIO 42 (pin 48)| MPD         | Disabilita Math Pack  |

I pin GPIO 39–42 erano originariamente riservati a JTAG; vengono liberati in
`setup()` tramite `gpio_reset_pin()` prima della creazione del bundle.

### Mappatura del bus (riepilogo)

| Segnale      | GPIO        | QFN-56 pin | Note                              |
|--------------|-------------|------------|-----------------------------------|
| PHI2         | 1           | 6          | Input dedicated                   |
| R/W          | 2           | 7          | Input dedicated                   |
| D0–D7        | 4–11        | 9–16       | Bus dati bidirezionale            |
| A0–A9        | 12–21       | 17–27      | A0–A7 via U4, A8–A9 via U3; GPIO 19=USB D−, 20=USB D+ (USB disabilitato) |
| A10          | 35          | 40         | Address bit 10 (via U3)           |
| A11          | 36          | 41         | Address bit 11 (via U3)           |
| A12          | 33          | 38         | Address bit 12 (via U3)           |
| A13          | 34          | 39         | Address bit 13 (via U3)           |
| A14          | 45          | 51         | Address bit 14 (via U3); strapping VDD\_SPI |
| A15          | 46          | 52         | Address bit 15 (via U3); strapping ROM msg / download |
| ARESET       | 37          | 42         | System reset ATARI (output)       |
| BOOT0        | 0           | 5          | Pulsante BOOT / auto-reset (non usato dal firmware) |
| CRESET       | 38          | 43         | Chip reset VERA (output)          |
| CDONE        | 39          | 44         | VERA programmata (input)          |
| —            | 3           | 8          | Non usato; strapping (JTAG source), pull-up R71 10 kΩ |
| DEV\_SEL\_N  | 40          | 45         | Chip-select VERA (output, dedicated) |
| EXTSEL\_N    | 41          | 47         | Disabilita MMU (output, dedicated)|
| MPD          | 42          | 48         | Disabilita Math Pack (dedicated)  |
| ABUS\_OE\_N  | 47          | 37         | `~OE` di U3/U4 (output): alto durante il reset (pull-up R7/R34) per gli strapping di GPIO 45/46, basso da `setup()` |
| Debug TX     | 43          | 49         | UART0 TX (Serial)                 |
| Debug RX     | 44          | 50         | UART0 RX (Serial)                 |

Tutti i 16 bit di indirizzo sono decodificati: la pagina (A8–A15) indicizza le
tabelle di `bus_decode.h`, A0–A7 l'offset nella pagina. A10–A15 arrivano dai
canali Dedicated GPIO, A0–A9 dall'unica lettura APB di `GPIO.in`.

Fino a quando `setup()` porta basso `PIN_ABUS_OE_N`, U3 e U4 sono spenti: GPIO 45/46
(A14/A15) leggono LOW dai pull-down interni a ogni reset del chip, quindi VDD\_SPI = 3,3 V
e il download boot funziona anche con l'Atari acceso (`VERA-ATARI-HW-REQUIREMENTS.md` §2.9).

Per la mappatura completa GPIO→QFN-56 e i level shifter vedere `PIN-MAPPING.md` nella root.

---

## 3. Struttura dei File

```
firmware/MCU/
├── platformio.ini               # Ambienti: esp32s3fn8, esp32s3fn8-cctl
├── pre_build_6502.py            # Script pre-build PlatformIO: esegue make in 6502/
├── include/
│   ├── bus_decode.h             # Tabelle di decodifica del bus, senza dipendenze IDF
│   ├── pbi-driver.h             # ROM PBI precedente (4 KB), non più usata
│   └── vera_pbi_handler.h       # ROM PBI servita a $D800 (2 KB, generata da make, DRAM_ATTR)
├── src/
│   └── main.cpp                 # Sorgente principale del firmware ESP32
├── test/host/
│   └── test_bus_decode.c        # Test di equivalenza della decodifica (gcc sul PC)
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

Ambienti PlatformIO disponibili (`platformio.ini`):

| Ambiente | Modalità | Note |
|---|---|---|
| `esp32s3fn8` | PBI (`$D1FF`) | produzione, scheda senza modifiche |
| `esp32s3fn8-cctl` | CCTL (`$D5FF`) | slot cartuccia |

Opzioni di compilazione (`-D`):

| Macro | Default | Effetto |
|---|---|---|
| `VERA_BOARD_IS_PBI` | 1 | 1 = PBI, 0 = CCTL (impostata dall'ambiente) |
| `VERA_TRACE_REGS` | 0 | log di ogni accesso ai registri VERA. **Solo debug**: in lettura campiona il bus fino alla discesa di PHI2 e ritarda il ciclo seguente |
| `BUS_ADDR_SETTLE_NS` | 150 | attesa dalla discesa di PHI2 al campionamento dell'indirizzo (`t_ADS` + traslatori) |
| `BUS_WR_SAMPLE_NS` | 120 | ritardo dalla salita di PHI2 al campionamento del dato in scrittura |

La modalità PBI/CCTL è scelta tramite la macro in `main.cpp`:

```cpp
#define VERA_BOARD_IS_PBI 0x01   /* 1 = PBI (default), 0 = CCTL */
```

La RAMbo è sempre presente, in entrambe le modalità: segue il bit 4 di PORTB.

### Modalità PBI (`VERA_BOARD_IS_PBI = 1`)

> **Regola PBI Atari.** La scheda risponde solo se selezionata tramite il proprio bit in
> `$D1FF`; da deselezionata non tocca il bus, così può convivere con altri device PBI che
> usano gli stessi indirizzi `$D1xx`. I registri VERA (`$D100–$D11F`) sono quindi
> raggiungibili solo con la scheda selezionata: dal driver in ROM (l'OS seleziona la scheda
> prima di `INIT`, dell'handler IRQ e delle chiamate CIO) oppure da codice utente che scrive
> il bit in `$D1FF`, accede ai registri e la deseleziona. Mentre la scheda è selezionata il
> Math Pack (`$D800–$DFFF`) è sostituito dalla ROM PBI: niente floating point in quella finestra.

| Range         | Segnali affermati                       | Dati bus               |
|---------------|-----------------------------------------|------------------------|
| $D100–$D11F   | EXTSEL\_N + DEV\_SEL\_N (se selezionato) | VERA FPGA risponde     |
| $D120–$D1FE   | EXTSEL\_N (se selezionato)               | non decodificato       |
| $D1FF         | latch VCS — **scrittura**: D7 = 1 seleziona, D7 = 0 deseleziona (si usa solo il proprio bit). **Lettura**: non pilotata (l'identificazione IRQ via `$D1FF` non è realizzabile con `U19`, vedere `VERA-ATARI-HW-REQUIREMENTS.md` §2.4) | —                      |
| $D600–$D7FF   | EXTSEL\_N (se selezionato)               | ESP32 → `ram_pbi[]`    |
| $D800–$DFFF   | MPD (se selezionato)                    | ESP32 → `vera_pbi_handler[]` |

D0–D7 sono su GPIO 4–11 contigui: `bus_drive()` scrive `byte << 4` in `GPIO.out`
e abilita le uscite con `GPIO.enable_w1ts` (due scritture APB).

### Modalità CCTL (`VERA_BOARD_IS_PBI = 0`)

| Range         | Segnali affermati                       | Dati bus               |
|---------------|-----------------------------------------|------------------------|
| $D500–$D5FE   | DEV\_SEL\_N (se selezionato)             | VERA FPGA risponde     |
| $D5FF         | latch CCTL — write `0x80` per selezionare | —                    |

L'ESP32 non guida D0–D7 per i propri registri in CCTL mode; nessuna RAM
(`ram_pbi`) né ROM esposta al 6502. `bus_drive()` serve comunque per la finestra
RAMbo $4000–$7FFF.

### Emulatore RAMbo 320K

L'ESP32 emula una scheda **RAMbo 320K** (64 KB di base + 256 KB in 16 banchi da
16 KB) bank-switched a $4000–$7FFF, sempre attiva, sia in PBI mode sia in CCTL
mode. Il meccanismo è identico all'hardware originale. La PIA 6520 dell'Atari espone due
registri sovrapposti a `$D301` — Output Register e Data Direction Register (DDR) — la
scelta dipende da **PBCTL** (`$D303`) bit 2 (CRB-2):

| PBCTL bit 2 | Registro accessibile a $D301 |
|-------------|------------------------------|
| **0** | Data Direction Register (DDR) |
| **1** | Output Register = **PORTB** |

Il banco segue i livelli dei pin di Port B: bit del registro di uscita dove il
DDR è 1, livello alto (pull-up) dove il DDR è 0. Dopo il reset della PIA entrambi
i registri valgono 0 e i pin leggono `$FF` (RAMbo disabilitata).

| PORTB bit | Nome | Significato |
|-----------|------|-------------|
| 4 = **0** | **RAME** | RAMbo **attivo** — finestra $4000–$7FFF → banco selezionato; EXTSEL\_N asserito |
| 4 = **1** | RAME | RAMbo disabilitato — RAM interna Atari risponde normalmente |
| 6,5,3,2   | B3:B0 | Selezione banco (0–15): `bank = ((portb>>2)&0x03)\|((portb>>3)&0x0C)` |
| 0 = 1, 7 = 0 | — | ROM di autotest visibile: ha la precedenza su $5000–$57FF, che la scheda non intercetta |

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

Il firmware nel loop del bus:
1. **Snoopa `$D303`** (PBCTL) ad ogni write e aggiorna `pbctl`.
2. **Snoopa `$D301`**: con `PBCTL` bit 2 = 1 aggiorna il registro di uscita, altrimenti il DDR.
   La PIA decodifica solo A0–A1, quindi valgono anche i mirror in `$D300–$D3FF`.
3. Quando la RAMbo è attiva (pin RAME = 0) e l'indirizzo è in $4000–$7FFF:
   - Asserisce **EXTSEL\_N** per silenziare la RAM interna dell'Atari.
   - **Lettura**: guida D0–D7 con il byte dal banco in `extended_rambo_256k`.
   - **Scrittura**: memorizza il dato nel banco corrispondente.

#### Snooping passivo vs. pilotaggio attivo — nessun conflitto col 6520 fisico

Il chip **6520 PIA è fisicamente presente** sulla scheda madre dell'Atari e risponde a `$D301`/`$D303` esattamente come farebbe senza il modulo. Il firmware **non emula** il 6520: lo **snoopa**.

| Range | Chi risponde | Ruolo dell'ESP32 |
|-------|-------------|-----------------|
| `$D301` / `$D303` | 6520 PIA (hardware reale) | Solo snooping passivo — non pilota mai D0–D7 |
| `$4000–$7FFF` (RAMbo window) | ESP32 | Attivo: asserisce EXTSEL\_N, pilota D0–D7 |

Quando il 6502 scrive a `$D301` o `$D303`, il 6520 riceve e processa normalmente la transazione; l'ESP32 legge gli stessi dati sul bus e aggiorna le proprie copie (registro di uscita, DDR, PBCTL). Non c'è mai contesa sul bus perché l'ESP32 non tenta mai di guidare D0–D7 per quegli indirizzi.

Il motivo per cui il firmware deve tracciare PBCTL è proprio questo: essendo un osservatore passivo, non può interrogare il 6520 per sapere quale registro è esposto a `$D301`. Dal bus, una write DDR e una write all'Output Register sono **indistinguibili** — stesse linee, stesso ciclo. Solo tenendo traccia di quanto scritto a `$D303` è possibile qualificare correttamente le write a `$D301`.

```
CPU scrive $FF → $D301 con PBCTL=0x00:
  ├── 6520: aggiorna il DDR (tutti pin output)  ← hardware reale
  └── ESP32: PBCTL & 0x04 = 0 → ignora          ← snooping qualificato

CPU scrive $83 → $D301 con PBCTL=0x34:
  ├── 6520: aggiorna l'Output Register, porta Port B = $83  ← hardware reale
  └── ESP32: PBCTL & 0x04 = 1 → PORTB = $83, banco 0       ← catturato
```

Sequenza tipica di init del sistema operativo Atari:

| Step | Write | Effetto |
|------|-------|---------|
| 1 | `$00` → `$D303` | PBCTL bit 2 = 0 → DDR selezionato a $D301 |
| 2 | `$FF` → `$D301` | Write DDR (tutti output) — registrata come DDR |
| 3 | `$34` → `$D303` | PBCTL bit 2 = 1 → Output Register selezionato a $D301 |
| 4 | `$NN` → `$D301` | Selezione banco — registrata come registro di uscita |

Le copie partono dallo stato di reset della PIA: registro di uscita e DDR a `0x00`
(pin a `$FF`, RAMbo disabilitata), `PBCTL` a `0x00` (DDR esposto a $D301).

### Area RAM PBI

`ram_pbi[512]` — esposta a $D600–$D7FF in PBI mode — è l'area di
scambio dati tra ESP32 e 6502.

`extended_rambo_256k[256 KB]` — `.bss` ordinaria in DRAM (zero Flash cost). Non
può stare in IRAM: il loop vi accede a byte, e una sezione fuori dal layout IRAM
di ESP-IDF non viene riservata sul bus dati (si sovrapporrebbe a `.data`, `.bss`
e heap).

### Pre-build automatico del driver 6502

`platformio.ini` dichiara `extra_scripts = pre:pre_build_6502.py`. Lo script
viene eseguito da PlatformIO **prima di qualsiasi compilazione**, garantendo che
`include/vera_pbi_handler.h` sia sempre aggiornato rispetto ai sorgenti 6502.
Il `make` è incrementale: se i sorgenti non sono cambiati non rigenera nulla.

---

## 5. Compilazione e Flashing

È necessario avere **PlatformIO Core** installato (`pip install platformio`).

> ⚠ **Programmazione e USB solo a scheda scollegata dall'Atari.** La rete 5 V della scheda è unita al +5 V dell'Atari (`CART1` pin 13, `ECI1` pin 7) senza diodo: con il cavo USB (`J4`) o il jack (`J5`) collegati mentre la scheda è inserita, la scheda alimenterebbe l'Atari (spento) o lavorerebbe in parallelo al suo alimentatore (acceso). Vale anche per il monitor seriale: con la scheda nell'Atari usare un adattatore senza VBUS. (`VERA-ATARI-HW-REQUIREMENTS.md` §2.11)

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
