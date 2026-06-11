# =========================================================================
# Makefile per compilare un driver 6502 e convertirlo in array C
# =========================================================================
CC65_HOME ?=
CA65 = $(CC65_HOME)ca65
LD65 = $(CC65_HOME)ld65

# Sorgente assembly e include files
ASM_SRC   = src/vera_pbi_handler.s
INC_FILES = inc/vera_common.inc

# Nomi dei file intermedi e finali
OBJ_NAME     = vera_pbi_handler
OBJ_FILE     = $(OBJ_NAME).o
LIST_FILE    = $(OBJ_NAME).l
BIN_FILE     = $(OBJ_NAME).bin
C_ARRAY_FILE = $(OBJ_NAME).h

# File di configurazione del linker
LINK_CFG = pbi-driver.ld

# FPGA ROM package (VHDL)
FPGA_DIR       = ../../FPGA
ROM_PKG_VHD    = $(FPGA_DIR)/pbi_rom_pkg.vhd
GEN_ROM_SCRIPT = $(FPGA_DIR)/gen_pbi_rom_pkg.py

.PHONY: all clean fpga-rom

# Regola predefinita: compila tutto
all: $(BIN_FILE) $(C_ARRAY_FILE) $(ROM_PKG_VHD)

# ---
## Assemblaggio del Codice 6502

# Regola per assemblare il codice 6502 in un file oggetto (.o)
$(OBJ_FILE): $(ASM_SRC) $(INC_FILES)
	$(CA65) -Iinc -g $< -o $@ -l $(LIST_FILE)

# ---
## Linking del File Binario

# Regola per linkare il file oggetto e creare il binario finale (.bin)
$(BIN_FILE): $(OBJ_FILE) $(LINK_CFG)
	$(LD65) -C $(LINK_CFG) $< -o $@

# ---
## Conversione in Array C

# Regola per convertire il binario in un array C in un file .h
$(C_ARRAY_FILE): $(BIN_FILE)
	@echo "/* Automatically generated! Do not edit! */" > $@
	@echo "#ifndef VERA_PBI_HANDLER_BIN_ARRAY_H" >> $@
	@echo "#define VERA_PBI_HANDLER_BIN_ARRAY_H" >> $@
	@echo "" >> $@
	@echo "#include <stdint.h>" >> $@
	@echo "" >> $@
	@echo "const uint8_t IRAM_ATTR vera_pbi_handler[] = {" >> $@
	hexdump -v -e '16/1 "0x%02x," "\n"' $< >> $@
	@echo "" >> $@
	@echo "};" >> $@
	@echo "" >> $@
	@echo "#define VERA_PBI_HANDLER_BIN_SIZE sizeof(vera_pbi_handler)" >> $@
	@echo "#endif // VERA_PBI_HANDLER_BIN_ARRAY_H" >> $@
	@echo "Copying file..."
	cp $@ ../include/

# ---
## FPGA ROM package

# Genera pbi_rom_pkg.vhd dal binario 6502 per l'inizializzazione della BRAM
fpga-rom: $(ROM_PKG_VHD)

$(ROM_PKG_VHD): $(BIN_FILE) $(GEN_ROM_SCRIPT)
	python3 $(GEN_ROM_SCRIPT) $< $@

# ---
## Pulizia

# Regola per pulire i file generati
clean:
	$(RM) $(OBJ_FILE) $(LIST_FILE) $(BIN_FILE) $(C_ARRAY_FILE)
	$(RM) $(ROM_PKG_VHD)
	$(RM) *~
