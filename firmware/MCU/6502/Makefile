# =========================================================================
# Slot ROM 6502 ($Cn00-$CnFF) della scheda Apple II -> array C per l'ESP32
# =========================================================================
CC65_HOME ?=
CA65 = $(CC65_HOME)ca65
LD65 = $(CC65_HOME)ld65

ASM_SRC   = src/slinky_rom.s
OBJ_NAME  = slinky_rom
OBJ_FILE  = $(OBJ_NAME).o
LIST_FILE = $(OBJ_NAME).l
BIN_FILE  = $(OBJ_NAME).bin
C_ARRAY_FILE = $(OBJ_NAME).h
LINK_CFG  = slinky_rom.ld

.PHONY: all clean

all: $(BIN_FILE) $(C_ARRAY_FILE)

$(OBJ_FILE): $(ASM_SRC)
	$(CA65) -g $< -o $@ -l $(LIST_FILE)

$(BIN_FILE): $(OBJ_FILE) $(LINK_CFG)
	$(LD65) -C $(LINK_CFG) $< -o $@

# DRAM_ATTR: il bus loop legge la ROM a byte (l8ui), possibile solo sul bus dati.
# Senza ESP-IDF (test host) DRAM_ATTR e' definito vuoto.
$(C_ARRAY_FILE): $(BIN_FILE) Makefile
	@echo "/* Automatically generated from src/slinky_rom.s! Do not edit! */" > $@
	@echo "#ifndef SLINKY_ROM_H" >> $@
	@echo "#define SLINKY_ROM_H" >> $@
	@echo "" >> $@
	@echo "#include <stdint.h>" >> $@
	@echo "#ifndef DRAM_ATTR" >> $@
	@echo "#define DRAM_ATTR" >> $@
	@echo "#endif" >> $@
	@echo "" >> $@
	@echo "static const uint8_t DRAM_ATTR slinky_rom[256] = {" >> $@
	hexdump -v -e '16/1 "0x%02x," "\n"' $< >> $@
	@echo "};" >> $@
	@echo "" >> $@
	@echo "#endif /* SLINKY_ROM_H */" >> $@
	cp $@ ../include/

clean:
	$(RM) $(OBJ_FILE) $(LIST_FILE) $(BIN_FILE) $(C_ARRAY_FILE) ../include/$(C_ARRAY_FILE)
