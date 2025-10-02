library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;
use work.custom_types.all;

entity C6502_Interface is
    port (
        -- Segnali di Interfaccia 6502
        A         : in  std_logic_vector(15 downto 0);  -- Bus Indirizzi 16 bit
        D         : inout std_logic_vector(7 downto 0); -- Bus Dati 8 bit BIDIREZIONALE
        RNW_      : in  std_logic;                      -- Read/Not_Write
        PHI2      : in  std_logic;                      -- Clock 6502 (Fase 2)

        -- Nuovi Input
        DIP_SEL   : in  std_logic_vector(2 downto 0);   -- 3 DIP Switch (selettore bit 0-7)

        -- Uscite di Controllo/Stato
        VERA_CS   : out std_logic;                      -- Output Latch (Attivo Basso)
        MPD       : out std_logic;                      -- Chip Select ROM $D800-$DFFF (Attivo Basso)
        EXTSEL    : out std_logic                       -- Chip Select RAM $D600-$D7FF (Attivo Basso)
    );
end C6502_Interface;

architecture Behavioral of C6502_Interface is

    -- Stato interno per il Latch VERA_CS (attivo basso)
    signal vera_cs_latch : std_logic := '1';

    -- Costanti per gli Indirizzi
    constant REG_ADDR_MASK   : std_logic_vector(15 downto 0) := x"D1FF";
    constant RAM_START_ADDR  : std_logic_vector(15 downto 0) := x"D600";
    constant RAM_END_ADDR    : std_logic_vector(15 downto 0) := x"D7FF";
    constant ROM_START_ADDR  : std_logic_vector(15 downto 0) := x"D800";
    constant ROM_END_ADDR    : std_logic_vector(15 downto 0) := x"DFFF";

    -- Segnali interni per la Gestione del Bus Dati (Tri-state)
    signal d_out_drive    : std_logic_vector(7 downto 0); -- Dati da pilotare (RAM/ROM)
    signal d_drive_enable : std_logic := '0';             -- Abilita il driver D

    -- Implementazione BRAM (ROM da 2KB) - Inizializzazione dati
    -- il firmware driver PBI va compilato a parte con CC65 e poi
    -- inserito qui, byte per byte
    signal pbi_driver : rom_array := (
        0      => x"01", -- Dati a offset 0 ($D800)
        1      => x"02", 
        others => x"EA"  -- Contenuto ROM NOP
    );
    
    -- Implementazione RAM (512 Byte) - Inizializzazione a zero
    signal internal_ram : ram_512_array := (others => x"00"); 

begin

    -- Collegamento del Latch al Pin di Uscita VERA_CS
    VERA_CS <= vera_cs_latch;

    -- Gestione del Bus Dati Bidirezionale (Tri-state Buffer)
    -- D viene messo in alta impedenza ('Z') quando l'FPGA non sta leggendo dalla RAM/ROM.
    D <= d_out_drive when d_drive_enable = '1' else (others => 'Z');

    -- Logica Latch, RAM, ROM e Controllo I/O
    process (PHI2)
        -- Variabili per le conversioni indice
        variable bit_index : integer range 0 to 7;
        variable address_unsigned : unsigned(15 downto 0);
        variable ram_index_full : integer;
        variable rom_offset : integer range 0 to 2047;
        
        -- Costanti unsigned per le sottrazioni
        constant RAM_START_UNSIGNED : unsigned(15 downto 0) := unsigned(RAM_START_ADDR);
        constant ROM_START_UNSIGNED : unsigned(15 downto 0) := unsigned(ROM_START_ADDR);

    begin
        if rising_edge(PHI2) then
            -- Reset di default dei segnali di controllo I/O
            d_drive_enable <= '0';
            MPD <= '1';
            EXTSEL <= '1';
            
            address_unsigned := unsigned(A);
            
            -- ******************************************************
            -- 1. Logica del LATCH VERA_CS (Scrittura a $D1FF$)
            -- ******************************************************
            if (RNW_ = '0') and (A = REG_ADDR_MASK) then 
                -- Calcola l'indice del bit D da controllare (0-7)
                bit_index := to_integer(unsigned(DIP_SEL));
                
                -- Aggiorna il latch in scrittura (Attivo Basso se D(index) = '1')
                vera_cs_latch <= not D(bit_index); 
            end if;

            -- ******************************************************
            -- 2. Logica RAM (D600-D7FF) e EXTSEL
            -- ******************************************************
            -- La RAM è selezionata solo se VERA_CS è attivo ('0') e l'indirizzo è nel range
            if (A >= RAM_START_ADDR) and (A <= RAM_END_ADDR) and (vera_cs_latch = '0') then 
                
                EXTSEL <= '0'; -- Abilita l'accesso RAM esterno (richiesta dall'utente)
                
                -- Calcola l'indirizzo relativo alla RAM (offset 0-511)
                ram_index_full := to_integer(address_unsigned - RAM_START_UNSIGNED);
                
                if RNW_ = '0' then -- **SCRITTURA** nella RAM
                    internal_ram(ram_index_full) <= D; -- Scrive il dato D (che in scrittura è input)
                
                else -- RNW_ = '1' - **LETTURA** dalla RAM
                    -- Legge il dato dalla RAM e lo mette nel driver
                    d_out_drive <= internal_ram(ram_index_full);
                    d_drive_enable <= '1'; -- Abilita il tri-state D (D pilota il bus)
                end if;
            
            -- ******************************************************
            -- 3. Logica ROM (D800-DFFF) e MPD
            -- ******************************************************
            elsif (A >= ROM_START_ADDR) and (A <= ROM_END_ADDR) and (RNW_ = '1') then 
                
                MPD <= '0'; -- Seleziona ROM
                
                -- Calcola l'offset ROM (A - $D800)
                rom_offset := to_integer(address_unsigned - ROM_START_UNSIGNED);
                
                -- Mette il dato della ROM nel driver
                d_out_drive <= pbi_driver(rom_offset);
                d_drive_enable <= '1'; -- Abilita il tri-state D (D pilota il bus)
                
            end if;

            -- Se in lettura e nessuna periferica è selezionata, d_drive_enable rimane '0' (bus in 'Z').

        end if;
    end process;

end Behavioral;
