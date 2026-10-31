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

## 10. Sessione 2026-10-30/31: PCB sbrogliato con fastroute

Tutto committato e pushato fino a `7ec9431` (resta solo questa nota). Nessuna scheda costruita.

- **Meandri tolti** dai bus `mA*`/`mD*` con `tools/unmeander.py` (`96fa4ad`). Il length matching
  non serve: bus 6502 a 1,79 MHz, 60 mm di differenza ≈ 0,4 ns su centinaia di ns di ciclo.
- **Routing rifatto da zero** con fastroute (`~/packages/fastroute/target/release/fastroute`),
  commit `500c89e`:
  - layer interni **GND e POWER (3V3) piani pieni**, senza piste. La prima prova con POWER "misto"
    lasciava 146 tratti di segnale sul piano 3V3 e lo spezzava in isole (39 non connessi al DRC);
  - ogni pad SMD GND/3V3 ha il suo via verso il piano (235 via con moncherino), messi da script
    prima del routing e fissati nel DSN;
  - due fasi: prima solo `AddressBus`/`DataBus`, poi il resto partendo da quel cablaggio;
  - risultato: 626/634 collegamenti, 0 violazioni; gli 8 mancanti (mA0 ×2, mA4, ~mWE, SPI_SSEL_N,
    VSYSCLK, D−, 3V3 U7.46–U7.55) erano da tracciare a mano in pcbnew: **verificare con il DRC**
    che siano stati fatti;
  - bus: mD0–mD7 76–90 mm, mA1–mA4 69–83 mm, mA5–mA15 15–31 mm.
- **Lezioni su fastroute 0.1.14** (per un prossimo routing):
  - via e piste con `(type fix)` non finiscono nel `.ses`: usare `(type protect)`;
  - una sessione importata con `--initial-session` **non** resta fissa: se il DSN contiene già gli
    stessi via, vengono caricati due volte (centinaia di "violazioni" via/via sulla stessa net); se
    non li contiene, il router li può rimuovere. Soluzione usata: via fissi nel DSN e tolti dalla
    sessione;
  - il length tuning (`--tune`) ha sovrapposto piste: non usarlo;
  - il router lascia pezzetti di pista < 0,2 mm all'ingresso dei pad nonostante
    `--router.min_trace_width_um=200`: controllare il `.ses` prima dell'import;
  - gli script sono in `tools/` (`7652964`): `tools/route.sh WORKDIR` rifà tutto il routing dal
    DSN esportato (preparazione, via ai piani, bus, resto, dedup, controllo piste sottili);
    `thermal_vias.py` per i via sotto le alette di U13/U15.
- **Footprint** (RetroBitLab-Library `aa15c43`, pushato):
  - `IC_ESP32-S3FN8-QFN40P700X700X90-57N`: 9 via termici 0,3/0,2 mm nell'EP (pad 57). U7 va
    aggiornato con "Update Footprint" (il nome non cambia, "Update PCB from Schematic" non lo fa);
  - nuovo `SSOP-20_5.3x7.2mm_P0.65mm_Pad0.45` per U12 (CH340T): pad 0,45 mm, 0,20 mm tra i pad
    invece di esattamente la clearance di 0,15 mm. Simbolo `USBCH340T` e `powersupply.sch` aggiornati.
- **Via termici sul PCB** (`6f2a52b`): 16 sotto l'aletta GND di U15 (L7805), 6 sotto l'aletta 3V3 di
  U13 (AMS1117). Via liberi, footprint KiCad standard non modificati (scelta dell'utente).
- **Uscite v4** (`./output_job.sh 4`, `d69c2d4`): BOM/CPL JLCPCB, Gerber, STEP/WRL, zip
  `VERA-MODULE-RBL-XE-v4.zip`. Versione "4" come la documentazione (il cartiglio dice ancora Rev 1.0).
  I CSV `-v4` e lo zip non sono tracciati, come le versioni precedenti.
- **eeplot** (`7ec9431`): con cairo 1.18 (Debian 13) l'indice del PDF dello schema andava in
  asserzione (`pdftoc_end`). Nuova `patches/eeplot-pdf14-toc.patch`: il PDF con indice è scritto
  in PDF 1.4. `output_job.sh` ricompila eeplot da solo quando cambiano le patch.
- **Aperti**:
  - DRC: courtyard sovrapposti **H3/J1** (F.Cu, ~1 mm) e **C23/R71** (B.Cu): da verificare se
    corretti dopo il routing;
  - cartiglio di schema e PCB ancora "Rev 1.0" contro "Rev 4" della documentazione;
  - `images/VERA-MODULE-RBL.step` cambia a ogni esecuzione di `output_job.sh` anche senza modifiche
    alla scheda: scartarlo se la scheda non è cambiata;
  - file di lavoro non tracciati nella radice: `VERA-MODULE-RBL.dsn`, `.ses`, `.kicad_pcb.bak`,
    `report.drc.rpt`, `bom/`.

## 11. Sessione 2026-10-31 (mattina): `output_job.sh` genera tutto dai sorgenti

**Correzione al §10:** le uscite v4 di `d69c2d4` non erano buone. La BOM JLCPCB veniva da un
`VERA-MODULE-RBL.xml` del 2025-11-05 (export manuale da Eeschema mai rifatto): mancavano
U19, U23–U26, Y2, SW3, L2, R69–R77, C61–C69, c'erano ancora U1 e SW2, e i codici erano vecchi
(U7 ESP32-PICO-D4 invece di ESP32-S3FN8, U3/U4/U6/U16 TXS0108 invece di 74LVC4245, U5 CD4011B
invece di 74HC00, molti valori R/C). JLCPCB lo ha segnalato al caricamento del CPL. Inoltre i
Gerber **non** li generava lo script: lo zip prendeva quelli esportati a mano in `production/`.

Ora `./output_job.sh 4` fa tutto senza passaggi manuali e si ferma al primo errore:

- `sch2bomxml.py`: rigenera ogni volta `VERA-MODULE-RBL.xml` leggendo i `.sch` KiCad 5 dalla
  gerarchia del foglio radice (`addressdecoder.sch` non è usato e viene ignorato), riferimenti da
  `AR Path`. Errore se un componente non è annotato o se un riferimento è usato da due parti
  diverse. `kicad-cli sch export python-bom` (KiCad 10) su un `.sch` KiCad 5 scrive un XML senza
  componenti: non usabile.
- `jlcpcb-sync-cpl.py`: allinea il CPL alla BOM. Toglie le righe senza BOM (oggi JP2 senza LCSC e
  R49 DNP) e si ferma se un componente della BOM non è nel CPL (non piazzato sul PCB).
- `generate_gerbers.py` (via `run_pcbnew.sh`, pcbnew KiCad 5 di sistema o AppImage `kicad5`):
  Gerber, job file, Excellon (pollici, decimale, PTH/NPTH separati), mappe PostScript e report,
  con le impostazioni di stampa salvate nel `.kicad_pcb`. `production/` viene svuotata prima
  (resta `.gitignore`). Zone usate come salvate; `REFILL_ZONES=1 ./output_job.sh 4` le riempie
  di nuovo prima della stampa (il `.kicad_pcb` non viene scritto). Non si usa `kicad-cli` per i
  Gerber: non supporta più la strategia di riempimento zone di KiCad 5.
  - nota tecnica: `PLOT_CONTROLLER` di KiCad 5 parte dalle opzioni di default, non da quelle della
    scheda: lo script le copia (coppie Get/Set). Con le estensioni Protel attive `OpenPlotfile`
    dà già `.gtl`, `.g2`, ecc.
  - verificato contro l'export manuale delle 03:15: rame, piani, maschere e forature identici a
    parte le date; serigrafie, paste, Edge.Cuts e job file con lo stesso contenuto in ordine diverso.
- Uscite v4 rigenerate: BOM 65 righe e CPL 191 componenti con gli stessi designator; zip
  `VERA-MODULE-RBL-XE-v4.zip` (17 file). CSV `-v4` e zip restano non tracciati.
- **Silkscreen bottom nel visualizzatore JLCPCB:** nella vista "Bottom" il testo si legge
  rovesciato. È corretto: quel visualizzatore mostra il lato inferiore visto da sopra, senza girare
  la scheda (ECI1 resta a sinistra in entrambe le viste). Il Gerber non contiene specchiature.
- **Da fare prima dell'ordine:** nell'anteprima JLCPCB controllare la rotazione dei componenti
  nuovi (U19, Y2, U7 soprattutto).

### 11b. Controllo stock JLCPCB prima di `output_job.sh`

- `./check_jlcpcb_stock.sh [-n SCHEDE]` (default 5): BOM dagli schematici in una cartella
  temporanea, poi `jlcpcb-stock-check.py` interroga in tempo reale la ricerca parti di jlcpcb.com
  (servizio non documentato, nessun account). Per ogni riga: stock, quantità con scarto e minimo,
  Basic/Preferred/Extended, costo; per le parti OUT/SHORT/MISSING propone alternative (stessa
  categoria e package, stesso valore per i passivi, Basic prima). Esce con 1 se qualcosa non si
  può montare: va cambiato nello schema prima di `output_job.sh`.
- Il database **jlcparts** (yaqwsx) è stato provato e scartato: mancano parti che JLCPCB ha
  (C2913196 ESP32-S3FN8, C2798196 e altre 10), quindi dava falsi "missing".
- Cambiati (con nuovi simboli in RetroBitLab-Library, CCAP0805/CRES0805):
  - C36 → `CCAP0805:2.2uF-X5R` C377773 (Basic; prima C7393943, 9 pezzi);
  - R37–R44 → `CRES0805:47K-125mW` C17713 (Basic; prima C20142780, 82 pezzi).
  Valori e footprint invariati: il PCB non cambia.
- **Ancora da risolvere** (controllo del 2026-10-31 per 5 schede):
  - **J7** jack audio SJ-3523-SMT-TR C4991872: esaurito, serve un altro jack (cambia il footprint);
  - **U11** WM8524CGEDT C146242: 3 pezzi per 5 schede, nessun equivalente pin a pin
    (Global Sourcing o 3 schede);
  - X1 ECS-2033-250-BN C2441279: 6 pezzi, ne servono 5 (avviso).
