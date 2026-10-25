# Note di sessione — 2026-10-25

Riassunto del lavoro per riprendere su un altro PC. **Niente di quanto segue è
committato** in questo repo: per averlo sull'altro PC bisogna committare e fare
push (oppure copiare i file elencati sotto).

---

## 1. Stato del repo

Branch `master`, allineato a `origin/master` (ultimo commit `f311311`). Modifiche
locali non committate:

| File | Cosa cambia |
|---|---|
| `firmware/MCU/src/main.cpp` | nuovo loop del bus (vedi §3) |
| `firmware/MCU/include/bus_decode.h` | pagina = numero reale A15..A8, 8 stati, RAMbo sempre presente, autotest, DDR |
| `firmware/MCU/test/host/test_bus_decode.c` | test aggiornato (16 777 477 controlli, 0 errori) |
| `firmware/MCU/6502/Makefile` | ROM generata con `DRAM_ATTR` (prima `IRAM_ATTR`) |
| `firmware/MCU/include/vera_pbi_handler.h` | rigenerato dal Makefile (`DRAM_ATTR`) |
| `firmware/MCU/README.md` | architettura nuova, niente `PIN_RAMBO_EN` |
| `PIN-MAPPING.md` | GPIO3 non usato, nuova decodifica con Dedicated GPIO |
| `firmware/tesi/` (nuova) | tesi LaTeX + `timing_model.py` + `data/` |
| `NOTE-SESSIONE-2026-10-25.md` | questo file |

Verifiche fatte: `pio run` compila `esp32s3fn8` e `esp32s3fn8-cctl`; test host ok.
**Mai provato su hardware.**

---

## 2. Analisi fatta (tesi in `firmware/tesi/`)

`tesi.tex` / `tesi.pdf` (47 pagine): architettura, protocollo PBI, RAMbo 320K, verifica
temporale a 1,773447 MHz (PAL) e 1,789773 MHz (NTSC), diagrammi temporali, grafici.
Tutti i numeri vengono da `timing_model.py` → `data/numbers.tex`, `data/slack_*.dat`.

Compilazione: `cd firmware/tesi && python3 timing_model.py && pdflatex tesi.tex && pdflatex tesi.tex`.
Serve TeX Live con `pgfplots`, `tikz-timing`; sillabazione italiana se c'è
`texlive-lang-italian` (altrimenti usa l'inglese).

**Attenzione:** la tesi (testo e PDF) descrive ancora il **firmware vecchio** e il pin
RAMBO_EN. `timing_model.py` invece è già aggiornato al loop nuovo (funzione
`proposed()` = loop implementato), quindi `data/` non corrisponde più al PDF.
Da fare: aggiornare i capitoli 6–8 e la conclusione con il firmware nuovo.

Risultati principali sul firmware **vecchio** (commit `f311311`):
- letture servite dall'ESP32 (ROM `$D800`, RAM `$D600`, RAMbo) fuori specifica in ogni
  caso: dato a ≥ 331 ns dalla salita di φ2, limite 229 ns (setup 6502 a 2 MHz = 50 ns);
- `EXTSEL`/`MPD` asseriti 160–300 ns dopo la salita di φ2;
- tick FreeRTOS sul core del bus: 3–9 cicli persi ogni ms;
- `bus_idx_to_page()` non inline, eseguita da flash a ogni scrittura `$D1xx/$D3xx/$D5xx`;
- ROM e RAMbo in IRAM (accesso a byte → LoadStoreError);
- RAMbo in `.iram0.bss` sovrapposta fisicamente a `.data`/`.bss`/heap;
- il firmware serviva una ROM PBI vecchia (`pbi-driver.h`, 4 KB) invece di
  `vera_pbi_handler.h` (2 KB, quella attuale).

---

## 3. Firmware nuovo (non committato)

- RAMbo 320K **sempre attiva**: GPIO3 nella netlist (U7 pin 8) è scollegato, niente
  RAMBO_EN. GPIO3 è un pin di strapping: **va aggiunta una resistenza esterna**
  (lo fa Gianluca sulla scheda).
- ROM (`vera_pbi_handler[]`) e RAMbo in DRAM accessibile a byte; nessuna sovrapposizione.
- Decodifica durante **φ1**: attesa `BUS_ADDR_SETTLE_NS` (150 ns) dalla discesa di φ2,
  poi campionamento → controlli intorno alla salita di φ2, dato pilotato in anticipo.
- PHI2, R/W, A10–A15 sui canali **Dedicated GPIO in** (8 canali): una sola lettura APB
  (`GPIO.in`) per ciclo.
- Scrittura campionata `BUS_WR_SAMPLE_NS` (120 ns) dopo la salita di φ2.
- Loop su **core 1 a interruzioni mascherate**; `LogTask` + `Serial` su core 0;
  watchdog delle interruzioni (MWDT1) disattivato dal loop.
- `bus_drive()` = `GPIO.out = byte << 4` (niente tabella), offset RAMbo senza salti.
- Autotest `$5000–$57FF` con priorità sulla RAMbo; PORTB = uscita | ~DDR; mirror PIA `$D3xx`.

Bilancio (dal disassemblato): dopo l'attesa 18 cicli + 1 lettura APB → controlli;
+22 cicli + 2 scritture APB → dato. Margine del dato (NTSC):

| Latenza APB | Caso peggiore | Tipico |
|---|---|---|
| 6 cicli | +56 ns | +82 ns |
| 9 cicli | +19 ns | +45 ns |
| 12 cicli | −19 ns | +7 ns |

**Conforme nel caso peggiore se la latenza APB ≤ ~10 cicli → va misurata.**

---

## 4. Prossimi passi

1. Misurare la latenza APB di `GPIO.in` su una qualsiasi ESP32-S3 (programma nella tesi,
   §9.1, con `xthal_get_ccount()`); mettere il valore in `SCENARIOS` di `timing_model.py`.
2. Aggiungere la resistenza di strapping su GPIO3.
3. Aggiornare la tesi con il firmware nuovo; ricompilare il PDF.
4. Decidere se cancellare `include/pbi-driver.h` e `6502/inc/pbi-driver.h` (ROM vecchia,
   non più usate).
5. Committare.
6. Su hardware: misure con analizzatore logico (§9.2 della tesi), regolare
   `BUS_ADDR_SETTLE_NS` / `BUS_WR_SAMPLE_NS` con `-D`.
7. Hardware ancora aperto (`VERA-ATARI-HW-REQUIREMENTS.md` §2.1–2.3): gating di
   `~OE` di U19, hold della scrittura verso la VERA (margine −32…−2 ns), IRQ open-drain.
8. Rischio strapping GPIO45 (A14 → VDD_SPI): se A14 è alto al power-on la flash va a
   1,8 V. Il commento in `main.cpp` presume il bus indirizzi in alta impedenza durante
   il reset del 6502: da verificare (eventualmente eFuse `VDD_SPI_FORCE`).
9. Se la latenza APB risulta alta: spostare ROM/RAM/RAMbo nella FPGA di interfaccia
   (`firmware/FPGA/C6502_Interface.vhd`).

---

## 5. Altro fatto oggi (repo AMIGA/stalker, già pushato)

`~/Progetti/AMIGA/stalker`: firmware v3.2NG-RTOS + bootloader v1.1, tag
`FWVER3.2-NG-RTOS`, release GitHub con i due `.hex`, `production.sh` che si ferma al
primo errore (commit `34d2a01`, `e6e3afe`, `b05ac19`). Da provare su scheda.
