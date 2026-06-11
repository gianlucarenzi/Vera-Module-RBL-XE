# FPGA Firmware — iCE40UP5K

6502 bus controller for the VERA RBL-XE module.  Implements PBI RAM, PBI ROM
(BRAM), RAMbo 256 KB bank-switched memory via external SRAM, and all associated
PBI control signals — replacing the ESP32-S3 MCU that handled the bus in the
previous design.

## Memory map

| Range | Size | Function | Implementation |
|-------|------|----------|----------------|
| `$4000–$7FFF` | 16 KB window | RAMbo (bank-switched, 256 KB total) | External SRAM |
| `$D1FF` | 1 B | VERA_CS latch (write only) | Register |
| `$D301` | 1 B | PIA PORTB snoop (bank select) | Register |
| `$D303` | 1 B | PIA PBCTL snoop (RAMbo enable) | Register |
| `$D600–$D7FF` | 512 B | PBI RAM | BRAM (1 EBR block) |
| `$D800–$DFFF` | 2 KB | PBI ROM (6502 driver) | BRAM (4 EBR blocks) |

**BRAM usage**: 5 of 30 EBR blocks on the iCE40UP5K.

## Hardware

### FPGA
**Lattice iCE40UP5K-SG48** (QFN-48, 39 user I/O, 5280 LUTs, 120 Kbit BRAM,
1 Mbit SPRAM).

### External SRAM — RAMbo 256 KB
Any **512K×8 asynchronous SRAM**, 55 ns or faster, 5 V tolerant.
Recommended: **AS6C4008-55SIN** (Alliance Memory, SOP-32).

| SRAM pin | Connection |
|----------|-----------|
| A[13:0] | 6502 A[13:0] — **direct PCB net**, no FPGA I/O pin used |
| A[17:14] | FPGA `SRAM_A_BANK[3:0]` (bank register) |
| A18 | **GND** (use lower 256 KB of the 512 KB chip) |
| DQ[7:0] | 6502 D[7:0] — **direct PCB net**, shared with FPGA D[7:0] |
| CE# | FPGA `SRAM_CE_N` |
| OE# | FPGA `SRAM_OE_N` |
| WE# | FPGA `SRAM_WE_N` |

This wiring uses only **7 new FPGA pins** for the full 256 KB RAMbo.

### SPI flash (FPGA configuration)
Any **SPI NOR flash** compatible with the iCE40 boot protocol, e.g.
Winbond W25Q16 (2 MB) or W25Q32 (4 MB).

The flash is shared between the FPGA (reads bitstream at power-up) and the
programmer (writes the bitstream).  During programming the FPGA is held in
reset via `CRESET_B`.

### Programmer hardware
Any of the following work with `openFPGALoader` or `iceprog`:

| Hardware | Interface | Tool |
|----------|-----------|------|
| FTDI FT232H breakout | SPI bitbang | `iceprog`, `openFPGALoader` |
| FTDI FT2232H breakout | MPSSE SPI | `iceprog`, `openFPGALoader` |
| Tigard | JTAG/SPI | `openFPGALoader` |
| Raspberry Pi (GPIO) | SPI bitbang | `openFPGALoader` |
| dirtyJTAG (STM32F103) | JTAG | `openFPGALoader` |

Programmer SPI wiring to SPI flash and FPGA:

```
Programmer    iCE40UP5K     W25Qxx flash
MOSI  ──────── SPI_SI ────── DI
MISO  ──────── SPI_SO ────── DO
SCK   ──────── SPI_SCK ───── CLK
CS#   ─────────────────────── CS#
CRST# ──────── CRESET_B
CDONE ──────── CDONE
```

---

## Toolchain

### Required packages

```bash
# Debian / Ubuntu
sudo apt install fpga-icestorm nextpnr-ice40 yosys ghdl
```

The `ghdl-yosys-plugin` is required for VHDL synthesis and is usually not
packaged — build it from source:

```bash
git clone https://github.com/ghdl/ghdl-yosys-plugin
cd ghdl-yosys-plugin && make && sudo make install
```

For flashing:

```bash
# openFPGALoader (recommended)
sudo apt install openFPGALoader

# iceprog is included in fpga-icestorm (installed above)
```

### Tool roles

| Tool | Input | Output | Purpose |
|------|-------|--------|---------|
| `ghdl` | VHDL sources | RTLIL | VHDL elaboration (via Yosys plugin) |
| `yosys` | RTLIL | `.json` netlist | Synthesis for iCE40 |
| `nextpnr-ice40` | `.json` + `.pcf` | `.asc` | Place & route |
| `icepack` | `.asc` | `.bin` | Pack ASCII bitstream into binary |
| `openFPGALoader` / `iceprog` | `.bin` | SPI flash | Write bitstream to board |

---

## Build

### 1. Build the 6502 ROM driver and generate the VHDL package

The PBI ROM (`$D800–$DFFF`) is pre-loaded into BRAM at synthesis time.
Its content comes from the assembled 6502 driver binary.

```bash
cd firmware/MCU/6502
make            # assembles vera_pbi_handler.s and generates:
                #   vera_pbi_handler.bin
                #   ../include/vera_pbi_handler.h
                #   ../../FPGA/pbi_rom_pkg.vhd   <-- VHDL ROM package
```

To regenerate only the VHDL package (when the binary already exists):

```bash
make fpga-rom
```

### 2. Synthesise, place & route, pack bitstream

```bash
cd firmware/FPGA
make
```

This runs the full flow and produces `C6502_Interface.bin`.

Step by step:

```bash
# Synthesis: VHDL -> JSON netlist
yosys -m ghdl -p \
    "ghdl --std=08 custom_types.vhd pbi_rom_pkg.vhd C6502_Interface.vhd \
          -e C6502_Interface; \
     synth_ice40 -top C6502_Interface -json C6502_Interface.json"

# Place & route: JSON + PCF -> ASCII bitstream
nextpnr-ice40 --up5k --package sg48 \
    --json C6502_Interface.json \
    --pcf board-pin-mapping.pcf \
    --asc C6502_Interface.asc

# Pack binary bitstream  <-- this is what gets flashed
icepack C6502_Interface.asc C6502_Interface.bin
```

### 3. Flash the bitstream

```bash
# openFPGALoader (recommended, supports many programmers)
make flash
# equivalent to:
openFPGALoader -b ice40_generic C6502_Interface.bin

# iceprog (FTDI FT2232H)
make flash-iceprog
# equivalent to:
iceprog C6502_Interface.bin
```

### 4. Clean

```bash
make clean      # removes .json, .asc, .bin
```

---

## Pin assignment

Edit `board-pin-mapping.pcf` to match your schematic before running
`nextpnr-ice40`.  All pin numbers in the file are **placeholders**.

The CLK signal must be assigned to a **Global Buffer (GB) input** pin.
Valid GB pins on the iCE40UP5K SG48 package: 20, 28, 31, 38, 41, 43, 48.

---

## Source files

| File | Description |
|------|-------------|
| `C6502_Interface.vhd` | Top-level entity (Rev 2) |
| `custom_types.vhd` | BRAM array type definitions |
| `pbi_rom_pkg.vhd` | PBI ROM contents as VHDL constant (**auto-generated**) |
| `gen_pbi_rom_pkg.py` | Script: converts `.bin` → `pbi_rom_pkg.vhd` |
| `board-pin-mapping.pcf` | Pin constraints for iCE40UP5K SG48 (edit before use) |
| `Makefile` | Build and flash targets |

---

## RAMbo bank formula

The FPGA snoops writes to the Atari PIA registers to track the active bank:

```
PORTB  at $D301  — bank bits
PBCTL  at $D303  — bit 2 enables RAMbo

bank[1:0] = PORTB[3:2]
bank[3:2] = PORTB[6:5]
→ SRAM A[17:14] = { PORTB[6], PORTB[5], PORTB[3], PORTB[2] }
```

RAMbo is active only when `PBCTL[2] = 1`.
