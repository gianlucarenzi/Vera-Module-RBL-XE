# 🚀 Interfaccia 6502 per FPGA Lattice iCE40UP5K

[![License](https://img.shields.io/badge/license-Open%20Source-blue.svg)](LICENSE)
[![FPGA](https://img.shields.io/badge/FPGA-Lattice%20iCE40UP5K-green.svg)](https://www.latticesemi.com/Products/FPGAandCPLD/iCE40UltraPlus)
[![Toolchain](https://img.shields.io/badge/toolchain-Open%20Source-orange.svg)](https://github.com/YosysHQ/oss-cad-suite-build)

Questo progetto implementa una logica di interfaccia per un microprocessore 6502 (o compatibile) utilizzando la FPGA Lattice iCE40 UltraPlus (iCE40UP5K). Il progetto è ottimizzato per l'uso della toolchain open-source su Linux (Project IceStorm, Yosys, nextpnr), sfruttando le Block RAM (BRAM) del chip per la memoria interna.

## 📋 Indice

- [Caratteristiche](#-caratteristiche)
- [Requisiti Funzionali](#-requisiti-funzionali)
- [Architettura](#-architettura)
- [Toolchain Open-Source](#️-toolchain-open-source)
- [Struttura del Progetto](#-struttura-del-progetto)
- [Installazione](#-installazione)
- [Configurazione Pin](#-configurazione-pin)
- [Compilazione](#️-compilazione)
- [Utilizzo](#-utilizzo)
- [Troubleshooting](#-troubleshooting)
- [Contribuire](#-contribuire)

## ✨ Caratteristiche

- **Interfaccia 6502 completa**: Supporto per bus dati bidirezionale a 8 bit e bus indirizzi a 16 bit
- **Memoria integrata**: 
  - 512 byte di RAM interna ($D600-$D7FF)
  - 2 KB di ROM interna ($D800-$DFFF)
- **Controllo latch VERA_CS**: Configurabile tramite DIP switch
- **Segnali di controllo**: MPD e EXTSEL per gestione memoria esterna
- **Ottimizzato per BRAM**: Utilizza le Block RAM native dell'iCE40UP5K
- **Toolchain open-source**: Completamente compatibile con strumenti liberi

## 📋 Requisiti Funzionali

L'interfaccia gestisce le seguenti aree di memoria/I/O:

### 🔧 Latch VERA_CS
- **Indirizzo**: `$D1FF` (scrittura)
- **Controllo**: 3 DIP switch esterni (`DIP_SEL[2:0]`) selezionano il bit di controllo
- **Funzione**: Abilita/disabilita l'accesso alla RAM esterna

### 💾 RAM Esterna (512 Byte)
- **Range**: `$D600-$D7FF`
- **Tipo**: Lettura/scrittura
- **Condizione**: Attiva quando `VERA_CS = '0'`
- **Controllo**: Pin `EXTSEL` abbassato per disabilitare ROM/RAM interna del 6502

### 📚 ROM Interna (2 KB)
- **Range**: `$D800-$DFFF`
- **Tipo**: Sola lettura
- **Controllo**: Pin `MPD` abbassato durante l'accesso

## 🏗️ Architettura

### Diagramma di Sistema

```
┌─────────────────┐    ┌─────────────────┐    ┌─────────────────┐
│   6502 CPU      │    │  iCE40UP5K      │    │  Memoria        │
│                 │    │  FPGA           │    │  Esterna        │
├─────────────────┤    ├─────────────────┤    ├─────────────────┤
│ A[15:0] ────────┼────┤ Address Bus     │    │                 │
│ D[7:0]  ────────┼────┤ Data Bus        │    │                 │
│ PHI2    ────────┼────┤ Clock           │    │                 │
│ R/W     ────────┼────┤ Read/Write      │    │                 │
│                 │    │                 │    │                 │
│                 │    │ VERA_CS ────────┼────┤ Chip Select     │
│                 │    │ MPD     ────────┼────┤ ROM Select      │
│                 │    │ EXTSEL  ────────┼────┤ RAM Select      │
└─────────────────┘    └─────────────────┘    └─────────────────┘
                              │
                       ┌─────────────────┐
                       │  DIP Switches   │
                       │  [2:0]          │
                       └─────────────────┘
```

### Flowchart del Processo Logico

Il seguente diagramma mostra il flusso di esecuzione del processo principale nell'FPGA:

```mermaid
flowchart TD
    Start([🔄 PHI2 Rising Edge]) --> Reset[🔧 Reset Control Signals<br/>MPD = '1', EXTSEL = '1'<br/>d_drive_enable = '0']
    
    Reset --> Check1{📝 Scrittura Latch?<br/>RNW_='0' AND A=$D1FF}
    
    Check1 -->|✅ Sì| Latch[🎛️ Aggiorna VERA_CS Latch<br/>bit_index = DIP_SEL<br/>vera_cs_latch = NOT D[bit_index]]
    Check1 -->|❌ No| Check2
    
    Latch --> Check2{💾 Accesso RAM?<br/>A in $D600-$D7FF<br/>AND VERA_CS='0'}
    
    Check2 -->|✅ Sì| RAMAccess[🔽 EXTSEL = '0']
    Check2 -->|❌ No| Check3{📚 Accesso ROM?<br/>A in $D800-$DFFF<br/>AND RNW_='1'}
    
    RAMAccess --> RAMOp{📝 Operazione RAM}
    RAMOp -->|RNW_='0'<br/>Scrittura| WriteRAM[✍️ Scrivi in RAM Interna<br/>internal_ram[offset] = D]
    RAMOp -->|RNW_='1'<br/>Lettura| ReadRAM[📖 Leggi da RAM Interna<br/>d_out_drive = internal_ram[offset]<br/>d_drive_enable = '1']
    
    WriteRAM --> End([🏁 Fine Processo])
    ReadRAM --> End
    
    Check3 -->|✅ Sì| ROMAccess[🔽 MPD = '0']
    Check3 -->|❌ No| End
    
    ROMAccess --> ReadROM[📖 Leggi da ROM Interna<br/>d_out_drive = pbi_driver[offset]<br/>d_drive_enable = '1']
    ReadROM --> End
    
    style Start fill:#e1f5fe
    style Reset fill:#fff3e0
    style Latch fill:#f3e5f5
    style RAMAccess fill:#e8f5e8
    style ROMAccess fill:#fff8e1
    style End fill:#ffebee
    style Check1 fill:#e3f2fd
    style Check2 fill:#e3f2fd
    style Check3 fill:#e3f2fd
```

### Legenda del Flowchart

| Simbolo | Significato |
|---------|-------------|
| 🔄 | Evento di clock (PHI2 rising edge) |
| 🔧 | Inizializzazione/Reset |
| 📝 | Operazione di scrittura |
| 📖 | Operazione di lettura |
| 💾 | Accesso alla RAM |
| 📚 | Accesso alla ROM |
| 🎛️ | Controllo del latch |
| 🔽 | Attivazione segnale di controllo |
| 🏁 | Fine del processo |

### Mappatura Indirizzi

| Range | Tipo | Funzione | Segnale Controllo |
|-------|------|----------|-------------------|
| `$D1FF` | Scrittura | Controllo latch VERA_CS | - |
| `$D600-$D7FF` | R/W | RAM interna (512 byte) | EXTSEL = '0' |
| `$D800-$DFFF` | Lettura | ROM interna (2 KB) | MPD = '0' |

## 🛠️ Toolchain Open-Source

Questo progetto richiede la toolchain open-source per iCE40:

| Strumento | Funzione | Versione Minima |
|-----------|----------|-----------------|
| **GHDL** | Front-end per l'analisi e l'elaborazione del codice VHDL | ≥ 1.0 |
| **Yosys** | Sintesi RTL (converte VHDL in netlist logica) | ≥ 0.9 |
| **nextpnr-ice40** | Place & Route (mappa la netlist sulle risorse del chip) | ≥ 0.1 |
| **IceStorm Tools** | `icepack` (crea il bitstream) e `iceprog` (carica il bitstream) | ≥ 1.0 |

### Installazione Raccomandata

Si raccomanda l'installazione tramite la suite **YosysHQ OSS-CAD Suite** per un ambiente già configurato con supporto GHDL/VHDL:

```bash
# Download e installazione OSS-CAD Suite
wget https://github.com/YosysHQ/oss-cad-suite-build/releases/latest/download/oss-cad-suite-linux-x64-latest.tgz
tar -xzf oss-cad-suite-linux-x64-latest.tgz
export PATH="$PWD/oss-cad-suite/bin:$PATH"
```

## 📂 Struttura del Progetto

```
FPGA/
├── README.md              # Questo file
├── custom_types.vhd       # Definizioni dei tipi di array per memoria
├── C6502_Interface.vhd    # Entità principale e logica di controllo
└── scheda.pcf            # File di vincoli pin (da creare)
```

### File VHDL

#### 1. `custom_types.vhd`
Definisce i tipi di array per la memoria:

```vhdl
-- ROM (2048 x 8 bit) per l'area $D800-$DFFF
type rom_array is array (0 to 2047) of std_logic_vector(7 downto 0);

-- RAM (512 x 8 bit) per l'area $D600-$D7FF
type ram_512_array is array (0 to 511) of std_logic_vector(7 downto 0);
```

#### 2. `C6502_Interface.vhd`
Contiene:
- Entità principale con tutti i segnali di interfaccia
- Logica di controllo per latch, RAM e ROM
- Implementazione delle memorie BRAM
- Gestione del bus dati bidirezionale

> **Nota**: L'inizializzazione dei segnali `pbi_driver` (ROM) e `internal_ram` (RAM) nel codice è il metodo corretto per forzare Yosys a utilizzare la Block RAM (BRAM) dedicata dell'iCE40UP5K.

## 🔧 Installazione

### Prerequisiti

- Sistema operativo Linux (Ubuntu/Debian raccomandato)
- Git per il controllo versione
- Make per l'automazione della build

### Clonazione del Repository

```bash
git clone <repository-url>
cd FPGA
```

## 📍 Configurazione Pin

Per connettere i segnali VHDL ai pin fisici della FPGA, è necessario creare un file PCF (Pin Constraint File). 

### Esempio `scheda.pcf`

> **⚠️ Importante**: I pin devono essere adattati alla TUA scheda specifica!

```pcf
# Segnali 6502
set_io PHI2     35
set_io RNW_     34

# Uscite di Controllo
set_io VERA_CS  32
set_io MPD      31
set_io EXTSEL   30

# Selettore Latch (DIP Switches)
set_io DIP_SEL[0] 29
set_io DIP_SEL[1] 28
set_io DIP_SEL[2] 27

# Bus Dati (8 bit, Bidirezionale)
set_io D[0]     60
set_io D[1]     61
set_io D[2]     62
set_io D[3]     63
set_io D[4]     64
set_io D[5]     65
set_io D[6]     66
set_io D[7]     67

# Bus Indirizzi (16 bit)
set_io A[0]     40
set_io A[1]     41
set_io A[2]     42
set_io A[3]     43
set_io A[4]     44
set_io A[5]     45
set_io A[6]     46
set_io A[7]     47
set_io A[8]     48
set_io A[9]     49
set_io A[10]    50
set_io A[11]    51
set_io A[12]    52
set_io A[13]    53
set_io A[14]    54
set_io A[15]    55
```

## ⚙️ Compilazione

### Processo Completo

Una volta installata la toolchain e creati i file `.vhd` e il file `.pcf`:

#### 1. Analisi e Sintesi (GHDL & Yosys)

```bash
# Analisi e elaborazione dei moduli VHDL
ghdl -a custom_types.vhd
ghdl -a C6502_Interface.vhd
ghdl -e C6502_Interface

# Sintesi con Yosys (genera la netlist JSON)
yosys -m ghdl -p 'ghdl C6502_Interface; synth_ice40 -top C6502_Interface -json C6502_Interface.json'
```

#### 2. Place and Route (nextpnr)

```bash
# Sostituisci --package se necessario (tqfp100, sg48, etc.)
nextpnr-ice40 --up5k --package tqfp100 --json C6502_Interface.json --pcf scheda.pcf --asc C6502_Interface.asc
```

#### 3. Generazione e Caricamento del Bitstream

```bash
# Crea il bitstream binario
icepack C6502_Interface.asc C6502_Interface.bin

# Carica il bitstream sulla Flash SPI
iceprog C6502_Interface.bin
```

### Script di Build Automatico

Puoi creare un Makefile per automatizzare il processo:

```makefile
# Makefile
PROJECT = C6502_Interface
SOURCES = custom_types.vhd C6502_Interface.vhd
PCF = scheda.pcf

all: $(PROJECT).bin

$(PROJECT).json: $(SOURCES)
	ghdl -a $(SOURCES)
	ghdl -e $(PROJECT)
	yosys -m ghdl -p 'ghdl $(PROJECT); synth_ice40 -top $(PROJECT) -json $@'

$(PROJECT).asc: $(PROJECT).json $(PCF)
	nextpnr-ice40 --up5k --package tqfp100 --json $< --pcf $(PCF) --asc $@

$(PROJECT).bin: $(PROJECT).asc
	icepack $< $@

prog: $(PROJECT).bin
	iceprog $<

clean:
	rm -f *.json *.asc *.bin work-obj93.cf

.PHONY: all prog clean
```

Utilizzo:
```bash
make          # Compila tutto
make prog     # Programma la FPGA
make clean    # Pulisce i file temporanei
```

## 🚀 Utilizzo

### Configurazione Iniziale

1. **Connetti la scheda** al sistema 6502
2. **Configura i DIP switch** (`DIP_SEL[2:0]`) per selezionare il bit di controllo desiderato (0-7)
3. **Carica il bitstream** sulla FPGA

### Operazioni Supportate

#### Controllo del Latch VERA_CS
```assembly
; Esempio: Attiva VERA_CS scrivendo '1' nel bit selezionato dai DIP switch
LDA #$01        ; Carica il valore con bit 0 = 1
STA $D1FF       ; Scrive nel registro di controllo
```

#### Accesso alla RAM ($D600-$D7FF)
```assembly
; La RAM è accessibile solo quando VERA_CS = '0'
LDA #$AA        ; Dato di test
STA $D600       ; Scrive nella RAM (primo byte)
LDA $D600       ; Legge dalla RAM
```

#### Lettura dalla ROM ($D800-$DFFF)
```assembly
LDA $D800       ; Legge il primo byte della ROM
```

## 🔍 Troubleshooting

### Problemi Comuni

#### Errore di Sintesi
```
Error: Module `C6502_Interface' not found
```
**Soluzione**: Verifica che tutti i file VHDL siano stati analizzati correttamente con GHDL.

#### Errore di Place & Route
```
Error: Unable to place cell
```
**Soluzione**: Controlla che i pin nel file PCF siano validi per il package della tua FPGA.

#### Problemi di Programmazione
```
Error: Could not access USB device
```
**Soluzione**: Verifica i permessi USB o esegui `iceprog` con `sudo`.

### Debug

#### Verifica della Sintesi
```bash
# Controlla le risorse utilizzate
yosys -m ghdl -p 'ghdl C6502_Interface; synth_ice40 -top C6502_Interface; stat'
```

#### Analisi del Timing
```bash
# Aggiungi opzioni di timing a nextpnr
nextpnr-ice40 --up5k --package tqfp100 --json C6502_Interface.json --pcf scheda.pcf --asc C6502_Interface.asc --freq 25
```

## 🤝 Contribuire

Le contribuzioni sono benvenute! Per contribuire:

1. **Fork** il repository
2. **Crea** un branch per la tua feature (`git checkout -b feature/AmazingFeature`)
3. **Commit** le tue modifiche (`git commit -m 'Add some AmazingFeature'`)
4. **Push** al branch (`git push origin feature/AmazingFeature`)
5. **Apri** una Pull Request

### Linee Guida

- Mantieni il codice VHDL ben commentato
- Testa le modifiche su hardware reale quando possibile
- Aggiorna la documentazione per nuove funzionalità
- Segui lo stile di codifica esistente

## 📄 Licenza

Questo progetto è distribuito sotto licenza open-source. Vedi il file `LICENSE` per i dettagli.

## 🙏 Ringraziamenti

- **YosysHQ** per la toolchain open-source
- **Lattice Semiconductor** per la documentazione dell'iCE40UP5K
- **Comunità 6502** per il supporto e i test

---

**Nota**: Il chip iCE40UP5K si configurerà automaticamente all'accensione leggendo il file `C6502_Interface.bin` dalla Flash SPI esterna.