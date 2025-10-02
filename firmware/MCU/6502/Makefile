# =========================================================================
# Makefile per compilare un driver 6502 e convertirlo in array C
# =========================================================================
CC65_HOME ?=
CA65 = $(CC65_HOME)ca65
LD65 = $(CC65_HOME)ld65

# Nome del file sorgente assembly
ASM_SRC = \
	src/pbi-driver.s

# Nomi dei file intermedi e finali
OBJ_NAME = pbi-driver
OBJ_FILE = $(OBJ_NAME).o
LIST_FILE = $(OBJ_NAME).l
BIN_FILE = $(OBJ_NAME).bin
C_ARRAY_FILE =$(OBJ_NAME).h

# File di configurazione del linker
LINK_CFG = pbi-driver.ld

.PHONY: all clean

# Regola predefinita: compila tutto
all: $(BIN_FILE) $(C_ARRAY_FILE)

# ---
## Assemblaggio del Codice 6502

# Regola per assemblare il codice 6502 in un file oggetto (.o)
$(OBJ_FILE): $(ASM_SRC)
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
	@echo "#ifndef PBI_DRIVER_BIN_ARRAY_H" >> $@
	@echo "#define PBI_DRIVER_BIN_ARRAY_H" >> $@
	@echo "" >> $@
	@echo "#include <stdint.h>" >> $@
	@echo "" >> $@
	@echo "const uint8_t pbi_driver[] = {" >> $@
	hexdump -v -e '16/1 "0x%02x," "\n"' $<  >> $@
	@echo "" >> $@
	@echo "};" >> $@
	@echo "" >> $@
	@echo "#define PBI_DRIVER_BIN_SIZE sizeof(pbi_driver)" >> $@
	@echo "#endif // PBI_DRIVER_BIN_ARRAY_H" >> $@
	@echo "Copying file..."
	cp $@ ../include/

# ---
## Pulizia

# Regola per pulire i file generati
clean:
	$(RM) $(OBJ_FILE) $(LIST_FILE) $(BIN_FILE) $(C_ARRAY_FILE)
	$(RM) *~
