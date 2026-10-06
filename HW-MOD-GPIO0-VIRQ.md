# Modifica hardware: `~mVIRQ` → GPIO0 (ESP32-S3)

Promemoria per il filo di modifica che porta l'IRQ della VERA all'ESP32. Solo per la scheda
attuale (netlist del 2026-09-01); se lo schema cambia, ricontrollare i riferimenti dei
componenti.

## 1. Perché serve

Il PBI Atari identifica l'origine di un IRQ leggendo `$D1FF`: ogni device pilota **il proprio bit**
(la VERA: D7) a 1 quando ha richiesto l'interrupt, gli altri bit restano Hi-Z. L'OS legge
`$D1FF`, la maschera con `PDIMSK` e chiama la ROM del device corrispondente (vettore IRQ a
`$D808`). Il firmware dell'ESP32 serve già la ROM `$D800` e il latch `$D1FF`, ma **non sa se
la VERA ha un IRQ attivo**: tutti i GPIO sono assegnati e `~mVIRQ` va solo all'FPGA e al
connettore ECI, mai all'ESP32.

Senza questo filo la scheda non può rispondere correttamente a `$D1FF`. Oggi non fa danni
perché il software non abilita nessun IRQ della VERA; diventa necessario appena un programma
usa VSYNC, LINE o SPRCOL.

L'unico GPIO utilizzabile è **GPIO0** (QFN56 pin 5, `U7` pin 5): è anche il pin di strapping di
boot, ma il suo uso normale è solo il programmatore.

## 2. Cosa c'è oggi su GPIO0 (rete `BOOT0`)

| Componente | Valore | Funzione |
|---|---|---|
| `R33` | 4,7 kΩ → 3V3 | pull-up di boot |
| `SW3` | → GND | pulsante BOOT (modalità download) |
| `C61` | 100 nF → GND | filtro |
| `R54` | 220 Ω → `Q4` pin 3 | auto-reset dal programmatore (CH340) |

`PIN-MAPPING.md` dice "GPIO 0 NC": non è vero, la rete `BOOT0` è in uso.

## 3. Il collegamento

```
 ~mVIRQ  (R65 pin 2 / U16 pin 18 / U2 pin 32, lato 3,3 V, PRIMA dello stadio open-drain)
    │
   [R_sense 470 Ω]            ← nuova resistenza (0603/0805, 1 %)
    │
 BOOT0   (SW3 pin 2 / R33 pin 2, il più vicino possibile a U7 pin 5)
```

- **Sorgente:** `~mVIRQ` sul lato 3,3 V, cioè pad 2 di `R65` (o `U16` pin 18). **Non** dal lato
  ECI/5 V e **non** dopo il `74LVC1G07`.
- **Destinazione:** pad della rete `BOOT0` vicino a `U7` (pin 2 di `SW3` o pin 2 di `R33`).
- **Resistenza serie: 470 Ω.** Con la linea IRQ attiva (FPGA a ~0,2 V) la tensione su `BOOT0`
  è ≈ 0,5 V (partitore con `R33`), sotto la soglia bassa dell'ESP32 (≈ 0,8 V). Un valore da
  1 kΩ darebbe ≈ 0,75 V, troppo vicino alla soglia: non usarlo.
- **Perché la resistenza.** Evita che l'uscita push-pull dell'FPGA vada in cortocircuito con
  `SW3` o con `Q4` quando premi BOOT o programmi: con 470 Ω la corrente resta ≈ 7 mA.
- Filo smaltato (30 AWG) o pad di rework; resistenza saldata vicino a `U7`.

## 4. Regole e rischi (leggere prima di saldare)

1. **Strapping.** GPIO0 basso al reset dell'ESP32 = modalità download: il firmware non parte.
   `~mVIRQ` deve quindi essere **alto** quando l'ESP32 si resetta. A riposo lo è (`R65` +
   `R33`). Rischio residuo: reset dell'ESP32 con la VERA ancora in funzione e un IRQ attivo; per
   evitarlo tenere l'FPGA in reset insieme all'ESP32 (pull-down su `CRESET`).
2. **Pulsante BOOT durante il funzionamento.** Premendolo, `BOOT0` va a 0 e il firmware vede
   un falso IRQ finché lo tieni premuto. È innocuo (la ROM `IRQVECTOR` non fa nulla senza
   sorgente), ma non usarlo con il sistema in uso.
3. **Costante di tempo.** `C61` (100 nF) con 470 Ω dà ~50 µs sul fronte; irrilevante per il bus.
4. **Ordine dei lavori.** Fare prima la modifica dello stadio open-drain per l'IRQ
   (`VERA-ATARI-HW-REQUIREMENTS.md` §2.3), poi questa. Questo filo è solo in **ingresso**
   all'ESP32: non cambia ciò che arriva sul bus Atari.
5. **Firmware.** Il filo è inutile senza il firmware giusto, e il firmware con questa opzione
   attiva su una scheda **senza** il filo leggerebbe GPIO0 (con il pull-up interno, quindi alto):
   non succede niente di male, ma il bit D7 non verrebbe mai pilotato.

## 5. Firmware

Compilare e programmare l'ambiente dedicato:

```sh
cd firmware/MCU
pio run -e esp32s3fn8-virq -t upload
```

(equivale a `-D VERA_BOARD_IS_PBI=1 -D VERA_HAS_VIRQ_SENSE=1`). Per l'uso normale senza la
modifica restano `esp32s3fn8` e `esp32s3fn8-cctl`.

Comportamento: a ogni **lettura** di `$D1FF` (con la scheda selezionata o no) il firmware legge
GPIO0; se è basso pilota solo D7 = 1 sul bus dati (D0-D6 restano Hi-Z, `bus_drive_d7()`).

## 6. Verifica dopo la modifica

1. **A scheda spenta**, con il multimetro: continuità tra `R65` pad 2 e `SW3` pad 2 attraverso
   470 Ω; nessun corto verso GND o 3V3.
2. **A riposo** (nessun IRQ): `BOOT0` ≈ 3,3 V. L'ESP32 deve partire normalmente.
3. **Forzare un IRQ** dal BASIC, per un tempo brevissimo (l'OS non serve l'IRQ della VERA
   finché `PDIMSK` non è impostato, vedi la nota sotto: con l'IRQ a livello il computer rallenta
   molto finché lo spegni):

   ```basic
   10 POKE 53510,1
   20 X=PEEK(53759)
   30 IF X<128 THEN 20
   40 POKE 53510,0:POKE 53511,7:PRINT X
   ```

   (`53510` = `IEN` `$D106`, `53511` = `ISR` `$D107`, `53759` = `$D1FF`.) Abilita l'IRQ VSYNC,
   aspetta che D7 di `$D1FF` valga 1, poi spegne l'IRQ e cancella `ISR`. Con l'oscilloscopio
   su `BOOT0` si vede il livello basso mentre l'IRQ è attivo. Se `X` resta < 128 il filo,
   la resistenza o il firmware `esp32s3fn8-virq` non sono a posto.
4. Prova di regressione: `TESTFX.COM`, `TEST8.COM` e `RUNCPM.COM` devono comportarsi come prima.

> **Attenzione.** L'OS chiama la ROM del device solo se il bit corrispondente di `PDIMSK`
> (`$0249`, verificare l'indirizzo sulla tua mappa) è a 1. L'handler `INIT` imposta oggi solo
> `PDVMSK` (`$0247`). Un programma che abilita IRQ VERA deve impostare anche il bit 7 di
> `PDIMSK`, altrimenti con l'IRQ a livello l'OS rientra continuamente senza mai servire la
> sorgente.

## 7. Come annullare

Togliere la resistenza `R_sense` (e il filo). Nessun'altra connessione è stata modificata. Il
firmware dell'ambiente `esp32s3fn8-virq` si può sostituire con `esp32s3fn8` in qualsiasi momento.
