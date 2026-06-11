# =============================================================================
# Makefile -- iCE40UP5K FPGA build (VHDL via GHDL + Yosys + nextpnr)
#
# Requirements:
#   ghdl              https://github.com/ghdl/ghdl
#   yosys             https://github.com/YosysHQ/yosys
#   ghdl-yosys-plugin https://github.com/ghdl/ghdl-yosys-plugin
#   nextpnr-ice40     https://github.com/YosysHQ/nextpnr
#   icestorm          https://github.com/YosysHQ/icestorm  (icepack, iceprog)
#   openFPGALoader    https://github.com/trabucayre/openFPGALoader
#
# Usage:
#   make            -- synthesise, place & route, pack bitstream
#   make flash      -- write bitstream to SPI flash via openFPGALoader
#   make flash-iceprog -- alternative: use iceprog (FTDI FT2232H)
#   make clean      -- remove all generated files
# =============================================================================

TOP    = C6502_Interface
PCF    = board-pin-mapping.pcf
DEVICE = up5k
PKG    = sg48

VHDL_SRCS = \
    custom_types.vhd \
    pbi_rom_pkg.vhd \
    C6502_Interface.vhd

GHDL_STD = --std=08

DOC = build-and-programming-guide

.PHONY: all flash flash-iceprog doc clean

all: $(TOP).bin

# -----------------------------------------------------------------------------
# Synthesis: VHDL -> JSON netlist
# Uses the GHDL plugin loaded into Yosys (-m ghdl).
# -----------------------------------------------------------------------------
$(TOP).json: $(VHDL_SRCS)
	yosys -m ghdl -p \
	    "ghdl $(GHDL_STD) $(VHDL_SRCS) -e $(TOP); \
	     synth_ice40 -top $(TOP) -json $@"

# -----------------------------------------------------------------------------
# Place & Route: JSON netlist -> ASCII bitstream
# -----------------------------------------------------------------------------
$(TOP).asc: $(TOP).json $(PCF)
	nextpnr-ice40 \
	    --$(DEVICE) \
	    --package $(PKG) \
	    --json $< \
	    --pcf $(PCF) \
	    --asc $@

# -----------------------------------------------------------------------------
# Bitstream packing: ASCII -> binary
# -----------------------------------------------------------------------------
$(TOP).bin: $(TOP).asc
	icepack $< $@

# -----------------------------------------------------------------------------
# Documentation
# -----------------------------------------------------------------------------
doc: $(DOC).pdf

$(DOC).pdf: $(DOC).tex
	pdflatex -interaction=nonstopmode $<
	pdflatex -interaction=nonstopmode $<

# -----------------------------------------------------------------------------
# Flash programming
# -----------------------------------------------------------------------------
flash: $(TOP).bin
	openFPGALoader -b ice40_generic $<

flash-iceprog: $(TOP).bin
	iceprog $<

# -----------------------------------------------------------------------------
# Cleanup
# -----------------------------------------------------------------------------
clean:
	$(RM) $(TOP).json $(TOP).asc $(TOP).bin
	$(RM) $(DOC).aux $(DOC).log $(DOC).out $(DOC).toc $(DOC).pdf
