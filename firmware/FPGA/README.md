🚀 Interfaccia 6502 per FPGA Lattice iCE40UP5K (Open-Source Toolchain)

Questo progetto implementa una logica di interfaccia per un microprocessore 6502 (o compatibile) utilizzando la FPGA Lattice iCE40 UltraPlus (iCE40UP5K).

Il progetto è ottimizzato per l'uso della toolchain open-source su Linux (Project IceStorm, Yosys, nextpnr), sfruttando le Block RAM (BRAM) del chip per la memoria interna.

📋 Requisiti Funzionali

L'interfaccia gestisce le seguenti aree di memoria/I/O:

    Latch VERA_CS: Scrittura all'indirizzo $D1FF. Il bit di controllo per il latch è selezionato da 3 DIP switch esterni (DIP_SEL[2:0]).

    RAM Esterna (512 Byte): Accesso in lettura/scrittura all'area $D600-$D7FF (gestita da una RAM interna all'FPGA). L'accesso è condizionato dallo stato del latch (VERA_CS = '0'). Il pin EXTSEL viene abbassato per disabilitare la ROM/RAM interna del 6502 su questo range.

    ROM Interna (2 KB): Sola lettura all'area $D800-$DFFF.

    Pin Select MPD: Abbassato ('0') durante l'accesso alla ROM.

🛠️ Toolchain Open-Source (Linux)

Questo progetto richiede la toolchain open-source per iCE40:
Strumento	Funzione
GHDL	Front-end per l'analisi e l'elaborazione del codice VHDL.
Yosys	Sintesi RTL (converte VHDL in netlist logica).
nextpnr-ice40	Place & Route (mappa la netlist sulle risorse del chip, leggendo il file PCF).
IceStorm Tools	icepack (crea il bitstream .bin) e iceprog (carica il bitstream).

Si raccomanda l'installazione tramite la suite YosysHQ OSS-CAD Suite per un ambiente già configurato con supporto GHDL/VHDL.

📂 Struttura dei File VHDL

I file VHDL implementano la logica descritta:

1. custom_types.vhd

Definisce i tipi di array per la memoria.
VHDL

library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

package custom_types is
    -- ROM (2048 x 8 bit) per l'area $D800-$DFFF
    type rom_array is array (0 to 2047) of std_logic_vector(7 downto 0);
    
    -- RAM (512 x 8 bit) per l'area $D600-$D7FF
    type ram_512_array is array (0 to 511) of std_logic_vector(7 downto 0);
end package custom_types;

2. C6502_Interface.vhd

Contiene l'entità principale, la logica di controllo e l'implementazione delle memorie BRAM.

N.B.: L'inizializzazione dei segnali pbi_driver (ROM) e internal_ram (RAM) nel codice è il metodo corretto per forzare Yosys a usare la Block RAM (BRAM) dedicata dell'iCE40UP5K.

(Inserire qui il codice VHDL completo di C6502_Interface.vhd)

📍 Assegnazione dei Pin (File PCF)

Per connettere i segnali VHDL ai pin fisici della FPGA, è necessario creare un file PCF (Pin Constraint File). Il nome del file deve essere specificato a nextpnr (es. scheda.pcf).

Esempio di scheda.pcf (i Pin devono essere adattati alla TUA scheda):
Snippet di codice

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

# Bus Dati (8 bit, Bidirezionale) - Esempio
set_io D[0]     60
set_io D[1]     61
set_io D[2]     62
set_io D[3]     63
set_io D[4]     64
set_io D[5]     65
set_io D[6]     66
set_io D[7]     67

# Bus Indirizzi (16 bit) - Esempio
set_io A[0]     40
set_io A[1]     41
# ... A[2] - A[14] ...
set_io A[15]    55

⚙️ Processo di Compilazione su Linux

Una volta installata la toolchain e creati i file .vhd e il file .pcf (es. scheda.pcf):

1. Analisi e Sintesi (GHDL & Yosys)

Bash

# 1. Analisi e Elaboratione dei moduli VHDL
ghdl -a custom_types.vhd
ghdl -a C6502_Interface.vhd
ghdl -e C6502_Interface

# 2. Sintesi con Yosys (genera la netlist JSON)
yosys -m ghdl -p 'ghdl C6502_Interface; synth_ice40 -top C6502_Interface -json C6502_Interface.json'

2. Place and Route (nextpnr)

Sostituisci --package tqfp100 e il nome del chip se necessario.
Bash

nextpnr-ice40 --up5k --package tqfp100 --json C6502_Interface.json --pcf scheda.pcf --asc C6502_Interface.asc

3. Generazione e Caricamento del Bitstream (IceStorm Tools)

Il bitstream (configurazione dell'FPGA) viene memorizzato in una Flash SPI esterna.
Bash

# 1. Crea il bitstream binario
icepack C6502_Interface.asc C6502_Interface.bin

# 2. Carica il bitstream sulla Flash SPI (o direttamente sull'FPGA, dipende dal setup)
iceprog C6502_Interface.bin

Il chip iCE40UP5K si configurerà all'accensione leggendo il file C6502_Interface.bin dalla Flash SPI.
