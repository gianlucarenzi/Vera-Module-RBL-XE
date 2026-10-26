# VERA RBL-XE — Requisiti e verifiche hardware (dal lavoro su driver ed emulatore)

Documento di passaggio dal progetto software (`VERA_ATARI_PBI`: handler PBI, driver
`VERA.SYS`, test, emulatore `atari800`) al progetto della scheda reale.
Data: 2026-10-06.

## 0. Fonti e limiti

| Fonte | Cosa ho usato |
|---|---|
| HDL VERA 47.0.2 (`VERA_ATARI_PBI/fpga-vera/47.0.2/.../fpga/source/top.v`) | protocollo del bus, reset, IRQ, mappa registri |
| `VERA_ATARI_PBI` (`vera_pbi_handler.s`, `vera_common.inc`, `vera-tests/`) | cosa il software si aspetta dall'hardware |
| `atari800/src/pbi_verax16.c`, `pbi.c` | modello di riferimento (ora allineato all'HDL) |
| Questo repo: `PIN-MAPPING.md`, `firmware/MCU/README.md`, netlist `VERA-MODULE-RBL.net` (2026-09-01) | stato della scheda |

**Limiti.** I punti della sezione 2 sono ricavati dalla *netlist*: nessuna simulazione elettrica
e nessuna misura. La netlist è del 2026-09-01 e l'ultimo commit del repo ("deleted all tracks and
imported from schematics") può averla resa non allineata. Ogni punto marcato **VERIFICA** va
controllato sullo schema aggiornato, sui datasheet e con l'oscilloscopio.

Il software è stato provato solo in emulazione. La scheda reale non è mai stata vista dal
driver.

---

## 1. Cosa fa la VERA sul bus (dall'HDL 48.0.1)

Firmware di riferimento: **48.0.1** (`VERA_ATARI_PBI/fpga-vera/48.0.1`). Rispetto alla 47.0.2 l'HDL
cambia solo in tre punti: l'indirizzo di scrittura è catturato direttamente all'inizio di WR (prima
passava da un registro intermedio), i byte di versione restituiscono anche minor e build, e le onde
sawtooth/triangle del PSG sono in XOR con `~pulsewidth`. I requisiti per la scheda non cambiano.

Questi sono i vincoli che l'hardware deve rispettare, indipendentemente dal progetto.

| Aspetto | Comportamento reale (`top.v`) | Conseguenza per la scheda |
|---|---|---|
| Strobe | `bus_read = !cs_n && wr_n && !rd_n`, `bus_write = !cs_n && !wr_n` | servono RD_n e WR_n **distinti**; `rd_n` è ignorato se `wr_n` è basso |
| Scrittura | l'indirizzo è campionato sul fronte di **salita** di `bus_write` (inizio); il **dato** sul fronte di **discesa** (`negedge bus_write`, cioè fine scrittura) | il dato deve restare stabile fino **dopo** la salita di WR_n / CS_n (hold) |
| Lettura | `extbus_d` pilotato solo mentre `bus_read`; effetti collaterali (autoincremento, prefetch) sul fronte di discesa di `bus_read`, dopo 3 cicli di `clk25` (~120 ns) | un'attivazione di RD_n = un evento di lettura. Cicli doppi (RMW, dummy read) li duplicano |
| Reset | solo POR interno (`por_cnt_r`, 128 cicli di `clk25`); **nessun pin di reset** | il RESET Atari non resetta la VERA |
| CTRL bit 7 (`$D105`) | `fpga_reconfigure` → ricarica l'intero bitstream: VRAM persa, bus morto ~100 ms | scrittura distruttiva; vedi §3 |
| IRQ | `extbus_irq_n = (irq_status & irq_enable) == 0`: **push-pull**, a livello | non può pilotare direttamente una linea IRQ condivisa |
| Registri FX | in sola scrittura (la lettura di `$D109-$D10C` con DCSEL≥2 restituisce i byte di identità del firmware, `'V',48,0,1` sulla 48.0.1, tranne FX_CTRL e POLY_FILL_L/H) | niente read-back di FX |

---

## 2. Verifiche e modifiche sulla scheda attuale

Priorità: **A** = da risolvere prima del primo test sul bus, **B** = da risolvere prima del
rilascio, **C** = da decidere.

### 2.1 [A] Il bus dati può fare contesa su letture non destinate alla scheda — **corretto nello schema (2026-10-26)**

Prima della modifica (netlist 2026-10-23):
- `U19` (74LVC4245, bus dati): `~OE_` (pin 22) è collegato **solo** a `NOT(PHI2)` (`U23` pin 3→4);
  `DIR` (pin 2) è `NOT(R/W)` (`U23` pin 1→2).
- Quindi `U19` è abilitato in **ogni** ciclo con PHI2 alto, qualunque sia l'indirizzo.

In un ciclo di lettura (R/W = 1) `DIR` = 0, cioè verso l'Atari (nella netlist `VCCA` è a 5 V,
quindi il lato A di `U19` è quello Atari e il lato B quello 3,3 V della scheda). In un ciclo di
lettura di **altra** memoria (RAM, ROM OS, cartuccia) `U19` pilota comunque il bus dati
dell'Atari dal lato scheda, dove i GPIO dell'ESP32 e le uscite dell'FPGA sono Hi-Z e non
pilotati. Un 74LVC4245 non ha bus-hold, quindi la contesa con RAM/ROM sul bus è probabile,
con possibili errori di lettura in tutto il sistema.

Richiesta: `~OE_` di `U19` deve essere attivo **solo** quando la scheda risponde in lettura
(registri VERA, RAM `$D600-$D7FF`, ROM `$D800-$DFFF`, finestra RAMbo), oppure in scrittura
(Atari→scheda, dove `U19` pilota solo il lato scheda). **Non** va collegato a `CDONE`: quello
segnala solo la configurazione dell'FPGA ed è già usato per rilasciare `ARESET`. Il gating è
ciclo per ciclo.

I segnali che indicano "la scheda risponde" esistono già: il firmware abbassa `DEV_SEL_N` (registri
VERA), `EXTSEL_N` (RAM `$D6xx`, RAMbo) e `MPD` (ROM `$D800`) quando la scheda risponde, e li rialza a
fine ciclo. Quindi:

```
~OE_ = NOT( PHI2 AND ( NOT R/W   OR   ~DEV_SEL_N OR ~EXTSEL_N OR ~MPD ) )
```

**Realizzazione (schema e netlist 2026-10-26): due `SN74LVC00A`** (`U24`, `U25`, quattro NAND a 2
ingressi ciascuno, TSSOP-14, LCSC C7803), alimentati a 3V3. Il 74LVC20 (due NAND a 4 ingressi), che
avrebbe fatto lo stesso con due porte, non si trova; la NAND a 4 ingressi si scompone in NAND a 2:

```
n1  = NAND(mR~W,    ~mVCS0)      U24 pin 1, 2  → 3
n2  = NAND(~mEXSEL, ~mMPD)       U24 pin 4, 5  → 6
a1  = NAND(n1, n1)               U24 pin 9, 10 → 8     (= AND)
a2  = NAND(n2, n2)               U24 pin 12, 13 → 11   (= AND)
B   = NAND(a1, a2)               U25 pin 1, 2  → 3     (= NAND4: 1 se scrittura o la scheda risponde)
~OE = NAND(mPHI2, B)             U25 pin 4, 5  → 6     → U19 pin 22
```

Segnali presi dal lato scheda a 3,3 V (`~mVCS0` esiste solo lì):

| Pin | U24 | U25 |
|---|---|---|
| 1 | `mR~W` (U6.18 / U7.7 / U5.2) | a1 (U24.8) |
| 2 | `~mVCS0` (U7.45 / U2.28) | a2 (U24.11) |
| 3 | n1 → U24.9, U24.10 | B → U25.5 |
| 4 | `~mEXSEL` (U7.47 / U16.18) | `mPHI2` (U6.17 / U7.6 / U5.1) |
| 5 | `~mMPD` (U7.48 / U16.17) | B (U25.3) |
| 6 | n2 → U24.12, U24.13 | **`~OE` → U19 pin 22** |
| 7 | GND | GND |
| 8 | a1 → U25.1 | libero |
| 9, 10 | n1 | GND (porta libera) |
| 11 | a2 → U25.2 | libero |
| 12, 13 | n2 | GND (porta libera) |
| 14 | 3V3, 100 nF `C67` | 3V3, 100 nF `C68` |

- `U23` pin 4 non pilota più U19 pin 22; l'unità 3→4 di `U23` non è usata e il suo ingresso va a GND
  (§2.8).
- **pull-up 10 kΩ verso 3V3 su `~mVCS0`, `~mEXSEL` e `~mMPD`** (`R36`, `R53`, `R65`): durante reset e
  boot dell'ESP32 i GPIO 40–42 (pin JTAG) non hanno un livello garantito. Senza pull-up un livello
  basso spurio accenderebbe `U19` nelle letture e, tramite `U16`, abbasserebbe `EXTSEL`/`MPD` sul bus
  Atari.

| mPHI2 | mR~W | ~mVCS0 / ~mEXSEL / ~mMPD | B | ~OE | U19 |
|---|---|---|---|---|---|
| 0 | x | x | x | 1 | spento (φ1) |
| 1 | 0 (scrittura) | x | 1 | 0 | acceso, Atari → scheda |
| 1 | 1 (lettura) | tutti a 1 | 0 | 1 | spento: niente contesa |
| 1 | 1 (lettura) | almeno uno a 0 | 1 | 0 | acceso, scheda → Atari |

`~OE` di `U19` è riferito a VCCA (5 V) con soglia TTL (VIH ≥ 2,0 V): l'uscita LVC a 3,3 V lo pilota
direttamente. Ritardo: dai segnali di controllo a `~OE` quattro porte LVC (≤ 4,5 ns ciascuna a 3,3 V),
da `mPHI2` a `~OE` una. Con il modello della tesi (`timing_model.py`) il dato servito dall'ESP32 arriva
comunque almeno ~80 ns dopo l'accensione di U19: il margine nel caso peggiore non cambia rispetto al
74LVC20 (NTSC +27/+2 ns a 12/14 cicli APB); le letture VERA perdono ~5 ns (+136/+128 ns). **Non usare
74HC00**: a 3,3 V il datasheet non dà tempi e l'estrapolazione arriva a ~35–40 ns per porta, proprio dove
quattro livelli entrano nel percorso critico (~37–39 ns). `DIR` resta invariato (`U23` pin 1→2).

Da controllare:
- **Tempi.** Con il firmware attuale (decodifica in φ1) `DEV_SEL_N`, `EXTSEL_N` e `MPD` arrivano
  intorno alla salita di PHI2 (0…+40 ns nel caso peggiore); `~OE_` li segue di ≤ 18 ns (quattro porte)
  più il tempo di abilitazione del 4245 (≤ ~9 ns). Il dato dell'ESP32 arriva comunque dopo: da
  misurare con l'oscilloscopio.
- **Uscite dati sempre abilitate: valutato e scartato.** Con `U19` isolato l'ESP32 potrebbe tenere
  abilitate le uscite dati in φ1 (una scrittura APB in meno nelle letture), ma nei cicli di scrittura
  dovrebbe spegnerle prima che `U19` piloti il lato scheda (una sola porta dopo la salita di PHI2):
  questo vincolo si rompe a ~13 cicli APB, quindi il caso peggiore non migliora (tesi, cap. 8).
- **`$D1FF` in lettura non abilita `U19`** (nessun segnale di risposta è asserito): è voluto, vedere §2.4.
- **Scritture.** `U19` resta abilitato in ogni scrittura: l'ESP32 deve vedere tutte le scritture
  (`$D301`, `$D303`, latch `$D1FF`, finestra RAMbo).

### 2.2 [A] Hold del dato sul fronte di salita di WR_n — VERIFICA

- `~mWE` e `~mRE` sono generati da `U5` (74HC00): `~mRE = NAND(PHI2, R/W)`,
  `~mWE = NAND(PHI2, NOT R/W)` (la negazione è un'altra porta NAND di `U5`).
- La VERA campiona il dato in scrittura alla **salita di WR_n**, cioè circa un ritardo di
  porta dopo la discesa di PHI2.
- Lo stesso evento (discesa di PHI2) disabilita `U19` tramite `U23` (inverter HCT04) e il
  buffer: il dato verso la VERA può diventare non valido più o meno nello stesso istante in cui
  WR_n sale. In più il 6502C mantiene il dato per un tempo minimo dopo PHI2 (verificare il valore sul
  datasheet SALLY/6502C).

Richieste:
1. Usare una porta più veloce di 74HC00 a 3,3 V (per esempio 74LVC00 / 74AHC00).
2. Rendere il rilascio di `U19` **più lento** della salita di WR_n (per esempio ritardare `~OE_`),
   oppure usare un solo percorso di ritardo per strobe e dato, così che il margine di hold sia
   positivo.
3. Verificare con analizzatore logico/oscilloscopio: dato stabile ≥ 5 ns dopo la salita di
   `~mWE` e `~mVCS0` al pin FPGA.

### 2.3 [A] Linea IRQ verso il bus Atari (open-collector) — **corretto nello schema (2026-10-26)**

- L'uscita VERA è push-pull (§1). Il bus Atari ha una linea IRQ **open-collector** condivisa
  con POKEY, PIA e altri device: un'uscita push-pull in alto contro un altro device che tira
  in basso crea contesa.

Richiesta: tra FPGA e ECI usare uno stadio open-drain/open-collector con uscita tollerante a
5 V. Una linea IRQ open-collector è un OR cablato: ogni device può solo **tirarla in basso**,
mai in alto; il livello alto lo dà l'unico pull-up sul computer. Aggiungere un proprio stadio
open-drain dà esattamente questo comportamento con POKEY, PIA e gli altri device.

**Realizzazione (schema e netlist 2026-10-26): `U26` SN74LVC1G07DBVR** (TI, buffer non invertente
open-drain, SOT-23-5, LCSC C7829), alimentato a 3,3 V:

```
 3V3 ──┬── VCC (pin 5)              C69 100 nF (C840116) fra 3V3 e GND, vicino al chip
 ~mVIRQ (FPGA U2 pin 32, 3,3 V, attivo basso) ──► A (pin 2)      GND (pin 3), pin 1 NC
 Y (pin 4, open-drain) ──► ~IRQ, ECI1 pin B (5 V)   [nessun pull-up proprio verso 5 V]
```

- Uscita bassa quando l'FPGA chiede IRQ, alta impedenza altrimenti: non invertente, quindi la
  polarità resta quella di `~mVIRQ` (basso = IRQ).
- L'uscita open-drain tollera 5,5 V anche a chip spento (Ioff), quindi il pull-up a 5 V del computer
  non la danneggia.
- VOL < 0,4 V a qualche mA: la corrente del pull-up IRQ Atari (circa 1–1,5 mA con 3,3–4,7 kΩ,
  **verificare sul computer di prova**) è ampiamente entro i limiti. Ritardo ≤ 4,2 ns a 3,3 V.
- Il simbolo `SN74LVC1G07DBVR` è stato aggiunto alla RetroBitLab-Library (`RetroBitLab.lib`/`.dcm`),
  con i pin di alimentazione visibili (un pin VCC nascosto si collegherebbe da solo a una rete `VCC`).

**Storia.** Nella netlist 2026-10-23 `~mVIRQ` andava a `U16` pin 18 → `~IRQ` (`U16` pin 6) in push-pull.
Nella revisione successiva era stato sostituito dal diodo Schottky `D1` (BAT43W), montato però al
contrario (catodo su `~IRQ`, anodo sull'FPGA): l'IRQ della VERA non arrivava all'Atari e l'uscita alta
dell'FPGA caricava la linea quando POKEY o la PIA la tiravano bassa. Anche con il verso giusto il livello
basso (VOL dell'FPGA + Vf) sarebbe stato vicino al limite TTL di 0,8 V. `D1` è stato tolto.

**Gestione dell'IRQ.** Con lo stadio open-drain basta un gancio software sul vettore IRQ (vedere §2.4):
non serve nessun collegamento aggiuntivo all'ESP32.

**Cosa NON fare.** Non lasciare un'uscita push-pull verso il bus, non aggiungere un pull-up a 5 V sulla
scheda (la linea ha già il suo) e non usare un 74LVC4245 per questo segnale: non ha uscite open-drain.

### 2.4 [A] Identificazione dell'IRQ: via software, non tramite `$D1FF`

**Perché non tramite `$D1FF`.** Il PBI prevede che ogni device pilota il proprio bit (la VERA: D7) in
lettura di `$D1FF`, con gli altri bit Hi-Z, e che l'OS legga `$D1FF & PDIMSK` per scegliere la ROM da
chiamare. Con la scheda attuale non è realizzabile:
- `U19` è un 74LVC4245: abilita **tutti e otto** i canali o nessuno. Pilotare solo D7 dal lato ESP32
  lascerebbe D0-D6 del lato scheda non pilotati, e `U19` li porterebbe sul bus Atari a un livello
  casuale, in contesa con i bit di eventuali altri device PBI. Con la regola di §2.1 `U19` non viene
  nemmeno abilitato per `$D1FF`.
- `~mVIRQ` non arriva all'ESP32. L'unico GPIO libero è GPIO 48 (`PIN-MAPPING.md` §7; GPIO 47 è
  `ABUS_OE_N`, §2.9); anche portando lì `~mVIRQ`, servirebbe comunque un buffer tri-state separato per pilotare solo D7. Per questo il filo `~mVIRQ`→GPIO0 e l'opzione firmware `VERA_HAS_VIRQ_SENSE` sono
  stati **eliminati** (il documento `HW-MOD-GPIO0-VIRQ.md` non esiste più).

**Soluzione adottata: gancio software sul vettore IRQ.** I registri VERA sono sempre leggibili
(§2.5b). Un programma che usa gli IRQ della VERA aggancia `VIMIRQ` (`$0216`): legge `ISR` e `IEN`,
se `ISR & IEN` ha bit attivi conferma VSYNC/LINE/SPRCOL scrivendo 1 in `ISR` e maschera AFLOW in
`IEN`, altrimenti passa il controllo al gestore precedente. Serve solo lo stadio open-drain di §2.3 sulla
linea IRQ. L'OS **non** viene più usato per smistare l'IRQ: `PDIMSK` (`$0249`) resta a 0 e
`IRQVECTOR` della ROM PBI (`$D808`) non verrà chiamato.

Stato: **scritto e provato in emulatore**. Il gancio è in `VERA_ATARI_PBI/vera-tests/vera_irq.s`
(interfaccia C in `vera_irq.h`: `vera_irq_install`, `vera_irq_enable`, `vera_irq_disable`,
`vera_irq_take`, `vera_irq_set_line`, `vera_irq_set_callback`, `vera_irq_remove`). Il test
`TESTIRQ.COM` verifica VSYNC (circa 59,94 Hz anche con Atari PAL), LINE, il mascheramento di AFLOW,
la catena verso l'OS e la rimozione: 13 PASS, 0 FAIL in NTSC e in PAL. Sulla scheda reale va
provato dopo il montaggio dello stadio open-drain. Guida d'uso: `VERA_ATARI_PBI/Documentation/VERA-IRQ.md`.

### 2.5 [B] Scrittura `$D1FF`: solo il proprio bit — **corretto nel firmware**

Il latch di selezione deve usare solo **D7** (bit del device): con D7 = 1 la scheda si seleziona,
con D7 = 0 si deseleziona, qualunque sia il valore degli altri bit. L'emulatore si comporta
così (`byte & mask`) e ora anche `main.cpp` (`(data & PBI_DEV_ID) != 0`, prima `== 0x80`), sia in
modalità PBI sia CCTL.

### 2.5b [A] I registri VERA devono rispondere anche con il latch a 0 — **corretto nel firmware**

Il firmware asseriva `DEV_SEL_N` per `$D100-$D11F` **solo con la scheda selezionata** (latch
`$D1FF` = `0x80`). Ma l'OS deseleziona la scheda (`$D1FF` = 0) subito dopo l'`INIT`, e il driver
non la riseleziona mai: selezionarla mappa la ROM e disattiva il Math Pack (`MPD`), cosa che rompe
la virgola mobile del BASIC. Risultato: dopo il boot i registri VERA non sarebbero più
raggiungibili (il banner di boot compare perché l'`INIT` gira a scheda selezionata).

Ora `DEV_SEL_N` ed `EXTSEL_N` vengono asseriti per `$D100-$D11F` **sempre** (come
nell'emulatore e come presuppone il software); `MPD` e `EXTSEL_N` per `$D600-$DFFF` restano
legati alla selezione.

### 2.5c [A] Il log dei registri nel loop del bus faceva perdere cicli — **corretto nel firmware**

Per ogni accesso ai registri VERA il loop chiamava `log_send()` (`xQueueSend` + `esp_timer_get_time`,
circa 1 µs) mentre `DEV_SEL_N` era ancora asserito. Con un ciclo 6502 di 558 ns si poteva
superare la fine di PHI2: `DEV_SEL_N` restava basso nel ciclo successivo (accessi spuri alla
VERA con l'indirizzo sbagliato) oppure si perdeva il ciclo seguente. Ora il log dei registri è
compilato solo con `-DVERA_TRACE_REGS=1` (debug, mai in produzione); resta attivo il log dei
cambi di latch (rari).

Aggiornamento: anche il log dei cambi di latch costava circa 1 µs (`xQueueSend` +
`esp_timer_get_time`) proprio nel ciclo dopo la scrittura su `$D1FF`, a cui seguono i fetch
dalla ROM PBI. Ora il hot loop gira sul Core 0 e passa gli eventi al Core 1 (che stampa su
`Serial`) con un ring buffer senza lock: il push costa poche istruzioni.

### 2.6 [B] Header ROM PBI — **ROM allineata**

La ROM `vera_pbi_handler.rom` (2 KB, assemblata da `vera_pbi_handler.s`) ha l'header:
`$D803 = $80`, `$D80B = $91`, `$D80C = 'V'`, vettori `$D805` (SIO, restituisce C=0 = non
gestito) e `$D808` (IRQ). Il contenuto deve essere esattamente quello servito dalla scheda a
`$D800-$DFFF`. La copia in `firmware/MCU/6502/src/vera_pbi_handler.s` (+ `inc/vera_common.inc`) è stata
sostituita con l'handler corrente di `VERA_ATARI_PBI` e rigenerata con `make -C 6502`
(`vera_pbi_handler.bin`, `include/vera_pbi_handler.h`, `FPGA/pbi_rom_pkg.vhd`): il binario è identico
a `vera_pbi_handler.rom` di `VERA_ATARI_PBI`. Contiene il nuovo `IRQVECTOR` e l'attesa lunga di
`WAIT_VERA`.

### 2.7 [A] VCCB (pin 23) dei 74LVC4245 scollegato — **corretto nello schema (2026-10-26)**

Nello schema il pin 23 di ogni chip è ora collegato al pin 24 (3V3).

Nella netlist 2026-10-23 il pin 23 di `U3`, `U4`, `U6`, `U16` e `U19` è su una rete senza altri
collegamenti; solo il pin 24 va a 3V3. Il simbolo KiCad lo chiama `VCCB` (power_in): è il secondo pin
di alimentazione del lato B. Collegare il pin 23 a 3V3 su tutti e cinque i chip, con il
disaccoppiamento vicino.

### 2.8 [B] Ingressi liberi flottanti — **corretto nello schema (2026-10-26)**

`U23` (SN74HCT04) usava due invertitori (pin 1→2, 3→4) e aveva gli ingressi degli altri quattro (pin 5,
9, 11, 13) scollegati: gli ingressi CMOS flottanti possono oscillare e assorbire corrente. Con il gating
di §2.1 anche l'invertitore 3→4 non serve più. Nello schema ora vanno a GND:
- `U23` pin 3, 5, 9, 11, 13 (le uscite 4, 6, 8, 10, 12 restano libere);
- gli ingressi dei canali di riserva dei 74LVC4245, **ciascuno con una propria resistenza da 0 Ω**
  (CREZ0805, LCSC C328378) verso GND, così ogni canale si può riusare togliendo una resistenza:
  `U6` pin 10 (A7, lato Atari) → `R74`; `U16` pin 14 → `R72`, 15 → `R75`, 19 → `R73`, 20 → `R76`,
  21 → `R77` (lato scheda). Le uscite corrispondenti (`U6` pin 14, `U16` pin 3, 4, 5, 9, 10) restano
  libere. Le masse di `U6` (pin 11–13) vanno direttamente a GND.

### 2.9 [A] Strapping dell'ESP32-S3 — **corretto nello schema e nel firmware (2026-10-26)**

L'ESP32-S3 campiona GPIO 0, 3, 45 e 46 a ogni reset del chip (accensione, CHIP_PU, programmatore,
brown-out); i livelli devono essere validi alla salita di CHIP_PU e per 3 ms dopo (datasheet v2.2, §3).

| Pin | Funzione | Problema | Correzione |
|---|---|---|---|
| GPIO 3 (pin 8) | sorgente JTAG | senza pull interne, era scollegato (il datasheet vieta l'alta impedenza) | `R71` 10 kΩ → 3V3 |
| GPIO 0 (pin 5) | modo di boot | CHIP_PU (`R31` 4,7 kΩ + `C25` 100 nF) e BOOT0 (`R33` 4,7 kΩ + `C61` 100 nF) avevano la stessa costante di tempo: GPIO 0 arrivava alla soglia insieme al rilascio del reset | `C25` → 1 µF (4,7 ms contro 0,47 ms) |
| GPIO 45 (pin 51) | VDD_SPI | è A14 attraverso `U3`, sempre abilitato (`~OE` a GND con `R7` 0 Ω). Il 6502 pilota il bus indirizzi anche in reset: A14 alto → VDD_SPI 1,8 V e la flash della FN8 non parte | `~OE` di `U3` sulla rete `~ABUS_OE` (sotto) |
| GPIO 46 (pin 52) | messaggi ROM / download | è A15: A15 alto con GPIO 0 basso è una combinazione non valida → programmazione via CH340 fallita a caso con l'Atari acceso | come GPIO 45 |

**Rete `~ABUS_OE`.** `~OE` (pin 22) di `U3` (A8–A15) e di `U4` (A0–A7) è pilotato da **GPIO 47**
(pin 37, prima libero), con pull-up `R7` e `R34` (prima 0 Ω a GND, ora 10 kΩ → 3V3, stessa parte
LCSC C84376 delle altre pull-up). GPIO 47 dopo il reset è un ingresso senza pull e senza glitch
(datasheet Table 2-1/2-2): durante il reset dell'ESP32 le pull-up spengono `U3`/`U4`, le uscite verso
l'ESP32 sono in alta impedenza e i pull-down interni (45 kΩ) danno GPIO 45 = GPIO 46 = LOW a ogni
reset, qualunque cosa faccia l'Atari. Il firmware (`PIN_ABUS_OE_N` in `main.cpp`) porta GPIO 47 basso
in `setup()`, molto dopo i 3 ms di hold e prima di avviare il loop del bus. `~OE` è riferito a VCCA
(5 V) con soglia TTL: 3,3 V lo tengono alto.

Spegnere anche `U4` non serve agli strapping, ma all'accensione GPIO 12–19 hanno glitch di uscita di
~60 µs (datasheet Table 2-2): con `U4` spento non c'è contesa con le sue uscite. Gli eFuse
`VDD_SPI_FORCE`/`VDD_SPI_TIEH` (irreversibili) non servono più.

Resta aperto, fuori da questo punto: anche GPIO 1, 2 (PHI2, R/W da `U6`) e GPIO 4–11 (dati, `U19` in
scrittura) hanno glitch di uscita all'accensione. `U6` resta sempre abilitato perché `mPHI2`/`mR~W`
servono al gating di §2.1 e agli strobe della VERA.

### 2.10 [A] U16 pilotava `~HALT`, `RD4` e `RD5` sul bus Atari da ingressi flottanti — **corretto nello schema (2026-10-26)**

`U16` (scheda → Atari, `DIR` = GND con `R66`, `~OE` = GND) è sempre abilitato. Nella netlist 2026-10-26
tre suoi canali avevano l'ingresso lato scheda collegato **solo a un test point** e l'uscita sul
connettore: `~mHALT` (pin 19) → `~HALT` (pin 5, `ECI1` pin C), `mRD4` (pin 20) → `RD4` (pin 4, `CART1`
pin A), `mRD5` (pin 21) → `RD5` (pin 3, `CART1` pin 14). Gli ingressi flottanti davano livelli casuali,
portati sul bus in push-pull: `HALT` è un'uscita di ANTIC verso il 6502 (contesa, CPU ferma con un
livello basso); `RD4`/`RD5` dicono all'MMU che c'è una cartuccia a `$8000`/`$A000`.

I tre segnali servivano solo come test point. Correzione:
- `U16` pin 3, 4, 5 (uscite) **non collegati**; pin 19, 20, 21 (ingressi) a GND con una 0 Ω ciascuno
  (`R73`, `R76`, `R77`), come gli altri canali di riserva (§2.8);
- i test point (`~HALT1`, `RD5`, `RD4`, prima `~mHALT1`, `mRD5`, `mRD4`) sono sulle reti lato Atari
  `~HALT`, `RD5`, `RD4`: misurano direttamente i segnali a 5 V, senza caricarli.

### 2.11 [A] Il 5 V della scheda è collegato al 5 V dell'Atari senza diodo — **regola d'uso (2026-10-26)**

La rete `5V` della scheda unisce tre sorgenti:
- `CART1` pin 13 e `ECI1` pin 7: il +5 V dell'Atari, **collegato direttamente**;
- `U18` (MAX40200, diodo ideale) da VBUS dell'USB (`J4`);
- `U17` (MAX40200) dal 7805 `U15` alimentato dal jack `J5` (5–24 V).

I due MAX40200 impediscono alla scheda di alimentare all'indietro l'USB e il 7805, ma niente impedisce
alla scheda di alimentare l'Atari: con l'Atari spento e l'USB (o il jack) collegato, l'USB o il 7805
alimenterebbero la scheda madre dell'Atari attraverso il connettore; con l'Atari acceso le sorgenti
lavorerebbero in parallelo.

Decisione: nessun diodo sul ramo dell'Atari. **Programmazione e USB solo a scheda scollegata dall'Atari.** La rete 5 V della scheda è unita al +5 V dell'Atari (`CART1` pin 13, `ECI1` pin 7) senza diodo: con il cavo USB (`J4`) o il jack (`J5`) collegati mentre la scheda è inserita, la scheda alimenterebbe l'Atari (spento) o lavorerebbe in parallelo al suo alimentatore (acceso). Vale anche per il monitor seriale: con la scheda nell'Atari usare un adattatore senza VBUS. Il jack e l'USB servono per programmare
l'ESP32 e la flash della VERA e per le prove a banco, con la scheda fuori dall'Atari.

---

## 3. Reset e configurazione FPGA

| # | Requisito | Stato nel firmware attuale |
|---|---|---|
| 1 | Tenere l'Atari in reset (`ARESET` basso) fino a CONFIG_DONE della VERA | **già così**: `ARESET`, `CRESET` bassi in `setup()`; `CRESET` alto, attesa `CDONE`, poi `ARESET` alto |
| 2 | Il blocco vale **solo al power-on** | la sequenza è eseguita una volta: **non** deve essere rieseguita se `CDONE` scende dopo una riconfigurazione (CTRL bit 7) |
| 3 | Ritardo dopo `CDONE` prima di rilasciare `ARESET` (~1 ms) | **fatto**: `delayMicroseconds(VERA_POST_CDONE_DELAY_US)`, default 1000 µs (la VERA ha un POR interno di 128 cicli di `clk25`, ~5 µs, più `reset_sync`) |
| 4 | `ARESET` open-drain con pull-up | il tasto RESET Atari deve continuare a funzionare; mai pilotare la linea in alto |
| 5 | Timeout se `CDONE` non arriva (oggi 5 s, solo log) | decidere: rilasciare comunque `ARESET`? Il software ha un timeout di ~0,7 s in `WAIT_VERA`: prosegue senza VERA ed emette **tre beep** dall'altoparlante della console (un colore sullo schermo ANTIC non resta: l'OS reimposta i colori dopo l'`INIT` PBI) |
| 6 | Scrittura di `$80` in CTRL (`$D105`) | **protezione consigliata**: far mascherare al firmware/glue il bit 7 delle scritture a `$D105`, perché la riconfigurazione distrugge VRAM e blocca il bus ~100 ms. In alternativa il software non deve mai scriverlo (i test già non lo fanno). **Non implementato nel firmware ESP32**: il dato è valido solo a metà di PHI2, quindi `DEV_SEL_N` andrebbe asserito in ritardo solo per `$D105`; ritirarlo dopo la lettura non basta, perché la VERA registra la scrittura alla fine di `bus_write` comunque. Senza hardware per misurarlo, un errore di temporizzazione farebbe perdere scritture legittime a CTRL (cambi di DCSEL): rischio peggiore del problema |

Durante una riconfigurazione il bus VERA non è pilotato: le letture danno valori casuali.
L'emulatore la simula con 100 ms di bus morto (`-verax16-config-ms`, 0 = RESET tenuto fino a
CDONE).

---

## 4. Livelli, alimentazioni e temporizzazioni

- **Livelli.** VERA, ESP32 e FPGA sono a 3,3 V; il bus Atari è a 5 V TTL. Tutti i segnali verso
  l'Atari passano da 74LVC4245. Controllare che le uscite 3,3 V rispettino VIH dei ricevitori
  sul bus (TTL ≥ 2,0 V) e che gli ingressi sul lato 5 V non superino i limiti dell'FPGA.
- **Alimentazioni.** In tutti e cinque i 74LVC4245 `VCCA` (pin 1) è a 5 V (lato A = Atari) e `VCCB`
  (pin 23, 24) a 3,3 V (lato B = ESP32/FPGA); `DIR` e `~OE` sono riferiti a VCCA con soglia TTL.
  `PIN-MAPPING.md` è stato corretto (2026-10-26): prima descriveva i due lati invertiti.
- **Tempo di risposta in lettura.** In lettura la VERA risponde da registri e dati già pronti
  (prefetch): alla scheda basta il tempo di propagazione attraverso `U4/U3`, FPGA e `U19`
  (circa 30-50 ns stimati, da misurare). Per la ROM/RAM `$D600-$DFFF` servite dall'ESP32 il
  margine è molto più stretto (fetch di istruzioni: tutti i cicli): il firmware ha ~279 ns per
  ciclo, vedi `Vera_Module_Analisys.ITA.tex`.
- **Frequenza di quadro.** La VERA gira a 59,94 Hz fisso, asincrona rispetto al VBI Atari. Non
  è un problema hardware, ma il software non può assumere sincronismo fra i due.

---

## 5. Regole software da rispettare con l'hardware reale

Il driver e i test già le seguono, ma valgono per qualunque programma futuro:

1. **Mai** `inc`, `dec`, `asl`, `lsr`, `rol`, `ror` su `$D100-$D11F`: le istruzioni RMW fanno
   due accessi e duplicano l'evento (autoincremento due volte su `DATA0/DATA1`).
2. **Mai** indirizzamento indicizzato su `$D100-$D11F` (una dummy read al cambio di pagina
   colpisce `DATA0/DATA1`), né `sta abs,x` / `sta (zp),y` con base nella pagina `$D1`.
3. Non scrivere `$80` in `$D105` (riconfigurazione dell'FPGA).
4. Non leggere i registri FX: sono in sola scrittura (restituiscono i byte di identità del firmware, `'V',48,0,1` sulla 48.0.1).
5. Niente sincronismo supposto con il VBI Atari.
6. Gli IRQ della VERA si gestiscono agganciando `VIMIRQ` (`$0216`), non via `$D1FF`/`PDIMSK` (§2.4).
7. Dopo il reset Atari la VERA mantiene tutto lo stato: l'handler `INIT` ne riscrive solo una
   parte. Un programma che lascia FX o IEN attivi lascia lo stato anche al riavvio.

---

## 6. Piano di bring-up consigliato

1. **Senza Atari**: alimentare la scheda, verificare `CDONE`, la sequenza `CRESET`→`ARESET`
   (tempi) e i livelli sul bus con `ARESET` scollegato.
2. **Con l'Atari, scheda scollegata dal bus dati** (`U19` disabilitato): verificare che
   l'Atari parta normalmente con `ARESET` rilasciato dalla scheda.
3. **Con `U19` abilitato**: controllare con l'oscilloscopio che non ci sia contesa nelle letture
   di RAM/ROM (§2.1) e il margine di hold sulle scritture (§2.2).
4. **Rilevamento**: dal DOS, `vera_detect()` (CTRL=`$7E`, `$D109`=`'V'`, `$D10A`=47) via
   un programma minimo; poi `$D1FF`/ROM (`$D803=$80`, `$D80B=$91`).
5. **Test funzionali** in questo ordine: da `disk3-standalone.atr` `TESTFX.COM` (atteso
   **PASS: 36, FAIL: 0**, e la riga `VERA firmware 48.0.1`); da `disk2-veratests-*.atr` `TEST8.COM`, `TESTGS8.COM`, `TESTMAZ8.COM`,
   `TESTMTX8.COM` (ESC per uscire); da `disk3-standalone.atr` `TESTPLR.COM`; poi `RUNCPM.COM`
   (`disk1-runcpm.atr`) con FujiNet.
6. **IRQ** (stadio open-drain `U26`, §2.3): `TESTIRQ.COM` da `disk3-standalone.atr` (atteso PASS 13, FAIL 0).
7. Ogni anomalia va riprodotta in emulatore con `-verax16-config-ms` (0 e 100) per separare
   problemi software da problemi di scheda.

---

## 7. Punti aperti da decidere

- Architettura: la documentazione descrive **due** soluzioni per RAM/ROM PBI: servite
  dall'ESP32 (`PIN-MAPPING.md`, `firmware/MCU`) oppure dalla BRAM di un'FPGA di interfaccia
  (`firmware/FPGA`, `C6502_Interface.vhd`). Va stabilita quale è quella attuale e allineata la
  documentazione.
- Presenza o meno di una decodifica hardware (`addressdecoder.sch`: 74LVC138, `~dD1XX`,
  `~dROMSEL`, `~dRAMSEL`) accanto alla decodifica software dell'ESP32.
- Compatibilità ECI del 130XE: confermare che l'ECI porti `MPD`, `EXTSEL`, `IRQ` e `D1XX`, e che
  l'OS XE esegua la scansione PBI (`$D1FF`, `$D803`, `$D80B`) come l'OS XL.
