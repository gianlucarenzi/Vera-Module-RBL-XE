library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;
use work.custom_types.all;
use work.pbi_rom_pkg.all;

-- =============================================================================
-- C6502_Interface  --  Rev 2
--
-- Changes from Rev 1:
--   * CLK input (25 MHz FPGA clock) for PHI2 synchronisation
--   * PBI ROM ($D800-$DFFF) moved from external EEPROM into BRAM.
--     External EEPROM no longer required; FPGA drives D[7:0] for ROM reads.
--     MPD is still asserted to prevent the Atari from also driving the bus.
--   * Snoop PIA PORTB ($D301) and PBCTL ($D303) for RAMbo bank selection
--   * EXTSEL asserted also for RAMbo accesses ($4000-$7FFF)
--   * External 512Kx8 SRAM control via 7 new output pins:
--       SRAM_A_BANK[3:0]  ->  chip A[17:14] (bank)
--       SRAM_CE_N, SRAM_OE_N, SRAM_WE_N
--
-- PCB wiring (no extra FPGA pins needed for these signals):
--   * 6502 A[13:0]  ->  SRAM A[13:0]   (same net, 16 KB window offset)
--   * 6502 D[7:0]   ->  SRAM DQ[7:0]   (same net, bidirectional)
--   * SRAM A18      ->  GND            (use lower 256 KB of 512 KB chip)
--
-- RAMbo bank formula (from MCU firmware):
--   bank = ((PORTB >> 2) & 0x03) | ((PORTB >> 3) & 0x0C)
--   -> bank[1:0] = PORTB[3:2]
--   -> bank[3:2] = PORTB[6:5]
--
-- BRAM usage (iCE40UP5K has 30 EBR blocks = 15 KB):
--   PBI ROM  2048 B  ->  4 EBR blocks
--   PBI RAM   512 B  ->  1 EBR block
--   Total:   5 of 30 EBR blocks used
-- =============================================================================

entity C6502_Interface is
    port (
        CLK         : in    std_logic;  -- 25 MHz FPGA system clock

        -- 6502 bus
        A           : in    std_logic_vector(15 downto 0);
        D           : inout std_logic_vector(7 downto 0);
        RNW        : in    std_logic;
        PHI2        : in    std_logic;

        -- DIP switches (VERA_CS bit selector, selects D[0]-D[3])
        DIP_SEL     : in    std_logic_vector(1 downto 0);

        -- PBI control outputs
        VERA_CS     : out   std_logic;
        MPD         : out   std_logic;  -- still needed: blocks Atari from driving bus
        EXTSEL      : out   std_logic;

        -- External SRAM control -- 7 new pins
        SRAM_A_BANK : out   std_logic_vector(3 downto 0);  -- A[17:14] -> bank
        SRAM_CE_N   : out   std_logic;
        SRAM_OE_N   : out   std_logic;
        SRAM_WE_N   : out   std_logic
    );
end entity C6502_Interface;

architecture Behavioral of C6502_Interface is

    -- -------------------------------------------------------------------------
    -- Address constants
    -- -------------------------------------------------------------------------
    constant C_LATCH_ADDR  : unsigned(15 downto 0) := x"D1FF";
    constant C_PORTB_ADDR  : unsigned(15 downto 0) := x"D301";
    constant C_PBCTL_ADDR  : unsigned(15 downto 0) := x"D303";
    constant C_PBI_RAM_LO  : unsigned(15 downto 0) := x"D600";
    constant C_PBI_RAM_HI  : unsigned(15 downto 0) := x"D7FF";
    constant C_PBI_ROM_LO  : unsigned(15 downto 0) := x"D800";
    constant C_PBI_ROM_HI  : unsigned(15 downto 0) := x"DFFF";

    -- -------------------------------------------------------------------------
    -- PHI2 two-stage synchroniser.
    -- Adds ~80 ns latency from the real PHI2 edge, well within the
    -- PHI2-high window (~279 ns at 1.79 MHz).
    -- -------------------------------------------------------------------------
    signal phi2_s    : std_logic := '0';
    signal phi2_ss   : std_logic := '0';
    signal phi2_rise : std_logic;           -- high for exactly one CLK cycle

    -- -------------------------------------------------------------------------
    -- Internal registers, updated on phi2_rise
    -- -------------------------------------------------------------------------
    signal vera_cs_latch : std_logic := '1';
    signal portb_reg     : std_logic_vector(7 downto 0) := x"FF";
    signal pbctl_reg     : std_logic_vector(7 downto 0) := x"00";

    -- PBI RAM 512 B ($D600-$D7FF) — 1 EBR block
    signal pbi_ram  : ram_512_array := (others => x"00");
    signal d_pbi    : std_logic_vector(7 downto 0) := (others => '0');

    -- PBI ROM 2048 B ($D800-$DFFF) — 4 EBR blocks, pre-loaded at synthesis
    signal pbi_rom  : rom_array := PBI_ROM_INIT;
    signal d_rom    : std_logic_vector(7 downto 0) := (others => '0');

    -- -------------------------------------------------------------------------
    -- Address decode signals -- purely combinational.
    -- The 6502 address bus is stable before PHI2 rises (tADS < 40 ns),
    -- so all select signals are valid for the entire PHI2-high phase.
    -- -------------------------------------------------------------------------
    signal addr        : unsigned(15 downto 0);

    signal sel_latch   : std_logic;   -- write to $D1FF
    signal sel_portb   : std_logic;   -- write to $D301 (PIA PORTB)
    signal sel_pbctl   : std_logic;   -- write to $D303 (PIA PBCTL)
    signal sel_pbi_ram : std_logic;   -- $D600-$D7FF  (only when VERA selected)
    signal sel_pbi_rom : std_logic;   -- $D800-$DFFF
    signal sel_rambo   : std_logic;   -- $4000-$7FFF  (only when RAMbo enabled)

    -- -------------------------------------------------------------------------
    -- RAMbo
    -- -------------------------------------------------------------------------
    signal rambo_bank : std_logic_vector(3 downto 0);
    signal rambo_en   : std_logic;

begin

    addr <= unsigned(A);

    -- =========================================================================
    -- RAMbo bank selection
    -- =========================================================================
    rambo_bank <= portb_reg(6) & portb_reg(5) & portb_reg(3) & portb_reg(2);
    rambo_en   <= pbctl_reg(2);

    -- =========================================================================
    -- Address decode
    -- =========================================================================
    sel_latch   <= '1' when addr = C_LATCH_ADDR                              else '0';
    sel_portb   <= '1' when addr = C_PORTB_ADDR and RNW = '0'              else '0';
    sel_pbctl   <= '1' when addr = C_PBCTL_ADDR and RNW = '0'              else '0';
    sel_pbi_ram <= '1' when addr >= C_PBI_RAM_LO and addr <= C_PBI_RAM_HI
                            and vera_cs_latch = '0'                          else '0';
    sel_pbi_rom <= '1' when addr >= C_PBI_ROM_LO and addr <= C_PBI_ROM_HI   else '0';
    sel_rambo   <= '1' when A(15) = '0' and A(14) = '1' and rambo_en = '1'  else '0';

    -- =========================================================================
    -- PHI2 synchroniser
    -- =========================================================================
    p_sync : process (CLK)
    begin
        if rising_edge(CLK) then
            phi2_s  <= PHI2;
            phi2_ss <= phi2_s;
        end if;
    end process p_sync;

    phi2_rise <= phi2_s and not phi2_ss;

    -- =========================================================================
    -- Main process -- runs on every CLK, active only on phi2_rise.
    --
    -- Handles:
    --   1. VERA_CS latch update            ($D1FF write)
    --   2. PIA PORTB snoop                 ($D301 write)
    --   3. PIA PBCTL snoop                 ($D303 write)
    --   4. PBI RAM read (latch BRAM output) ($D600-$D7FF read)
    --   5. PBI RAM write                   ($D600-$D7FF write)
    --   6. PBI ROM read (latch BRAM output) ($D800-$DFFF read)
    --
    -- Bus outputs (MPD, EXTSEL, D, SRAM_*) are driven by combinational
    -- logic so they remain active for the full PHI2-high window.
    -- =========================================================================
    p_main : process (CLK)
        variable bit_idx  : integer range 0 to 3;
        variable ram_offs : integer range 0 to 511;
        variable rom_offs : integer range 0 to 2047;
    begin
        if rising_edge(CLK) then
            if phi2_rise = '1' then

                -- 1. VERA_CS latch
                if sel_latch = '1' and RNW = '0' then
                    bit_idx := to_integer(unsigned(DIP_SEL));
                    vera_cs_latch <= not D(bit_idx);
                end if;

                -- 2. Snoop PORTB -- update RAMbo bank
                if sel_portb = '1' then
                    portb_reg <= D;
                end if;

                -- 3. Snoop PBCTL -- update RAMbo enable
                if sel_pbctl = '1' then
                    pbctl_reg <= D;
                end if;

                -- 4. PBI RAM read: latch BRAM output.
                --    d_pbi valid one CLK later (~40 ns after phi2_rise).
                if sel_pbi_ram = '1' and RNW = '1' then
                    ram_offs := to_integer(addr - C_PBI_RAM_LO);
                    d_pbi    <= pbi_ram(ram_offs);
                end if;

                -- 5. PBI RAM write
                if sel_pbi_ram = '1' and RNW = '0' then
                    ram_offs          := to_integer(addr - C_PBI_RAM_LO);
                    pbi_ram(ram_offs) <= D;
                end if;

                -- 6. PBI ROM read: latch BRAM output.
                --    d_rom valid one CLK later (~40 ns after phi2_rise).
                --    ROM is read-only; writes are silently ignored.
                if sel_pbi_rom = '1' and RNW = '1' then
                    rom_offs := to_integer(addr - C_PBI_ROM_LO);
                    d_rom    <= pbi_rom(rom_offs);
                end if;

            end if;
        end if;
    end process p_main;

    -- =========================================================================
    -- 6502 bus control outputs -- combinational, gated by PHI2.
    --
    -- Using PHI2 directly as a combinational gate is standard practice for
    -- 6502 bus interfaces: the address is stable before PHI2 rises and does
    -- not change until after PHI2 falls, so no hazardous glitches occur.
    -- =========================================================================

    VERA_CS <= vera_cs_latch;

    -- MPD: assert to block the Atari from driving D[7:0] for $D800-$DFFF.
    -- FPGA now owns that range (BRAM ROM); the external EEPROM is removed.
    MPD <= '0' when sel_pbi_rom = '1' and PHI2 = '1' else '1';

    -- EXTSEL: assert for PBI RAM or RAMbo to block the Atari's internal RAM
    EXTSEL <= '0' when (sel_pbi_ram = '1' or sel_rambo = '1') and PHI2 = '1' else '1';

    -- Data bus: FPGA drives D for PBI RAM reads and PBI ROM reads.
    -- For RAMbo reads: SRAM_OE_N='0' -> SRAM drives DQ[7:0] via PCB;
    --                  FPGA stays high-Z.
    D <= d_pbi when sel_pbi_ram = '1' and RNW = '1' and PHI2 = '1' else
         d_rom when sel_pbi_rom = '1' and RNW = '1' and PHI2 = '1' else
         (others => 'Z');

    -- =========================================================================
    -- External SRAM control -- combinational, gated by PHI2.
    --
    -- Timing budget (AS6C4008-55, tAA = 55 ns, 6502 @ 1.79 MHz):
    --   PHI2 rise  ->  CE# low        ~0 ns   (combinational)
    --   CE# low    ->  data valid     <=55 ns
    --   PHI2 high  ->  6502 samples   ~249 ns (279 ns - 30 ns tDSR)
    --   Read margin                   ~194 ns  (ok)
    --
    --   Write: WE# pulse width = PHI2 high time ~279 ns >> tWC = 55 ns  (ok)
    -- =========================================================================
    SRAM_A_BANK <= rambo_bank;

    SRAM_CE_N <= '0' when sel_rambo = '1' and PHI2 = '1'                 else '1';
    SRAM_OE_N <= '0' when sel_rambo = '1' and PHI2 = '1' and RNW = '1' else '1';
    SRAM_WE_N <= '0' when sel_rambo = '1' and PHI2 = '1' and RNW = '0' else '1';

end architecture Behavioral;
