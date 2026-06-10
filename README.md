# VERA Module RBL-XE

<div align="center">

[![License: GPL v3](https://img.shields.io/badge/License-GPLv3-blue.svg)](https://www.gnu.org/licenses/gpl-3.0)
[![KiCad](https://img.shields.io/badge/KiCad-5.1.9-blue.svg)](https://www.kicad.org/)
[![ATARI Ready](https://img.shields.io/badge/ATARI-Ready-red.svg)](https://atari.com)
[![ESP32](https://img.shields.io/badge/ESP32--S3-FN8-yellow.svg)](https://www.espressif.com/)

*🌐 English | [🌐 Italiano](#vera-module-rbl-xe-italiano)*

</div>

---

<div style="background: #ffffff; border-radius: 16px; padding: 24px; margin: 24px 0; box-shadow: 0 4px 12px rgba(0,0,0,0.1); border: 1px solid #e0e0e0;">

## 📋 Table of Contents

- [🔍 Overview](#-overview)
- [✨ Features](#-features)
- [⚙️ Technical Specifications](#️-technical-specifications)
- [🔧 Hardware Components](#-hardware-components)
- [🖼️ Board Preview](#️-board-preview)
- [📐 PCB Layers](#-pcb-layers)
- [🔌 Connectors & Interfaces](#-connectors--interfaces)
- [⚡ Power Requirements](#-power-requirements)
- [🔨 Assembly Instructions](#-assembly-instructions)
- [📦 Bill of Materials](#-bill-of-materials)
- [💻 Programming & Configuration](#-programming--configuration)
- [📁 Project Structure](#-project-structure)
- [🤝 Contributing](#-contributing)
- [📄 License](#-license)

</div>

---

<div style="background: #ffffff; border-radius: 16px; padding: 24px; margin: 24px 0; box-shadow: 0 4px 12px rgba(0,0,0,0.1); border: 1px solid #e0e0e0;">

## 🔍 Overview

The **VERA Module RBL-XE** is a sophisticated FPGA-based audio and video 
expansion card designed specifically for **ATARI XE** computers. 
This module brings modern graphics and audio capabilities to classic ATARI systems 
through the **PBI (Parallel Bus Interface)** protocol connectors.

### What is VERA?

**VERA** (Video Enhanced Retro Adapter) is originally designed for the 
Commander X16 computer but has been adapted for ATARI systems. It provides:

- **Advanced Graphics**: Multiple video layers, sprites, and high-resolution modes
- **Enhanced Audio**: Multi-channel digital audio with PCM and PSG capabilities  
- **Modern Connectivity**: VGA output, SD card storage, and USB programming
- **Retro Compatibility**: Integration with ATARI software with simple drivers

</div>

---

<div style="background: #ffffff; border-radius: 16px; padding: 24px; margin: 24px 0; box-shadow: 0 4px 12px rgba(0,0,0,0.1); border: 1px solid #e0e0e0;">

## ✨ Features

### 🎨 Graphics Capabilities

#### Video Output
- **VGA Output**: 640x480 @ 60Hz with analog RGB signals
- **Multiple Layers**: Background, foreground, and sprite layers
- **Color Depth**: 256 colors from a palette of 4096
- **Sprites**: Hardware-accelerated sprite engine
- **Scrolling**: Smooth horizontal and vertical scrolling

#### Audio Features
- **Digital Audio**: High-quality PCM audio output
- **Multi-channel**: Stereo audio
- **Audio Codec**: WM8524 for professional audio quality
- **3.5mm Jack**: Standard audio output connector

### 🔗 System Integration

#### ATARI Compatibility
- **PBI Interface**: Direct connection to ATARI XE systems
- **Memory Mapping**: $D1XX-$DFXX address space
- **Bus Decoder**: Full software address decoding in ESP32 — A0–A15 sampled every PHI2 cycle, no external hardware decoder required
- **Power Management**: Efficient 5V to 3.3V/1.2V conversion

#### Storage & Programming
- **SD Card Slot**: Micro SD for data storage and ROM loading
- **SPI Flash**: 16Mbit W25Q16 for FPGA configuration
- **USB Programming**: CH340 USB-to-serial for easy updates
- **SPI**: Direct FPGA programming interface

</div>

---

<div style="background: #ffffff; border-radius: 16px; padding: 24px; margin: 24px 0; box-shadow: 0 4px 12px rgba(0,0,0,0.1); border: 1px solid #e0e0e0;">

## ⚙️ Technical Specifications

### Core Specifications

| Component | Specification |
|-----------|---------------|
| **FPGA** | Lattice ICE40UP5K-SG48ITR |
| **Microcontroller** | ESP32-S3FN8 |
| **Flash Memory** | W25Q16JVSNIQ (16Mbit) |
| **Audio Codec** | WM8524CGEDT |
| **USB Interface** | CH340T |
| **Power Regulators** | AMS1117-3.3V, AMS1117-1.2V, L7805 |
| **Crystal Oscillator** | 25MHz ECS-2033-250-BN |
| **PCB Layers** | 4-layer (Signal/Power/GND/Signal) |
| **Board Size** | TBD mm x TBD mm |

### Electrical Characteristics

| Parameter | Min | Typ | Max | Unit |
|-----------|-----|-----|-----|------|
| **Supply Voltage** | 4.5 | 5.0 | 5.5 | V |
| **Current Consumption** | - | 800 | 1200 | mA |
| **Operating Temperature** | -10 | 25 | 70 | °C |
| **VGA Output Impedance** | - | 75 | - | Ω |
| **Audio Output Level** | - | 2.0 | - | Vrms |

</div>

---

<div style="background: #ffffff; border-radius: 16px; padding: 24px; margin: 24px 0; box-shadow: 0 4px 12px rgba(0,0,0,0.1); border: 1px solid #e0e0e0;">

## 🔧 Hardware Components

### Main Processing Units

#### FPGA - ICE40UP5K
- **Logic Elements**: 5,280 LUTs
- **Memory**: 120 Kbit BRAM, 1 Mbit SPRAM
- **I/O Pins**: 39 user I/O
- **Package**: QFN-48 (7x7mm)
- **Programming**: SPI configuration

#### ESP32-S3FN8
- **CPU**: Dual-core Xtensa LX7 @ 240 MHz
- **Memory**: 8 MB in-package Quad SPI flash, 512 KB SRAM
- **Connectivity**: Wi-Fi 802.11 b/g/n, Bluetooth 5 LE
- **GPIO**: 45 total (38 usable), all bidirectional
- **Package**: QFN-56 (7×7 mm)
- **Role**: Full software 6502-bus controller — reads A0–A15, PHI2, R/W every cycle and drives EXTSEL\_N, DEV\_SEL\_N, MPD in real time; handles PBI ($D1xx) and CCTL ($D5xx) in a single unified firmware
- **Timing**: hot loop on Core 1 at max FreeRTOS priority; control signals asserted via **Dedicated GPIO** Xtensa TIE instructions (`ee.get_gpio_in` / `ee.wr_mask_gpio_out`, 1 CPU cycle) within the 279 ns PHI2-high window of the 1.79 MHz 6502 clock

### Interface Components

#### Audio System
- **Codec**: WM8524CGEDT (24-bit, 192kHz)
- **Interface**: From FPGA digital audio bus
- **Output**: 3.5mm stereo jack
- **SNR**: >100dB

#### Video System
- **Output**: 15-pin VGA connector
- **Resolution**: 640x480 @ 60Hz
- **Colors**: 8-bit RGB (256 colors)
- **Sync**: Separate H/V sync signals

</div>

---

<div style="background: #ffffff; border-radius: 16px; padding: 24px; margin: 24px 0; box-shadow: 0 4px 12px rgba(0,0,0,0.1); border: 1px solid #e0e0e0;">

## 🖼️ Board Preview

### 3D Renderings

#### Top View
![VERA Module Top](images/VERA-MODULE-RBL-TOP.png)
*Top side showing main components and connectors*

#### Bottom View  
![VERA Module Bottom](images/VERA-MODULE-RBL-BOTTOM.png)
*Bottom side with additional components*

#### Cartridge Adapter
![VERA Cartridge Adapter](images/VERA-ADAPTER-CARTRIDGE.png)
*Optional cartridge interface adapter*

### Board Dimensions

- **Length**: TBD mm
- **Width**: TBD mm  
- **Thickness**: 1.6mm (4-layer PCB)
- **Mounting**: Compatible with ATARI XE CART + ECI slots
- **Connectors**: Flat for space efficiency. It needs a custom 3D printed case

</div>

---

<div style="background: #ffffff; border-radius: 16px; padding: 24px; margin: 24px 0; box-shadow: 0 4px 12px rgba(0,0,0,0.1); border: 1px solid #e0e0e0;">

## 📐 PCB Layers

### Layer Stack-up

#### Layer 1 - Top Copper (F.Cu)
![Top Copper Layer](images/F.Cu-L1.png)
*Component placement and signal routing*

#### Layer 2 - Power Plane (POWER)
![Power Layer](images/POWER-L2.png)
*5V, 3.3V, and 1.2V power distribution*

#### Layer 3 - Ground Plane (GND)
![Ground Layer](images/GND-L3.png)
*Solid ground plane for signal integrity*

#### Layer 4 - Bottom Copper (B.Cu)
![Bottom Copper Layer](images/B.Cu-L4.png)
*Additional signal routing and components*

</div>

---

<div style="background: #ffffff; border-radius: 16px; padding: 24px; margin: 24px 0; box-shadow: 0 4px 12px rgba(0,0,0,0.1); border: 1px solid #e0e0e0;">

## 🔌 Connectors & Interfaces

### External Connections

#### ATARI XE Interface
- **Cartridge Connector**: Classic Cartridge Edge Connector 30 pin 
- **ECI Slot**: Enhanced Cartridge Interface 14 pin 
- **Address Range**: $D1XX-$DFXX
- **Signals**: Data bus, address bus, control signals

#### Video Output (J2)
| Pin | Signal | Description |
|-----|--------|-------------|
| 1-3 | RGB | Red, Green, Blue analog signals |
| 13-14 | H/V Sync | Horizontal/Vertical sync |
| 6-10 | GND | Ground connections |
| 11-12 | Audio L/R | Stereo audio output |

#### Storage Interfaces
- **SD Card Slot (J3)**: Micro SD card interface
- **SPI Flash**: On-board 16Mbit storage
- **USB Port (J4)**: Programming and data transfer

### Programming Interfaces

#### ICEPROG Connector (J6)
| Pin | Signal | Function |
|-----|--------|----------|
| 1 | 5V | Power supply |
| 2 | FPGA_CDONE/PULL-UP | FPGA Programmed |
| 3 | FPGA_RESET | FPGA Reset |
| 4 | MISO | SPI data in |
| 5 | MOSI | SPI data out |
| 6 | SCK | SPI clock |
| 7 | SS | SPI select |
| 8 | GND | Ground |

#### Reset & Control
- **Reset Button (SW1)**: ESP32 system reset
- **DIP Switch (SW2)**: Configuration options
- **Status LEDs**: Power, programming, activity indicators

</div>

---

<div style="background: #ffffff; border-radius: 16px; padding: 24px; margin: 24px 0; box-shadow: 0 4px 12px rgba(0,0,0,0.1); border: 1px solid #e0e0e0;">

## ⚡ Power Requirements

### Power Distribution

#### Power Rails
| Rail | Voltage | Current | Components |
|------|---------|---------|------------|
| **5V** | 5.0V ± 5% | 200mA | Input from ATARI, USB |
| **3.3V** | 3.3V ± 3% | 400mA | ESP32, logic, interfaces |
| **1.2V** | 1.2V ± 3% | 300mA | FPGA core voltage |

#### Power Sources
1. **ATARI PBI**: Primary 5V supply from computer
2. **USB**: Alternative 5V for programming/testing
3. **External**: Optional external 5V supply (J5)

#### Power Protection
- **Schottky Diodes**: Reverse polarity protection
- **Ferrite Beads**: EMI filtering
- **Decoupling**: Extensive capacitor network
- **Current Limiting**: Overcurrent protection

</div>

---

<div style="background: #ffffff; border-radius: 16px; padding: 24px; margin: 24px 0; box-shadow: 0 4px 12px rgba(0,0,0,0.1); border: 1px solid #e0e0e0;">

## 🔨 Assembly Instructions

### Component Assembly

#### Assembly Order
1. **SMD Components**: Start with smallest components (0603, 0805)
2. **ICs**: Place all integrated circuits
3. **Connectors**: Install all external connectors
4. **Through-hole**: Add any remaining THT components
5. **Testing**: Verify power rails before first power-on

#### Special Considerations
- **FPGA**: Use proper ESD precautions
- **BGA Components**: Requires professional assembly
- **Thermal Management**: Ensure adequate cooling
- **Impedance Control**: Maintain 50Ω for high-speed signals

### Testing & Validation

#### Power-On Tests
1. **Visual Inspection**: Check for shorts, proper orientation
2. **Power Rails**: Verify all voltages within tolerance
3. **Clock Signals**: Confirm oscillator operation
4. **Communication**: Test SPI, I2C, UART interfaces

#### Functional Tests
1. **FPGA Programming**: Load test bitstream
2. **Video Output**: Verify VGA signal generation
3. **Audio Output**: Test audio codec functionality
4. **ATARI Interface**: Confirm PBI communication protocol

</div>

---

<div style="background: #ffffff; border-radius: 16px; padding: 24px; margin: 24px 0; box-shadow: 0 4px 12px rgba(0,0,0,0.1); border: 1px solid #e0e0e0;">

## 📦 Bill of Materials

### Component Categories

#### Major Components
- **1x** ICE40UP5K-SG48ITR FPGA
- **1x** ESP32-S3FN8 Microcontroller  
- **1x** W25Q16JVSNIQ SPI Flash
- **1x** WM8524CGEDT Audio Codec
- **1x** CH340T USB Interface

#### Passive Components
- **23x** 1µF Capacitors (1206)
- **20x** 100nF Capacitors (1206)
- **6x** 10µF Capacitors (1206)
- **Various** Resistors (0805/1206)
- **1x** 25MHz Crystal Oscillator

#### Connectors
- **1x** 15-pin VGA Connector
- **1x** Micro SD Card Slot
- **1x** USB Type-C Connector
- **1x** 3.5mm Audio Jack
- **1x** 8-pin Programming Header

### Cost Analysis

| Category | Estimated Cost | Percentage |
|----------|----------------|------------|
| **FPGA** | $15-20 | 35% |
| **ESP32** | $8-12 | 20% |
| **Connectors** | $5-8 | 15% |
| **Passives** | $3-5 | 10% |
| **PCB** | $8-12 | 20% |
| **Total** | **$39-57** | **100%** |

*Costs are estimates for small quantity production*

</div>

---

<div style="background: #ffffff; border-radius: 16px; padding: 24px; margin: 24px 0; box-shadow: 0 4px 12px rgba(0,0,0,0.1); border: 1px solid #e0e0e0;">

## 💻 Programming & Configuration

### FPGA Programming

#### Programming Methods
1. **USB/Serial**: Using FTDI 232H adapter interface with iceprog
2. **SPI Direct**: Direct SPI flash programming

#### Programming Steps
```bash
# Install icestorm toolchain
sudo apt install fpga-icestorm

# Program FPGA via USB (The FPGA bitstream is not provided!)
iceprog vera_module.bin

# Verify programming
iceprog -c
```

#### Development Tools
- **IceStorm**: Open-source FPGA toolchain
- **Yosys**: Verilog synthesis
- **NextPNR**: Place and route
- **KiCad**: PCB design and layout

### ESP32 Configuration

#### ESP32 Firmware
The ESP32-S3FN8 (Core 1 hot loop) handles:
- **Startup sync**: ARESET and CRESET asserted LOW immediately in `setup()` (before `Serial.begin`). `MonitorTask` releases CRESET, polls CDONE until the ICE40UP5K bitstream is loaded (< 100 ms typical, 5 s timeout), then releases ARESET — Atari starts with bus and FPGA already ready
- **Bus Decoding**: Full software decode of A0–A15 sampled on every PHI2 rising edge — no page ambiguity
- **Dedicated GPIO**: Control signals (EXTSEL\_N, DEV\_SEL\_N, MPD) asserted in a single Xtensa TIE instruction (`ee.wr_mask_gpio_out`, 1 CPU cycle) within the 279 ns PHI2-high window
- **Data Bus Drive** *(PBI mode only)*: D0–D7 driven via `GPIO.out` for ROM ($D800–$DFFF) and RAM ($D600–$D7FF) reads; FPGA drives the bus in CCTL mode
- **Debugging**: UART0 on GPIO43 (TX) / GPIO44 (RX), 115200 baud

The operating mode is selected at compile time via `VERA_BOARD_IS_PBI` in `main.cpp`:

| `VERA_BOARD_IS_PBI` | Mode | Active ranges | Selection latch |
|---------------------|------|---------------|-----------------|
| `1` (default) | PBI | $D100–$D1FF, $D600–$D7FF, $D800–$DFFF | write `0x80` to $D1FF |
| `0`           | CCTL | $D500–$D5FF                           | write `0x80` to $D5FF |

The **RAMbo 256 KB** emulator is always compiled in and enabled at runtime via **GPIO 3**
(`PIN_RAMBO_EN`, QFN56 pin 8) — no recompilation needed:

| GPIO 3 at boot | Effect |
|----------------|--------|
| Pull-up 10 kΩ → VCC (HIGH) | RAMbo enabled — 256 KB bank-switched at $4000–$7FFF; PORTB ($D301) bit 4 selects active/inactive |
| Pull-down 10 kΩ → GND (LOW) | RAMbo disabled — $4000–$7FFF not intercepted |

Works in both PBI and CCTL mode. `portb_rambo` initialises to `0xFF` (bit 4 = 1) so RAMbo
starts inactive even when enabled, until the OS writes $D301.

#### Programming ESP32
The firmware is built with **PlatformIO** (install via `pip install platformio`):
```bash
# Build firmware (edit VERA_BOARD_IS_PBI in main.cpp before building)
pio run -e esp32s3fn8

# Flash firmware — connect UART adapter to GPIO43/44
pio run -e esp32s3fn8 --target upload
```

> **Note**: USB CDC is disabled because GPIO 19 (USB D−) and GPIO 20 (USB D+) are reused
> as address lines A7 and A8. Use an external UART adapter on GPIO 43/44.

</div>

---

<div style="background: #ffffff; border-radius: 16px; padding: 24px; margin: 24px 0; box-shadow: 0 4px 12px rgba(0,0,0,0.1); border: 1px solid #e0e0e0;">

## 📁 Project Structure

### Repository Organization

```
Vera-Module-RBL-XE/
├── 📄 README.md                    # This documentation
├── 📄 LICENSE                      # GPL v3 license
├── ⚙️ VERA-MODULE-RBL.pro         # KiCad project file
├── 📄 VERA-MODULE-RBL.sch         # Main schematic
├── 🔧 VERA-MODULE-RBL.kicad_pcb   # PCB layout
├── 📄 VERA-MODULE-RBL.csv         # Bill of materials
├── 🐍 jlcpcb-check-bom.py        # BOM processing script
├── 📁 images/                      # Board images and renders
│   ├── 🖼️ VERA-MODULE-RBL-TOP.png
│   ├── 🖼️ VERA-MODULE-RBL-BOTTOM.png
│   ├── 🖼️ F.Cu-L1.png            # PCB layer images
│   ├── 🖼️ POWER-L2.png
│   ├── 🖼️ GND-L3.png
│   └── 🖼️ B.Cu-L4.png
├── 📁 production/                  # Manufacturing files
│   ├── 📄 *.gbl, *.gtl            # Gerber files
│   ├── 📄 *.drl                   # Drill files
│   └── 📄 *.gbrjob                # Job files
├── 📁 ibom/                       # Interactive BOM
│   └── 🌐 ibom.html
└── 📁 schematics/                 # Individual schematics
    ├── 📄 vera-fpga.sch           # FPGA module
    ├── 📄 busdecoder.sch          # Bus decoder
    ├── 📄 cartridgeInterface.sch   # Cartridge interface
    ├── 📄 vera-fpga-flash.sch     # Flash memory
    └── 📄 powersupply.sch         # Power management
```

### Design Files

#### KiCad Files
- **Schematic**: Multi-sheet hierarchical design
- **PCB Layout**: 4-layer board with controlled impedance
- **3D Models**: Custom component models
- **Footprints**: Custom connector footprints

#### Manufacturing Files
- **Gerber Files**: Industry-standard PCB fabrication
- **Drill Files**: NC drill and via specifications  
- **Pick & Place**: SMT assembly coordinates
- **BOM**: Component specifications with LCSC part numbers

</div>

---

<div style="background: #ffffff; border-radius: 16px; padding: 24px; margin: 24px 0; box-shadow: 0 4px 12px rgba(0,0,0,0.1); border: 1px solid #e0e0e0;">

## 🤝 Contributing

### Development Guidelines

#### Contributing Process
1. **Fork** the repository
2. **Create** a feature branch
3. **Make** your changes
4. **Test** thoroughly
5. **Submit** a pull request

#### Contribution Areas
- **Hardware Design**: PCB layout improvements
- **Firmware**: ESP32 and FPGA code
- **Documentation**: User guides and tutorials
- **Testing**: Validation and verification
- **Software**: Host applications and tools

#### Design Goals
- **Compatibility**: Maintain ATARI XE compatibility
- **Performance**: Optimize for speed and efficiency
- **Reliability**: Ensure robust operation
- **Manufacturability**: Design for production
- **Cost**: Balance features with affordability

</div>

---

<div style="background: #ffffff; border-radius: 16px; padding: 24px; margin: 24px 0; box-shadow: 0 4px 12px rgba(0,0,0,0.1); border: 1px solid #e0e0e0;">

## 📄 License

This project is licensed under the **GNU General Public License v3.0**.

### License Details

#### Permissions
- ✅ Commercial use
- ✅ Modification
- ✅ Distribution
- ✅ Patent use
- ✅ Private use

#### Limitations
- ❌ Liability
- ❌ Warranty

#### Conditions
- 📄 License and copyright notice
- 📄 State changes
- 📄 Disclose source
- 📄 Same license

For the complete license text, see the [LICENSE](LICENSE) file.

</div>

---

<div style="background: linear-gradient(135deg, #1976d2, #42a5f5); color: white; border-radius: 16px; padding: 32px; margin: 32px 0; box-shadow: 0 8px 24px rgba(25,118,210,0.3); text-align: center;">

## 💙 Support the Project

If you find this project useful, please consider:

🌟 **Starring** the repository  
🐛 **Reporting** issues  
🔧 **Contributing** improvements  
📢 **Sharing** with the community  

---

**Made with ❤️ by RetroBit Lab**

*Bringing modern capabilities to classic computers*

</div>

---

# VERA Module RBL-XE (Italiano)

<div align="center">

*[🌐 English](#vera-module-rbl-xe) | 🌐 Italiano*

</div>

---

<div style="background: #ffffff; border-radius: 16px; padding: 24px; margin: 24px 0; box-shadow: 0 4px 12px rgba(0,0,0,0.1); border: 1px solid #e0e0e0;">

## 📋 Indice

- [🔍 Panoramica](#-panoramica)
- [✨ Caratteristiche](#-caratteristiche)
- [⚙️ Specifiche Tecniche](#️-specifiche-tecniche)
- [🔧 Componenti Hardware](#-componenti-hardware)
- [🖼️ Anteprima Scheda](#️-anteprima-scheda)
- [📐 Layer PCB](#-layer-pcb)
- [🔌 Connettori e Interfacce](#-connettori-e-interfacce)
- [⚡ Requisiti di Alimentazione](#-requisiti-di-alimentazione)
- [🔨 Istruzioni di Assemblaggio](#-istruzioni-di-assemblaggio)
- [📦 Distinta Materiali](#-distinta-materiali)
- [💻 Programmazione e Configurazione](#-programmazione-e-configurazione)
- [📁 Struttura Progetto](#-struttura-progetto)
- [🤝 Contribuire](#-contribuire)
- [📄 Licenza](#-licenza-1)

</div>

---

<div style="background: #ffffff; border-radius: 16px; padding: 24px; margin: 24px 0; box-shadow: 0 4px 12px rgba(0,0,0,0.1); border: 1px solid #e0e0e0;">

## 🔍 Panoramica

Il **VERA Module RBL-XE** è una sofisticata scheda di espansione audio e video 
basata su FPGA, progettata specificamente per i computer **ATARI XE**. 
Questo modulo porta capacità grafiche e audio moderne ai sistemi ATARI classici 
attraverso i connettori cartuccia ed ECI per il supporto del protocollo **PBI (Parallel Bus Interface)**.

### Cos'è VERA?

**VERA** (Video Enhanced Retro Adapter) è originariamente progettato per il 
computer Commander X16 ma è stato adattato per i sistemi ATARI. Fornisce:

- **Grafica Avanzata**: Livelli video multipli, sprite e modalità ad alta risoluzione
- **Audio Migliorato**: Audio digitale multicanale con capacità PCM e PSG
- **Connettività Moderna**: Uscita VGA, storage su scheda SD e programmazione USB
- **Compatibilità Retro**: Integrazione con il software ATARI classico tramite semplici drivers

</div>

---

<div style="background: #ffffff; border-radius: 16px; padding: 24px; margin: 24px 0; box-shadow: 0 4px 12px rgba(0,0,0,0.1); border: 1px solid #e0e0e0;">

## ✨ Caratteristiche

### 🎨 Capacità Grafiche

#### Uscita Video
- **Uscita VGA**: 640x480 @ 60Hz con segnali RGB analogici
- **Livelli Multipli**: Livelli di sfondo, primo piano e sprite
- **Profondità Colore**: 256 colori da una palette di 4096
- **Sprite**: Motore sprite accelerato hardware
- **Scrolling**: Scorrimento orizzontale e verticale fluido

#### Caratteristiche Audio
- **Audio Digitale**: Uscita audio PCM di alta qualità
- **Multicanale**: Audio stereo
- **Codec Audio**: WM8524 per qualità audio professionale
- **Jack 3.5mm**: Connettore uscita audio standard

### 🔗 Integrazione Sistema

#### Compatibilità ATARI
- **Interfaccia PBI**: Connessione diretta ai sistemi ATARI XE/XL
- **Mappatura Memoria**: Spazio indirizzi $D1XX-$DFXX
- **Decodificatore Bus**: Decodifica software completa degli indirizzi nell'ESP32 — A0–A15 campionati ad ogni ciclo PHI2, nessun hardware decoder esterno richiesto
- **Gestione Alimentazione**: Conversione efficiente da 5V a 3.3V/1.2V

#### Storage e Programmazione
- **Slot Scheda SD**: Micro SD per storage dati e caricamento ROM
- **Flash SPI**: 16Mbit W25Q16 per configurazione FPGA
- **Programmazione USB**: CH340 USB-to-serial per aggiornamenti facili
- **SPI**: Interfaccia programmazione FPGA diretta

</div>

---

<div style="background: #ffffff; border-radius: 16px; padding: 24px; margin: 24px 0; box-shadow: 0 4px 12px rgba(0,0,0,0.1); border: 1px solid #e0e0e0;">

## ⚙️ Specifiche Tecniche

### Specifiche Principali

| Componente | Specifica |
|------------|-----------|
| **FPGA** | Lattice ICE40UP5K-SG48ITR |
| **Microcontrollore** | ESP32-S3FN8 |
| **Memoria Flash** | W25Q16JVSNIQ (16Mbit) |
| **Codec Audio** | WM8524CGEDT |
| **Interfaccia USB** | CH340T |
| **Regolatori Alimentazione** | AMS1117-3.3V, AMS1117-1.2V, L7805 |
| **Oscillatore Cristallo** | 25MHz ECS-2033-250-BN |
| **Layer PCB** | 4 layer (Segnale/Alimentazione/GND/Segnale) |
| **Dimensioni Scheda** | TBD mm x TBD mm |

### Caratteristiche Elettriche

| Parametro | Min | Tip | Max | Unità |
|-----------|-----|-----|-----|-------|
| **Tensione Alimentazione** | 4.5 | 5.0 | 5.5 | V |
| **Consumo Corrente** | - | 800 | 1200 | mA |
| **Temperatura Operativa** | -10 | 25 | 70 | °C |
| **Impedenza Uscita VGA** | - | 75 | - | Ω |
| **Livello Uscita Audio** | - | 2.0 | - | Vrms |

</div>

---

<div style="background: #ffffff; border-radius: 16px; padding: 24px; margin: 24px 0; box-shadow: 0 4px 12px rgba(0,0,0,0.1); border: 1px solid #e0e0e0;">

## 🔧 Componenti Hardware

### Unità di Elaborazione Principali

#### FPGA - ICE40UP5K
- **Elementi Logici**: 5,280 LUT
- **Memoria**: 120 Kbit BRAM, 1 Mbit SPRAM
- **Pin I/O**: 39 I/O utente
- **Package**: QFN-48 (7x7mm)
- **Programmazione**: Configurazione SPI

#### ESP32-S3FN8
- **CPU**: Dual-core Xtensa LX7 @ 240 MHz
- **Memoria**: 8 MB flash Quad SPI integrata, 512 KB SRAM
- **Connettività**: Wi-Fi 802.11 b/g/n, Bluetooth 5 LE
- **GPIO**: 45 totali (38 usabili), tutti bidirezionali
- **Package**: QFN-56 (7×7 mm)
- **Ruolo**: Controllore bus 6502 completamente software — campiona A0–A15, PHI2, R/W ad ogni ciclo e pilota EXTSEL\_N, DEV\_SEL\_N, MPD tramite istruzioni **Dedicated GPIO** Xtensa TIE (1 ciclo CPU) nella finestra PHI2-high di 279 ns; gestisce PBI ($D1xx) e CCTL ($D5xx) con un unico firmware unificato

### Componenti Interfaccia

#### Sistema Audio
- **Codec**: WM8524CGEDT (24-bit, 192kHz)
- **Interfaccia**: Audio digitale dalla FPGA con bus dedicato
- **Uscita**: Jack stereo 3.5mm
- **SNR**: >100dB

#### Sistema Video
- **Uscita**: Connettore VGA 15-pin
- **Risoluzione**: 640x480 @ 60Hz
- **Colori**: RGB 8-bit (256 colori)
- **Sync**: Segnali H/V sync separati

</div>

---

<div style="background: #ffffff; border-radius: 16px; padding: 24px; margin: 24px 0; box-shadow: 0 4px 12px rgba(0,0,0,0.1); border: 1px solid #e0e0e0;">

## 🖼️ Anteprima Scheda

### Rendering 3D

#### Vista Superiore
![VERA Module Top](images/VERA-MODULE-RBL-TOP.png)
*Lato superiore mostrando componenti principali e connettori*

#### Vista Inferiore
![VERA Module Bottom](images/VERA-MODULE-RBL-BOTTOM.png)
*Lato inferiore con componenti aggiuntivi*

#### Adattatore Cartuccia
![VERA Cartridge Adapter](images/VERA-ADAPTER-CARTRIDGE.png)
*Adattatore interfaccia cartuccia opzionale*

### Dimensioni Scheda

- **Lunghezza**: TBD mm
- **Larghezza**: TBD mm  
- **Spessore**: 1.6mm (PCB 4-layer)
- **Montaggio**: Compatibile con slot ATARI XE CART + ECI
- **Connettori**: Piatti per efficienza spazio. Necessita di case stampato in 3D

</div>

---

<div style="background: #ffffff; border-radius: 16px; padding: 24px; margin: 24px 0; box-shadow: 0 4px 12px rgba(0,0,0,0.1); border: 1px solid #e0e0e0;">

## 📐 Layer PCB

### Stack-up Layer

#### Layer 1 - Rame Superiore (F.Cu)
![Top Copper Layer](images/F.Cu-L1.png)
*Posizionamento componenti e routing segnali*

#### Layer 2 - Piano Alimentazione (POWER)
![Power Layer](images/POWER-L2.png)
*Distribuzione alimentazione 5V, 3.3V e 1.2V*

#### Layer 3 - Piano Massa (GND)
![Ground Layer](images/GND-L3.png)
*Piano massa solido per integrità segnale*

#### Layer 4 - Rame Inferiore (B.Cu)
![Bottom Copper Layer](images/B.Cu-L4.png)
*Routing segnali aggiuntivo e componenti*

</div>

---

<div style="background: #ffffff; border-radius: 16px; padding: 24px; margin: 24px 0; box-shadow: 0 4px 12px rgba(0,0,0,0.1); border: 1px solid #e0e0e0;">

## 🔌 Connettori e Interfacce

### Connessioni Esterne

#### Interfaccia ATARI
- **Connettore PBI**: Connettore edge 50-pin
- **Slot ECI**: Enhanced Cartridge Interface
- **Range Indirizzi**: $D1XX-$DFXX
- **Segnali**: Bus dati, bus indirizzi, segnali controllo

#### Uscita Video (J2)
| Pin | Segnale | Descrizione |
|-----|---------|-------------|
| 1-3 | RGB | Segnali analogici Rosso, Verde, Blu |
| 13-14 | H/V Sync | Sync orizzontale/verticale |
| 6-10 | GND | Connessioni massa |
| 11-12 | Audio L/R | Uscita audio stereo |

#### Interfacce Storage
- **Slot Scheda SD (J3)**: Interfaccia scheda Micro SD
- **Flash SPI**: Storage on-board 16Mbit
- **Porta USB (J4)**: Programmazione e trasferimento dati

### Interfacce Programmazione

#### Connettore ICEPROG (J6)
| Pin | Segnale | Funzione |
|-----|--------|----------|
| 1 | 5V | Alimentazione |
| 2 | FPGA_CDONE/PULL-UP | FPGA Programmata |
| 3 | FPGA_RESET | FPGA Reset |
| 4 | MISO | Dati SPI in ingresso |
| 5 | MOSI | Dati SPI in uscita |
| 6 | SCK | Clock SPI |
| 7 | SS | Selezione SPI |
| 8 | GND | Ground |

#### Reset e Controllo
- **Pulsante Reset (SW1)**: Reset sistema ESP32
- **DIP Switch (SW2)**: Opzioni configurazione
- **LED di Stato**: Indicatori alimentazione, programmazione, attività

</div>

---

<div style="background: #ffffff; border-radius: 16px; padding: 24px; margin: 24px 0; box-shadow: 0 4px 12px rgba(0,0,0,0.1); border: 1px solid #e0e0e0;">

## ⚡ Requisiti di Alimentazione

### Distribuzione Alimentazione

#### Rail Alimentazione
| Rail | Tensione | Corrente | Componenti |
|------|----------|----------|------------|
| **5V** | 5.0V ± 5% | 200mA | Ingresso da ATARI, USB |
| **3.3V** | 3.3V ± 3% | 400mA | ESP32, logica, interfacce |
| **1.2V** | 1.2V ± 3% | 300mA | Tensione core FPGA |

#### Sorgenti Alimentazione
1. **PBI ATARI**: Alimentazione 5V primaria dal computer
2. **USB**: 5V alternativo per programmazione/test
3. **Esterno**: Alimentazione 5V esterna opzionale (J5)

#### Protezione Alimentazione
- **Diodi Schottky**: Protezione polarità inversa
- **Perle di Ferrite**: Filtraggio EMI
- **Disaccoppiamento**: Rete estesa di condensatori
- **Limitazione Corrente**: Protezione sovracorrente

</div>

---

<div style="background: #ffffff; border-radius: 16px; padding: 24px; margin: 24px 0; box-shadow: 0 4px 12px rgba(0,0,0,0.1); border: 1px solid #e0e0e0;">

## 🔨 Istruzioni di Assemblaggio

### Assemblaggio Componenti

#### Ordine Assemblaggio
1. **Componenti SMD**: Iniziare con componenti più piccoli (0603, 0805)
2. **IC**: Posizionare tutti i circuiti integrati
3. **Connettori**: Installare tutti i connettori esterni
4. **Through-hole**: Aggiungere eventuali componenti THT rimanenti
5. **Test**: Verificare rail alimentazione prima della prima accensione

#### Considerazioni Speciali
- **FPGA**: Usare precauzioni ESD appropriate
- **Componenti BGA**: Richiede assemblaggio professionale
- **Gestione Termica**: Assicurare raffreddamento adeguato
- **Controllo Impedenza**: Mantenere 50Ω per segnali alta velocità

### Test e Validazione

#### Test Accensione
1. **Ispezione Visiva**: Controllare cortocircuiti, orientamento corretto
2. **Rail Alimentazione**: Verificare tutte le tensioni entro tolleranza
3. **Segnali Clock**: Confermare funzionamento oscillatore
4. **Comunicazione**: Testare interfacce SPI, I2C, UART

#### Test Funzionali
1. **Programmazione FPGA**: Caricare bitstream di test
2. **Uscita Video**: Verificare generazione segnale VGA
3. **Uscita Audio**: Testare funzionalità codec audio
4. **Interfaccia ATARI**: Confermare comunicazione con protocollo PBI

</div>

---

<div style="background: #ffffff; border-radius: 16px; padding: 24px; margin: 24px 0; box-shadow: 0 4px 12px rgba(0,0,0,0.1); border: 1px solid #e0e0e0;">

## 📦 Distinta Materiali

### Categorie Componenti

#### Componenti Principali
- **1x** ICE40UP5K-SG48ITR FPGA
- **1x** ESP32-S3FN8 Microcontrollore
- **1x** W25Q16JVSNIQ Flash SPI
- **1x** WM8524CGEDT Codec Audio
- **1x** CH340T Interfaccia USB

#### Componenti Passivi
- **23x** Condensatori 1µF (1206)
- **20x** Condensatori 100nF (1206)
- **6x** Condensatori 10µF (1206)
- **Vari** Resistori (0805/1206)
- **1x** Oscillatore Cristallo 25MHz

#### Connettori
- **1x** Connettore VGA 15-pin
- **1x** Slot Scheda Micro SD
- **1x** Connettore USB Type-C
- **1x** Jack Audio 3.5mm
- **1x** Header Programmazione 8-pin

### Analisi Costi

| Categoria | Costo Stimato | Percentuale |
|-----------|---------------|-------------|
| **FPGA** | $15-20 | 35% |
| **ESP32** | $8-12 | 20% |
| **Connettori** | $5-8 | 15% |
| **Passivi** | $3-5 | 10% |
| **PCB** | $8-12 | 20% |
| **Totale** | **$39-57** | **100%** |

*I costi sono stime per produzione piccole quantità*

</div>

---

<div style="background: #ffffff; border-radius: 16px; padding: 24px; margin: 24px 0; box-shadow: 0 4px 12px rgba(0,0,0,0.1); border: 1px solid #e0e0e0;">

## 💻 Programmazione e Configurazione

### Programmazione FPGA

#### Metodi Programmazione
1. **USB/Seriale**: Usando interfaccia FTDI 232H per SPI USB con iceprog
2. **SPI Diretto**: Programmazione flash SPI diretta

#### Passi Programmazione
```bash
# Installare toolchain icestorm
sudo apt install fpga-icestorm

# Programmare FPGA via USB (Il bitstream FPGA non e' fornito!)
iceprog vera_module.bin

# Verificare programmazione
iceprog -c
```

#### Strumenti Sviluppo
- **IceStorm**: Toolchain FPGA open-source
- **Yosys**: Sintesi Verilog
- **NextPNR**: Place and route
- **KiCad**: Design e layout PCB

### Configurazione ESP32

#### Firmware ESP32
L'ESP32-S3FN8 (hot loop sul Core 1) gestisce:
- **Sincronizzazione startup**: ARESET e CRESET abbassati subito in `setup()` (prima di `Serial.begin`). `MonitorTask` rilascia CRESET, attende CDONE = HIGH (bitstream ICE40UP5K caricato, tipicamente < 100 ms, timeout 5 s), poi rilascia ARESET — l'Atari parte con bus e FPGA già operativi
- **Decodifica Bus**: Decodifica software completa di A0–A15 campionati ad ogni fronte di salita di PHI2 — nessuna ambiguità di pagina, nessun hardware decoder esterno
- **Dedicated GPIO**: I segnali di controllo (EXTSEL\_N, DEV\_SEL\_N, MPD) vengono affermati in una singola istruzione Xtensa TIE (`ee.wr_mask_gpio_out`, 1 ciclo CPU) nella finestra PHI2-high di 279 ns
- **Bus Dati** *(solo modalità PBI)*: D0–D7 pilotati via `GPIO.out` per letture ROM ($D800–$DFFF) e RAM ($D600–$D7FF); in modalità CCTL è la FPGA a guidare il bus
- **Debug**: UART0 su GPIO43 (TX) / GPIO44 (RX), 115200 baud

La modalità operativa è scelta a **tempo di compilazione** tramite `VERA_BOARD_IS_PBI` in `main.cpp`:

| `VERA_BOARD_IS_PBI` | Modalità | Range attivi | Latch selezione |
|---------------------|----------|--------------|-----------------|
| `1` (default) | PBI  | $D100–$D1FF, $D600–$D7FF, $D800–$DFFF | write `0x80` su $D1FF |
| `0`           | CCTL | $D500–$D5FF                           | write `0x80` su $D5FF |

L'emulatore **RAMbo 256 KB** è sempre compilato nel firmware e abilitato a runtime tramite
**GPIO 3** (`PIN_RAMBO_EN`, QFN56 pin 8) — nessuna ricompilazione necessaria:

| GPIO 3 al boot | Effetto |
|----------------|---------|
| Pull-up 10 kΩ → VCC (HIGH) | RAMbo abilitato — 256 KB bank-switched a $4000–$7FFF; bit 4 di PORTB ($D301) seleziona attivo/inattivo |
| Pull-down 10 kΩ → GND (LOW) | RAMbo disabilitato — $4000–$7FFF non intercettato |

Funziona in entrambe le modalità PBI e CCTL. `portb_rambo` si inizializza a `0xFF`
(bit 4 = 1) quindi RAMbo parte inattivo anche se abilitato, finché l'OS non scrive $D301.

#### Programmazione ESP32
Il firmware viene compilato con **PlatformIO** (installare con `pip install platformio`):
```bash
# Compila firmware (modificare VERA_BOARD_IS_PBI in main.cpp prima di compilare)
pio run -e esp32s3fn8

# Carica firmware — adattatore UART su GPIO43/44
pio run -e esp32s3fn8 --target upload
```

> **Nota**: USB CDC è disabilitato perché GPIO 19 (USB D−) e GPIO 20 (USB D+) sono
> riutilizzati come linee indirizzo A7 e A8. Usare un adattatore UART esterno su GPIO 43/44.

</div>

---

<div style="background: #ffffff; border-radius: 16px; padding: 24px; margin: 24px 0; box-shadow: 0 4px 12px rgba(0,0,0,0.1); border: 1px solid #e0e0e0;">

## 📁 Struttura Progetto

### Organizzazione Repository

```
Vera-Module-RBL-XE/
├── 📄 README.md                    # Questa documentazione
├── 📄 LICENSE                      # Licenza GPL v3
├── ⚙️ VERA-MODULE-RBL.pro         # File progetto KiCad
├── 📄 VERA-MODULE-RBL.sch         # Schema principale
├── 🔧 VERA-MODULE-RBL.kicad_pcb   # Layout PCB
├── 📄 VERA-MODULE-RBL.csv         # Distinta materiali
├── 🐍 jlcpcb-check-bom.py        # Script elaborazione BOM
├── 📁 images/                      # Immagini scheda e render
│   ├── 🖼️ VERA-MODULE-RBL-TOP.png
│   ├── 🖼️ VERA-MODULE-RBL-BOTTOM.png
│   ├── 🖼️ F.Cu-L1.png            # Immagini layer PCB
│   ├── 🖼️ POWER-L2.png
│   ├── 🖼️ GND-L3.png
│   └── 🖼️ B.Cu-L4.png
├── 📁 production/                  # File produzione
│   ├── 📄 *.gbl, *.gtl            # File Gerber
│   ├── 📄 *.drl                   # File drill
│   └── 📄 *.gbrjob                # File job
├── 📁 ibom/                       # BOM interattiva
│   └── 🌐 ibom.html
└── 📁 schematics/                 # Schemi individuali
    ├── 📄 vera-fpga.sch           # Modulo FPGA
    ├── 📄 busdecoder.sch          # Decodificatore bus
    ├── 📄 cartridgeInterface.sch   # Interfaccia cartuccia
    ├── 📄 vera-fpga-flash.sch     # Memoria flash
    └── 📄 powersupply.sch         # Gestione alimentazione
```

### File Design

#### File KiCad
- **Schema**: Design gerarchico multi-foglio
- **Layout PCB**: Scheda 4-layer con impedenza controllata
- **Modelli 3D**: Modelli componenti personalizzati
- **Footprint**: Footprint connettori personalizzati

#### File Produzione
- **File Gerber**: Fabbricazione PCB standard industria
- **File Drill**: Specifiche drill NC e via
- **Pick & Place**: Coordinate assemblaggio SMT
- **BOM**: Specifiche componenti con codici LCSC

</div>

---

<div style="background: #ffffff; border-radius: 16px; padding: 24px; margin: 24px 0; box-shadow: 0 4px 12px rgba(0,0,0,0.1); border: 1px solid #e0e0e0;">

## 🤝 Contribuire

### Linee Guida Sviluppo

#### Processo Contribuzione
1. **Fork** del repository
2. **Creare** un branch feature
3. **Apportare** le modifiche
4. **Testare** accuratamente
5. **Inviare** una pull request

#### Aree Contribuzione
- **Design Hardware**: Miglioramenti layout PCB
- **Firmware**: Codice ESP32 e FPGA
- **Documentazione**: Guide utente e tutorial
- **Test**: Validazione e verifica
- **Software**: Applicazioni host e strumenti

#### Obiettivi Design
- **Compatibilità**: Mantenere compatibilità ATARI XE/XL
- **Performance**: Ottimizzare per velocità ed efficienza
- **Affidabilità**: Assicurare funzionamento robusto
- **Producibilità**: Design per produzione
- **Costo**: Bilanciare caratteristiche con convenienza

</div>

---

<div style="background: #ffffff; border-radius: 16px; padding: 24px; margin: 24px 0; box-shadow: 0 4px 12px rgba(0,0,0,0.1); border: 1px solid #e0e0e0;">

## 📄 Licenza

Questo progetto è licenziato sotto la **GNU General Public License v3.0**.

### Dettagli Licenza

#### Permessi
- ✅ Uso commerciale
- ✅ Modifica
- ✅ Distribuzione
- ✅ Uso brevetti
- ✅ Uso privato

#### Limitazioni
- ❌ Responsabilità
- ❌ Garanzia

#### Condizioni
- 📄 Avviso licenza e copyright
- 📄 Dichiarare modifiche
- 📄 Rivelare sorgente
- 📄 Stessa licenza

Per il testo completo della licenza, vedere il file [LICENSE](LICENSE).

</div>

---

<div style="background: linear-gradient(135deg, #1976d2, #42a5f5); color: white; border-radius: 16px; padding: 32px; margin: 32px 0; box-shadow: 0 8px 24px rgba(25,118,210,0.3); text-align: center;">

## 💙 Supporta il Progetto

Se trovi utile questo progetto, considera di:

🌟 **Mettere una stella** al repository  
🐛 **Segnalare** problemi  
🔧 **Contribuire** miglioramenti  
📢 **Condividere** con la community  

---

**Realizzato con ❤️ da RetroBit Lab**

*Portando capacità moderne ai computer classici*

</div>
