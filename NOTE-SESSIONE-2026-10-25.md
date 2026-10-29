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

**Correzione (2026-10-26):** nessuna scheda è stata costruita. Le modifiche hardware vanno
nello schema, nella netlist e nel PCB del primo lotto di produzione (vedi §8), non su una
scheda esistente. Piano originale: gating di `~OE` di U19 seguendo
`VERA-ATARI-HW-REQUIREMENTS.md` §2.1 (un 74LVC20, poi diventato due SN74LVC00A),
U23 pin 4 staccato da U19 pin 22, pull-up 10 kΩ su `~mVCS0`/`~mEXSEL`/`~mMPD`, più §2.7
(VCCB pin 23 dei 74LVC4245) e §2.8 (ingressi liberi di U23). ~~Dopo il montaggio: tenere abilitate le uscite dati in φ1~~ — **rivisto il
2026-10-26**: con le uscite sempre abilitate, nei cicli di scrittura l'ESP32 deve spegnerle
prima che U19 piloti il lato scheda, e questo vincolo si rompe a ~13–14 cicli APB: nel caso
peggiore non migliora il limite attuale (solo il caso tipico). Il gating resta indispensabile
per eliminare la contesa. Dettagli nella tesi, cap. 8.

1. Misurare la latenza APB di `GPIO.in` su una qualsiasi ESP32-S3 (programma nella tesi,
   §9.1, con `xthal_get_ccount()`); mettere il valore in `SCENARIOS` di `timing_model.py`.
2. ~~Aggiungere la resistenza di strapping su GPIO3.~~ Fatto nello schema (`R71`, §8).
3. Aggiornare la tesi con il firmware nuovo; ricompilare il PDF.
4. Decidere se cancellare `include/pbi-driver.h` e `6502/inc/pbi-driver.h` (ROM vecchia,
   non più usate).
5. Committare.
6. Su hardware: misure con analizzatore logico (§9.2 della tesi), regolare
   `BUS_ADDR_SETTLE_NS` / `BUS_WR_SAMPLE_NS` con `-D`.
7. Hardware ancora aperto (`VERA-ATARI-HW-REQUIREMENTS.md` §2.1–2.3): gating di
   `~OE` di U19, hold della scrittura verso la VERA (margine −32…−2 ns), IRQ open-drain.
8. ~~Rischio strapping GPIO45/46~~ risolto nello schema e nel firmware (§8): `~OE` di U3/U4
   pilotato da GPIO 47 con pull-up, niente eFuse.
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

---

## 7. Memoria di lavoro (copia di quella salvata da Claude su questo PC)

La memoria di Claude Code sta in `~/.claude/projects/…/memory/` e non passa da un PC
all'altro: questa sezione ne riporta il contenuto.

- **Stato del lavoro sul bus (2026-10-26).** Il firmware decodifica in φ1 con il percorso
  critico in assembly (commit `0a2e665` → `3d7f75f`). Secondo il modello rispetta il setup
  del 6502 nel caso peggiore solo se un accesso APB ai registri GPIO dura al massimo ~14
  cicli CPU (NTSC 14,1, PAL 14,5). La latenza non è mai stata misurata e nulla è stato
  provato su hardware: prima di altre ottimizzazioni servono la misura della latenza APB e
  le misure con l'oscilloscopio. Tenere le uscite dati sempre abilitate dopo il gating
  **non** estende il limite nel caso peggiore (vincolo di spegnimento nei cicli di
  scrittura a ~13–14 cicli): non riproporlo come guadagno.
- **Hardware: nessuna scheda costruita.** Tutte le correzioni vanno nel progetto KiCad
  (schema → netlist → "Update PCB from netlist") per il primo lotto di produzione, non come
  modifiche a mano. Stato al 2026-10-26 in §8. La RAMbo 320K è sempre attiva: non esiste un
  pin RAMBO_EN.
- **Metodo di lavoro tra i due PC.** Alla fine di ogni blocco di lavoro aggiornare questo
  file (stato, file non committati, prossimi passi, correzioni ad affermazioni precedenti)
  prima del commit. Commit e push solo su richiesta, direttamente su `master`.
- **Riferimento.** Manuale Altirra in locale:
  `~/Progetti/ATARI/AT2019/documents/Altirra-Hardware-Reference-Manual.pdf` (versione
  07/05/15): §2.2 clock (NTSC 1,789773 / PAL 1,773447 MHz), §3.4–3.5 banking di PORTB e
  schema "320K Rambo" (bit 2, 3, 5, 6 per il banco, bit 4 per CPU+ANTIC), cap. 10 PBI
  (`$D1FF`, MPD, EXTSEL). Per leggerlo: `pdftotext -layout`.

---

## 8. Sessione 2026-10-26 (pomeriggio): schema, netlist e strapping

Nessuna scheda è stata costruita: tutto è nello schema (`.sch`) e nella netlist
(`VERA-MODULE-RBL.net`); il PCB va aggiornato con "Update PCB from netlist". Niente commit.

- **Gating di `~OE` di U19** (requisiti §2.1) con **due SN74LVC00A** (TSSOP-14, LCSC C7803)
  invece del 74LVC20, introvabile: `U24` (n1 = NAND(mR~W, ~mVCS0), n2 = NAND(~mEXSEL, ~mMPD),
  a1/a2 = NAND usate come invertitori) e `U25` (B = NAND(a1, a2), `~OE` = NAND(mPHI2, B) →
  U19 pin 22), porte libere a GND, 100 nF `C67`/`C68`. Pull-up `R36`/`R53`/`R65`. Quattro
  livelli di NAND: con LVC il margine non cambia (ESP32 +27/+2 ns, VERA +136/+128 ns NTSC a
  12/14 cicli APB); con 74HC00 a 3,3 V il ritardo sarebbe stato al limite (~37–39 ns/porta).
  KiCad aveva chiamato i chip U1 e U20 (U20 esiste in `addressdecoder.sch`, fuori gerarchia):
  rinominati U24/U25.
- **U23**: unità 2 (pin 3→4) non più usata; ingressi 3, 5, 9, 11, 13 a GND.
- **VCCB** (pin 23) dei cinque 74LVC4245 collegato al pin 24 (3V3) (§2.7).
- **Canali di riserva**: ingressi `U6` pin 10 e `U16` pin 14, 15, 19, 20, 21 a GND, ciascuno con una propria
  0 Ω (`R74`, `R72`, `R75`, `R73`, `R76`, `R77`; scelta dell'utente per il DRC e per poter riusare i canali).
  Masse di `U6` (pin 11–13) dirette a GND.
- **Strapping ESP32** (requisiti §2.9):
  - GPIO 3: pull-up `R71` 10 kΩ (C84376);
  - `C25` (CHIP_PU) 100 nF → 1 µF (C2798196): 4,7 ms contro 0,47 ms di BOOT0;
  - GPIO 45/46 (A14/A15): `~OE` di U3 e U4 = rete `~ABUS_OE`, pilotata da **GPIO 47**
    (`PIN_ABUS_OE_N`), pull-up `R7`/`R34` (erano 0 Ω a GND, ora 10 kΩ → 3V3). Durante il
    reset dell'ESP32 i traslatori sono spenti e i pull-down interni danno LOW: VDD_SPI 3,3 V
    e download boot valido anche con l'Atari acceso. Firmware: `setup()` porta GPIO 47 basso
    dopo la configurazione degli ingressi; compilati entrambi gli ambienti. Resta libero solo
    GPIO 48.
- **Documentazione aggiornata**: `PIN-MAPPING.md`, `VERA-ATARI-HW-REQUIREMENTS.md` (§2.7, §2.8,
  nuova §2.9), `firmware/MCU/README.md`, `firmware/MCU/Vera_Module_Analisys.ITA.tex` + PDF,
  tesi (nuova sezione 3.4, tabella dei pin) + PDF.
- **Allineamento del gating (stesso giorno)**: requisiti §2.1 (piedinatura di U24/U25), `PIN-MAPPING.md`
  (nota su `~OE` di U19, ruolo di U23, lati A/B dei 74LVC4245 che erano invertiti: VCCA = 5 V Atari,
  VCCB = 3,3 V), tesi (sezione 3.2 e figura 3.2 ridisegnate, tabella dei ritardi, conclusioni),
  `timing_model.py` (4 porte LVC00 dai controlli, 1 da φ2), documento di analisi. Effetto sul modello:
  dato ESP32 invariato; letture VERA −5 ns; opzione "uscite sempre abilitate" 13,4 → 13,1 cicli.
- **Verifica dello schema**: script che ricava la connettività dai `.sch` (KiCad 5, librerie dalla
  cache) e la confronta con la netlist: 239 reti, nessuna differenza. `kicad-cli` (KiCad 8+) non
  legge i simboli del formato KiCad 5.
- **U16 (requisiti §2.10), corretto nello schema**: pilotava `~HALT`, `RD4`, `RD5` sul bus Atari da ingressi
  collegati solo a test point. Ora uscite 3, 4, 5 non collegate, ingressi 19, 20, 21 a GND; i test point
  (`~HALT1`, `RD5`, `RD4`) sono sulle reti lato Atari. Netlist aggiornata e confrontata con lo schema
  (0 differenze).
- **5 V senza diodo (requisiti §2.11), regola d'uso**: niente diodo; programmazione, USB e jack solo con la
  scheda scollegata dall'Atari (anche per il monitor seriale, salvo adattatore senza VBUS). Scritto in
  requisiti, `PIN-MAPPING.md` §4, `firmware/MCU/README.md`, `README.md`.
- **IRQ (requisiti §2.3), corretto nello schema**: il diodo `D1` (BAT43W) era al contrario (catodo su `~IRQ`).
  Sostituito da `U26` SN74LVC1G07DBVR (LCSC C7829, confermato dall'utente: >50k in stock il 2026-10-26)
  con `C69` 100 nF. Simbolo aggiunto alla RetroBitLab-Library (non committato) e alla cache del progetto.
  Netlist aggiornata e confrontata con lo schema (0 differenze).
- **Aperti**: il 5 V della scheda arriva su CART1.13/ECI1.7 senza diodo (la scheda alimentata
  da USB/jack alimenta l'Atari spento?); `PIN-MAPPING.md` dice RD4/RD5/HALT su test point, ma
  nella netlist i canali di riserva di U16 non sono collegati; glitch di uscita all'accensione
  su GPIO 1, 2, 4–11 contro U6/U19 (U6 resta sempre abilitato).

## 9. Sessione 2026-10-29: A10–A13 rimappati per il routing

- L'utente ha scambiato nello schema (`busdecoder.sch`, commit `dc3c933`) le etichette su U7
  (ESP32-S3) per semplificare il routing. Lato U3 invariato. Nuova assegnazione, ora in ordine
  naturale: **A10 = GPIO 33 (pin 38), A11 = GPIO 34 (pin 39), A12 = GPIO 35 (pin 40),
  A13 = GPIO 36 (pin 41)** (prima A10/A11 = GPIO 35/36, A12/A13 = GPIO 33/34). Netlist e
  `.kicad_pcb` già coerenti (pad 38–41 di U7 = mA10–mA13).
- Firmware: cambiano solo i `PIN_A10`…`PIN_A13` in `main.cpp`. Il loop legge A10–A15 dai canali
  Dedicated GPIO assegnati per nome (`dedic_in_pins[]`), quindi `bus_decode.h` e la maschera
  `0xFC` non cambiano. Build `esp32s3fn8` e `esp32s3fn8-cctl` OK, test host 0 errori.
- Documenti aggiornati: `PIN-MAPPING.md`, `firmware/MCU/README.md`, documento di analisi (.tex + PDF).
  La tesi riportava già "33–36" per A10–A13 senza ordine: nessuna modifica.
