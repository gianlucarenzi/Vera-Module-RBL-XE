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
# Two bitstreams are produced from the same VHDL via the VERA_IS_PBI generic:
#
#   make pbi           -- build C6502_Interface_PBI.bin  (PBI connector, $D1xx)
#   make cctl          -- build C6502_Interface_CCTL.bin (cartridge port, $D5xx)
#   make               -- build both (default)
#
#   make flash-pbi     -- flash PBI  bitstream via openFPGALoader
#   make flash-cctl    -- flash CCTL bitstream via openFPGALoader
#   make flash-pbi-iceprog   -- flash PBI  via iceprog (FTDI FT2232H)
#   make flash-cctl-iceprog  -- flash CCTL via iceprog
#
#   make doc           -- regenerate PDF documentation
#   make clean         -- remove all generated files
# =============================================================================

TOP      = C6502_Interface
TOP_PBI  = C6502_Interface_PBI
TOP_CCTL = C6502_Interface_CCTL
PCF      = board-pin-mapping.pcf
DEVICE   = up5k
PKG      = sg48
DOC      = build-and-programming-guide

VHDL_SRCS = \
    custom_types.vhd \
    pbi_rom_pkg.vhd \
    C6502_Interface.vhd

GHDL_STD = --std=08

.PHONY: all pbi cctl \
        flash-pbi flash-cctl flash-pbi-iceprog flash-cctl-iceprog \
        doc clean

all: pbi cctl

# =============================================================================
# PBI build  (VERA_IS_PBI=true)
# =============================================================================
$(TOP_PBI).json: $(VHDL_SRCS)
	yosys -m ghdl -p \
	    "ghdl $(GHDL_STD) -gVERA_IS_PBI=true $(VHDL_SRCS) -e $(TOP); \
	     synth_ice40 -top $(TOP) -json $@"

$(TOP_PBI).asc: $(TOP_PBI).json $(PCF)
	nextpnr-ice40 \
	    --$(DEVICE) \
	    --package $(PKG) \
	    --json $< \
	    --pcf  $(PCF) \
	    --asc  $@

$(TOP_PBI).bin: $(TOP_PBI).asc
	icepack $< $@

pbi: $(TOP_PBI).bin

# =============================================================================
# CCTL build  (VERA_IS_PBI=false)
# =============================================================================
$(TOP_CCTL).json: $(VHDL_SRCS)
	yosys -m ghdl -p \
	    "ghdl $(GHDL_STD) -gVERA_IS_PBI=false $(VHDL_SRCS) -e $(TOP); \
	     synth_ice40 -top $(TOP) -json $@"

$(TOP_CCTL).asc: $(TOP_CCTL).json $(PCF)
	nextpnr-ice40 \
	    --$(DEVICE) \
	    --package $(PKG) \
	    --json $< \
	    --pcf  $(PCF) \
	    --asc  $@

$(TOP_CCTL).bin: $(TOP_CCTL).asc
	icepack $< $@

cctl: $(TOP_CCTL).bin

# =============================================================================
# Flash programming
# =============================================================================
flash-pbi: $(TOP_PBI).bin
	openFPGALoader -b ice40_generic $<

flash-cctl: $(TOP_CCTL).bin
	openFPGALoader -b ice40_generic $<

flash-pbi-iceprog: $(TOP_PBI).bin
	iceprog $<

flash-cctl-iceprog: $(TOP_CCTL).bin
	iceprog $<

# =============================================================================
# Documentation
# =============================================================================
doc: $(DOC).pdf

$(DOC).pdf: $(DOC).tex
	pdflatex -interaction=nonstopmode $<
	pdflatex -interaction=nonstopmode $<

# =============================================================================
# Cleanup
# =============================================================================
clean:
	$(RM) $(TOP_PBI).json  $(TOP_PBI).asc  $(TOP_PBI).bin
	$(RM) $(TOP_CCTL).json $(TOP_CCTL).asc $(TOP_CCTL).bin
	$(RM) $(DOC).aux $(DOC).log $(DOC).out $(DOC).toc $(DOC).pdf
