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
    -- Usato solo per la RAM interna ($D600-$D7FF); la ROM è EEPROM esterna.
    signal d_out_drive    : std_logic_vector(7 downto 0);
    signal d_drive_enable : std_logic := '0';

    -- RAM interna (512 Byte, $D600-$D7FF)
    signal internal_ram : ram_512_array := (others => x"00");

begin

    -- Collegamento del Latch al Pin di Uscita VERA_CS
    VERA_CS <= vera_cs_latch;

    -- Gestione del Bus Dati Bidirezionale (Tri-state Buffer)
    -- D viene messo in alta impedenza ('Z') quando l'FPGA non sta leggendo dalla RAM/ROM.
    D <= d_out_drive when d_drive_enable = '1' else (others => 'Z');

    -- Logica Latch, RAM e MPD
    process (PHI2)
        variable bit_index      : integer range 0 to 7;
        variable address_unsigned : unsigned(15 downto 0);
        variable ram_index_full : integer;
        constant RAM_START_UNSIGNED : unsigned(15 downto 0) := unsigned(RAM_START_ADDR);
    begin
        if rising_edge(PHI2) then
            d_drive_enable <= '0';
            MPD    <= '1';
            EXTSEL <= '1';

            address_unsigned := unsigned(A);

            -- ******************************************************
            -- 1. Latch VERA_CS — scrittura a $D1FF
            -- ******************************************************
            if (RNW_ = '0') and (A = REG_ADDR_MASK) then
                bit_index := to_integer(unsigned(DIP_SEL));
                vera_cs_latch <= not D(bit_index);
            end if;

            -- ******************************************************
            -- 2. RAM interna $D600-$D7FF + EXTSEL
            --    Attiva solo quando il dispositivo è selezionato.
            -- ******************************************************
            if (A >= RAM_START_ADDR) and (A <= RAM_END_ADDR) and (vera_cs_latch = '0') then
                EXTSEL <= '0';
                ram_index_full := to_integer(address_unsigned - RAM_START_UNSIGNED);
                if RNW_ = '0' then
                    internal_ram(ram_index_full) <= D;
                else
                    d_out_drive    <= internal_ram(ram_index_full);
                    d_drive_enable <= '1';
                end if;

            -- ******************************************************
            -- 3. EEPROM esterna $D800-$DFFF — solo MPD
            --    L'EEPROM fisica guida il bus dati; l'FPGA rimane
            --    in alta impedenza (d_drive_enable resta '0').
            -- ******************************************************
            elsif (A >= ROM_START_ADDR) and (A <= ROM_END_ADDR) then
                MPD <= '0';

            end if;

        end if;
    end process;

end Behavioral;
