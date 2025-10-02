# Analisi del Progetto: Vera-Module-RBL-XE Firmware

## 1. Introduzione

Questo repository contiene il firmware per il `Vera-Module-RBL-XE`, un modulo di espansione hardware custom per i computer **ATARI della serie XE**.

L'analisi del codice e della struttura dei file suggerisce che questo progetto implementa un'interfaccia hardware che si collega alla **Parallel Bus Interface (PBI)** dell'ATARI. Il sistema è composto da due parti principali che lavorano in sinergia:

1.  Un **firmware per un microcontrollore moderno** (Raspberry Pi Pico).
2.  Un **driver in assembly 6502** che gira sull'ATARI per comunicare con il modulo.

L'obiettivo del modulo è probabilmente quello di estendere le capacità dell'ATARI, offloadando compiti complessi o fornendo nuove funzionalità hardware (come grafica avanzata, storage, connettività di rete, ecc.) gestite dal potente microcontrollore.

---

## 2. Architettura del Progetto

Il progetto ha un'architettura ibrida, con due codebase distinte.

### Firmware del Microcontrollore (MCU)

Questa è la parte principale del progetto, gestita con **PlatformIO**.

-   **Scheda:** Il file `platformio.ini` specifica che il microcontrollore target è un **Raspberry Pi Pico** (basato sul chip RP2040).
-   **Codice Sorgente:** Il file `src/main.cpp` è il punto di ingresso del firmware. Questo codice C++ gestisce la logica principale del modulo.
-   **Interfaccia:** Il file `include/pbi-driver.h` definisce le strutture dati, le costanti e le firme delle funzioni che l'MCU usa per comunicare con il driver 6502 sull'ATARI.

La logica in `main.cpp` è responsabile di:
-   Inizializzare l'hardware del Pico.
-   Mettersi in ascolto di comandi provenienti dall'ATARI attraverso la PBI.
-   Eseguire le operazioni richieste.
-   Restituire i risultati all'ATARI.

### Driver 6502 per ATARI

Questa parte del progetto risiede nella cartella `6502/`.

-   **Codice Sorgente:** Il file `6502/src/pbi-driver.s` è un driver scritto in **assembly 6502**. Questo codice viene eseguito direttamente dalla CPU dell'ATARI.
-   **Scopo:** La sua funzione è quella di gestire la comunicazione a basso livello con il modulo hardware attraverso la PBI. Inizializza il bus, invia comandi al modulo MCU e legge le risposte.
-   **Compilazione:** Il `Makefile` presente nella cartella `6502/` suggerisce che il driver viene compilato utilizzando un toolchain custom (probabilmente `ca65` o simili) per creare un file binario eseguibile dall'ATARI.

---

## 3. Struttura dei File Principali

```
/
├── platformio.ini            # File di configurazione di PlatformIO (target: RPi Pico)
├── 6502/
│   ├── Makefile              # Makefile per compilare il driver 6502
│   └── src/
│       └── pbi-driver.s      # Codice assembly del driver PBI per ATARI
├── include/
│   └── pbi-driver.h          # Header C++ con l'interfaccia di comunicazione
└── src/
    └── main.cpp              # Codice sorgente C++ principale per l'MCU
```

---

## 4. Tecnologie Utilizzate

-   **Linguaggi:** C++, Assembly 6502
-   **Piattaforma Embedded:** PlatformIO
-   **Hardware MCU:** Raspberry Pi Pico (RP2040)
-   **Hardware Host:** ATARI XE (con interfaccia PBI)
-   **Build System:** `make` (per il codice 6502)

---

## 5. Compilazione del Progetto

Per compilare le due parti del progetto, sono necessari due approcci diversi.

### Compilazione Firmware MCU

È necessario avere **PlatformIO Core** installato.

```bash
# Compila il progetto per il Raspberry Pi Pico
platformio run

# Carica il firmware sulla scheda (in modalità bootloader)
platformio run --target upload
```

### Compilazione Driver 6502

È necessario avere una toolchain di sviluppo per 6502 (es. `cc65`) installata e configurata nel proprio `PATH`.

```bash
# Entra nella cartella del driver
cd 6502

# Compila il driver utilizzando il Makefile
make
```
Questo produrrà un file binario che può essere caricato ed eseguito su un computer ATARI.