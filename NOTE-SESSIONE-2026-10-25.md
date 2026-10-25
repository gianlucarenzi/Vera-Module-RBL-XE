# Note di sessione — 2026-10-25

Riassunto del lavoro per riprendere su un altro PC.

**Aggiornamento (fine giornata):** firmware e tesi committati e pushati in `0a2e665`
e `04a165b`. Dopo il push è stata fatta l'**ottimizzazione del percorso critico**
(§3b), con questo file aggiornato: verificare con `git status` se è stata committata.

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

## 3b. Ottimizzazione dopo il commit `0a2e665`

- Percorso critico (attesa → campionamento → decodifica → controlli → dato) scritto in
  **assembly inline** in `MonitorTask`: tutti gli operandi in registro, costanti caricate
  prima dell'attesa. Controlli: **11 cicli + 1 lettura APB** (prima 18); dato: altri
  **14 cicli** (prima 22).
- Le due scritture del dato (`GPIO.out`, `GPIO.enable_w1ts`) e il rilascio del bus sono
  store "posted" senza `memw` (gli store Xtensa restano in ordine).
- Attesa dei fronti di φ2 in assembly: 3 cicli per campione anche sulla discesa (prima 5).
- Stato PIA e latch in una struttura in DRAM (`card`), scrittura pendente in due
  registri (`wr_c`, `wr_info`): meno variabili sullo stack.
- Le pagine `$D1xx`/`$D5xx` (`CT_SLOW`) escono dall'assembly e finiscono in C (non
  pilotano mai il bus).
- `timing_model.py` aggiornato (`CYC_P_DECODE = 11`, `CYC_P_DRIVE = 14`, overshoot
  dell'attesa fino a 5 cicli).

Margine del dato dopo l'ottimizzazione (NTSC; PAL ≈ +5 ns):

| Latenza APB | Caso peggiore | Tipico |
|---|---|---|
| 6 cicli | +98 ns | +134 ns |
| 9 cicli | +61 ns | +97 ns |
| 12 cicli | +23 ns | +59 ns |
| 15 cicli | −14 ns | +22 ns |

**Conforme nel caso peggiore fino a ~13–14 cicli di latenza APB** (prima ~10).

Passo successivo (stesso giorno): la pagina `$D1xx`/`$D5xx` (registri VERA) è decisa
nell'assembly con una seconda tabella `bus_slow[selected][A0-A7]` (`bus_build_slow_tables()`
in `bus_decode.h`, verificata dal test host): `DEV_SEL_N` arriva ~18 cicli + 1 lettura APB
dopo l'attesa invece di ~30. Il percorso del dato ROM/RAM/RAMbo è invariato (limite
~13–14 cicli APB). Provato lo srotolamento del loop di attesa: nel caso peggiore non
guadagna (salto all'indietro e uscita su salto preso), quindi è rimasto il loop semplice.

Terzo giro (valutata la riscrittura completa in assembly: il percorso critico è già
assembly con gli operandi in registro, il guadagno sarebbe 1–3 cicli a fronte di
centinaia di righe non verificabili senza scheda, quindi non fatta):
- tolto il `memw` prima della lettura di `GPIO.in` (l'ultimo store è di ≥150 ns prima):
  decodifica 10 cicli + 1 lettura APB (`CYC_P_DECODE = 10` nel modello);
- effetti delle scritture (RAM, RAMbo, latch, PORTB/PBCTL) spostati **nella coda di φ2
  dello stesso ciclo di scrittura**, subito dopo il campionamento del dato: φ1 contiene
  solo rilascio del bus e preparazione (~12 istruzioni), sempre dentro l'attesa;
- confronti sull'indirizzo a 16 bit (`$D1FF`, `$D301/$D303` con mirror) invece che
  pagina + offset; tabella `bus_pb_state[PORTB]` (`bus_portb_state()` in
  `bus_decode.h`, verificata dal test host) per lo stato RAMbo/autotest;
- log del cambio di latch differito (`card.log_evt`) e inviato nel successivo ciclo di
  lettura, durante l'attesa della discesa di φ2.

Margine del dato nel caso peggiore: NTSC +27 ns a 12 cicli APB, +2 ns a 14 cicli;
PAL +32 / +7 ns. **Conforme fino a 14 cicli di latenza APB.**

Limiti rimasti, non risolvibili solo nel firmware:
- i 3 accessi APB per ciclo letto dall'ESP32 (1 lettura indirizzo + 2 scritture dato);
  con il gating di `~OE` di U19 (§2.1 requisiti HW) le uscite potrebbero restare
  abilitate e il caso medio scenderebbe a 2, ma il caso peggiore (lettura dopo una
  scrittura) resta a 3;
- anticipare `EXTSEL`/`MPD` usando solo A10–A15 (canali Dedicated GPIO, prima della
  lettura APB) è possibile per RAMbo e `$D800`, ma costa ~5 cicli sul percorso del dato:
  non fatto, da valutare dopo le misure;
- gli effetti più lunghi (scrittura su PORTB ~45 cicli) possono superare la coda di φ2
  di qualche decina di ns e ritardare il campionamento del ciclo seguente di altrettanto.
Il paragrafo "Dal disassemblato" di `firmware/MCU/README.md` riporta ancora i conteggi
vecchi: da correggere con il resto della documentazione a fine ottimizzazione.

## 4. Prossimi passi

**Piano per il 2026-10-26:** Gianluca monta il gating di `~OE` di U19 seguendo
`VERA-ATARI-HW-REQUIREMENTS.md` §2.1 (un 74LVC20, U23 pin 4 staccato da U19 pin 22,
pull-up 10 kΩ su `~mVCS0`/`~mEXSEL`/`~mMPD`), più §2.7 (VCCB pin 23 dei 74LVC4245) e §2.8
(ingressi liberi di U23). ~~Dopo il montaggio: tenere abilitate le uscite dati in φ1~~ — **rivisto il
2026-10-26**: con le uscite sempre abilitate, nei cicli di scrittura l'ESP32 deve spegnerle
prima che U19 piloti il lato scheda, e questo vincolo si rompe a ~13–14 cicli APB: nel caso
peggiore non migliora il limite attuale (solo il caso tipico). Il gating resta indispensabile
per eliminare la contesa. Dettagli nella tesi, cap. 8.

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

---

## 6. Sessione 2026-10-26

- **Firmware** (`main.cpp`):
  - l'attesa di assestamento parte dal minore fra la discesa di φ2 rilevata e quella
    prevista (salita + `BUS_PHI2_HIGH_MAX_NS` = 292 ns): dopo effetti lunghi il
    campionamento successivo slitta al più di ~23 ns invece che dell'intero sforamento;
  - scritture in RAM PBI / RAMbo messe in coda (`card.wr_mem`) ed eseguite nel φ1
    successivo (~32 cicli, dentro l'attesa di 36): non ritardano mai il ciclo dopo;
    latch e PORTB restano nella coda di φ2.
- **Modello** (`firmware/tesi/timing_model.py`) riscritto: firmware iniziale (f311311) e
  attuale, scheda con gating, opzione "uscite sempre abilitate", lettura dopo scrittura
  sui registri. Limiti caso peggiore: NTSC 14,1 / PAL 14,5 cicli APB; tipico ~17;
  dopo scrittura su latch/PORTB 12,3 / 12,9.
- **Tesi riscritta** (`firmware/tesi/tesi.tex`, PDF 47 pagine) come progetto con gating e
  firmware attuale: cap. 3 hardware con 74LVC20, cap. 5 firmware nuovo con listato
  assembly, cap. 6 verifica, cap. 7 evoluzione dalla prima versione, cap. 8 questioni
  aperte, cap. 9 piano di misure. Risultati dichiarati come analitici, non misurati.
- Frontespizio ancora con segnaposto (università, relatore, matricola).
