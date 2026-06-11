library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;
use work.custom_types.all;
use work.pbi_rom_pkg.all;

-- =============================================================================
-- C6502_Interface  --  Rev 3
--
-- Generic VERA_IS_PBI selects the Atari bus interface at synthesis time.
-- Build two separate bitstreams:
--   make pbi   ->  C6502_Interface_PBI.bin   (PBI connector,       $D1xx)
--   make cctl  ->  C6502_Interface_CCTL.bin  (cartridge port CCTL, $D5xx)
--
-- PBI mode  (VERA_IS_PBI = true):
--   VERA_CS latch at $D1FF: any write; new state = PBI_ID_SEL
--     PBI_ID_SEL is an FPGA pin wired via hardware jumper to one of D[0..7].
--     The jumper selects the PBI device-ID bit entirely in hardware;
--     no VHDL change needed when the jumper position changes.
--   PBI RAM  $D600-$D7FF : 512 B in BRAM  (1 EBR block)
--   PBI ROM  $D800-$DFFF : 2 KB in BRAM   (4 EBR blocks, pre-loaded from binary)
--   EXTSEL asserted for PBI RAM + RAMbo accesses
--   MPD    asserted for PBI ROM reads (blocks Atari from driving D bus)
--
-- CCTL mode (VERA_IS_PBI = false):
--   VERA_CS latch at $D5FF: write $80 (D[7]=1) selects, write $00 deselects.
--   PBI_ID_SEL pin ignored.  No PBI RAM, no PBI ROM (saves 5 EBR blocks).
--   EXTSEL asserted for RAMbo only.
--   MPD    always inactive (high).
--
-- Hardware pins (common to both modes):
--   PBI_ID_SEL  FPGA pin wired via jumper to chosen D[n] on PCB.
--               Sampled at phi2_rise when $D1FF is written (PBI mode only).
--   RAMBO_EN    Pullup = RAMbo active, pulldown = RAMbo completely disabled.
--               Gates RAMbo independently from the PBCTL[2] software enable.
--               Both must be true for RAMbo to respond.
--
-- PCB wiring (no extra FPGA pins needed):
--   6502 A[13:0] -> SRAM A[13:0]  (same net, 16 KB window)
--   6502 D[7:0]  -> SRAM DQ[7:0]  (same net, bidirectional)
--   SRAM A18     -> GND            (use lower 256 KB of 512 KB device)
--
-- RAMbo bank formula:
--   bank[1:0] = PORTB[3:2]
--   bank[3:2] = PORTB[6:5]
--   SRAM A[17:14] = { PORTB[6], PORTB[5], PORTB[3], PORTB[2] }
--
-- BRAM usage (iCE40UP5K: 30 EBR blocks):
--   PBI mode : 5 / 30 (ROM: 4, RAM: 1)
--   CCTL mode: 0 / 30
-- =============================================================================

entity C6502_Interface is
    generic (
        VERA_IS_PBI : boolean := true   -- true = PBI ($D1xx), false = CCTL ($D5xx)
    );
    port (
        CLK         : in    std_logic;  -- 25 MHz FPGA system clock

        -- 6502 bus
        A           : in    std_logic_vector(15 downto 0);
        D           : inout std_logic_vector(7 downto 0);
        RNW         : in    std_logic;  -- high = read, low = write
        PHI2        : in    std_logic;

        -- Hardware configuration inputs
        PBI_ID_SEL  : in    std_logic;  -- jumper to chosen D[n]; used in PBI mode only
        RAMBO_EN    : in    std_logic;  -- pullup = enabled, pulldown = disabled

        -- PBI / bus control outputs
        VERA_CS     : out   std_logic;  -- VERA chip-select (active low)
        MPD         : out   std_logic;  -- Machine Program Data (active low, PBI mode)
        EXTSEL      : out   std_logic;  -- External Select (active low)

        -- External SRAM control -- 7 pins
        SRAM_A_BANK : out   std_logic_vector(3 downto 0);  -- SRAM A[17:14]
        SRAM_CE_N   : out   std_logic;
        SRAM_OE_N   : out   std_logic;
        SRAM_WE_N   : out   std_logic
    );
end entity C6502_Interface;

architecture Behavioral of C6502_Interface is

    -- -------------------------------------------------------------------------
    -- Address constants
    -- -------------------------------------------------------------------------
    constant C_LATCH_PBI   : unsigned(15 downto 0) := x"D1FF";  -- PBI latch
    constant C_LATCH_CCTL  : unsigned(15 downto 0) := x"D5FF";  -- CCTL latch

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
    signal phi2_rise : std_logic;

    -- -------------------------------------------------------------------------
    -- Internal registers
    -- -------------------------------------------------------------------------
    signal vera_cs_latch : std_logic := '1';              -- active low
    signal portb_reg     : std_logic_vector(7 downto 0) := x"FF";
    signal pbctl_reg     : std_logic_vector(7 downto 0) := x"00";

    -- PBI RAM 512 B / PBI ROM 2 KB -- present only in PBI mode.
    -- In CCTL mode these signals are never read; the synthesiser eliminates
    -- the BRAM blocks entirely when VERA_IS_PBI=false.
    signal pbi_ram  : ram_512_array := (others => x"00");
    signal d_pbi    : std_logic_vector(7 downto 0) := (others => '0');
    signal pbi_rom  : rom_array := PBI_ROM_INIT;
    signal d_rom    : std_logic_vector(7 downto 0) := (others => '0');

    -- -------------------------------------------------------------------------
    -- Address decode (combinational)
    -- -------------------------------------------------------------------------
    signal addr        : unsigned(15 downto 0);

    signal sel_latch   : std_logic;
    signal sel_portb   : std_logic;
    signal sel_pbctl   : std_logic;
    signal sel_pbi_ram : std_logic;
    signal sel_pbi_rom : std_logic;
    signal sel_rambo   : std_logic;

    -- -------------------------------------------------------------------------
    -- RAMbo
    -- -------------------------------------------------------------------------
    signal rambo_bank   : std_logic_vector(3 downto 0);
    signal rambo_sw     : std_logic;   -- software enable from PBCTL[2]
    signal rambo_active : std_logic;   -- RAMBO_EN AND rambo_sw

begin

    addr <= unsigned(A);

    -- =========================================================================
    -- RAMbo bank and enable
    -- Both the hardware jumper (RAMBO_EN) and the software bit (PBCTL[2])
    -- must be asserted for RAMbo to respond.
    -- =========================================================================
    rambo_bank   <= portb_reg(6) & portb_reg(5) & portb_reg(3) & portb_reg(2);
    rambo_sw     <= pbctl_reg(2);
    rambo_active <= RAMBO_EN and rambo_sw;

    sel_rambo <= '1' when A(15)='0' and A(14)='1' and rambo_active='1' else '0';

    -- =========================================================================
    -- Address decode
    -- =========================================================================

    -- Latch: $D1FF in PBI mode, $D5FF in CCTL mode
    sel_latch <= '1' when (    VERA_IS_PBI and addr = C_LATCH_PBI)  or
                               (not VERA_IS_PBI and addr = C_LATCH_CCTL) else '0';

    sel_portb <= '1' when addr = C_PORTB_ADDR and RNW='0' else '0';
    sel_pbctl <= '1' when addr = C_PBCTL_ADDR and RNW='0' else '0';

    -- PBI RAM and ROM exist only in PBI mode.
    -- When VERA_IS_PBI=false these are constant '0'; synthesiser removes the logic.
    sel_pbi_ram <= '1' when VERA_IS_PBI and
                            addr >= C_PBI_RAM_LO and addr <= C_PBI_RAM_HI and
                            vera_cs_latch='0' else '0';
    sel_pbi_rom <= '1' when VERA_IS_PBI and
                            addr >= C_PBI_ROM_LO and addr <= C_PBI_ROM_HI else '0';

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
    -- Main process -- clocked, active on phi2_rise only.
    --
    -- PBI mode:
    --   1. VERA_CS latch at $D1FF: vera_cs_latch <= NOT PBI_ID_SEL
    --      (PBI_ID_SEL is the data bit chosen by the hardware jumper;
    --       writing '1' in that bit selects VERA, '0' deselects)
    --   2. Snoop PORTB ($D301) for RAMbo bank
    --   3. Snoop PBCTL ($D303) for RAMbo software enable
    --   4. PBI RAM read: latch BRAM output ($D600-$D7FF)
    --   5. PBI RAM write
    --   6. PBI ROM read: latch BRAM output ($D800-$DFFF)
    --
    -- CCTL mode:
    --   1. VERA_CS latch at $D5FF: vera_cs_latch <= NOT D(7)
    --      (write $80 to select VERA, write $00 to deselect)
    --   2-3. Snoop PORTB/PBCTL (same as PBI mode)
    --   4-6. Not present (VERA_IS_PBI=false -> dead code removed by synthesiser)
    -- =========================================================================
    p_main : process (CLK)
        variable ram_offs : integer range 0 to 511;
        variable rom_offs : integer range 0 to 2047;
    begin
        if rising_edge(CLK) then
            if phi2_rise = '1' then

                -- 1. VERA_CS latch
                if sel_latch='1' and RNW='0' then
                    if VERA_IS_PBI then
                        -- PBI: PBI_ID_SEL carries the ID bit value (set by jumper).
                        -- Active-low latch: selected when bit=1.
                        vera_cs_latch <= not PBI_ID_SEL;
                    else
                        -- CCTL: D[7]=1 selects ($80), D[7]=0 deselects ($00).
                        vera_cs_latch <= not D(7);
                    end if;
                end if;

                -- 2. Snoop PORTB (RAMbo bank select)
                if sel_portb='1' then
                    portb_reg <= D;
                end if;

                -- 3. Snoop PBCTL (RAMbo software enable)
                if sel_pbctl='1' then
                    pbctl_reg <= D;
                end if;

                -- 4-6. PBI RAM/ROM -- only in PBI mode
                if VERA_IS_PBI then

                    -- 4. PBI RAM read
                    if sel_pbi_ram='1' and RNW='1' then
                        ram_offs := to_integer(addr - C_PBI_RAM_LO);
                        d_pbi    <= pbi_ram(ram_offs);
                    end if;

                    -- 5. PBI RAM write
                    if sel_pbi_ram='1' and RNW='0' then
                        ram_offs          := to_integer(addr - C_PBI_RAM_LO);
                        pbi_ram(ram_offs) <= D;
                    end if;

                    -- 6. PBI ROM read (ROM is read-only; writes silently ignored)
                    if sel_pbi_rom='1' and RNW='1' then
                        rom_offs := to_integer(addr - C_PBI_ROM_LO);
                        d_rom    <= pbi_rom(rom_offs);
                    end if;

                end if;

            end if;
        end if;
    end process p_main;

    -- =========================================================================
    -- Bus control outputs (combinational, gated by PHI2)
    -- =========================================================================

    VERA_CS <= vera_cs_latch;

    -- MPD: PBI mode only -- blocks Atari from driving D for PBI ROM reads.
    MPD <= '0' when VERA_IS_PBI and sel_pbi_rom='1' and PHI2='1' else '1';

    -- EXTSEL: blocks Atari internal RAM from responding.
    --   PBI mode : PBI RAM range + RAMbo
    --   CCTL mode: RAMbo only (no PBI RAM in cartridge mode)
    EXTSEL <= '0' when VERA_IS_PBI  and (sel_pbi_ram='1' or sel_rambo='1') and PHI2='1' else
              '0' when not VERA_IS_PBI and sel_rambo='1'                    and PHI2='1' else
              '1';

    -- Data bus: FPGA drives D[7:0] for PBI RAM/ROM reads.
    -- In CCTL mode and for RAMbo reads: FPGA stays high-Z (VERA or SRAM drives bus).
    D <= d_pbi when VERA_IS_PBI and sel_pbi_ram='1' and RNW='1' and PHI2='1' else
         d_rom when VERA_IS_PBI and sel_pbi_rom='1' and RNW='1' and PHI2='1' else
         (others => 'Z');

    -- =========================================================================
    -- External SRAM control (combinational, gated by PHI2)
    --
    -- Timing budget (AS6C4008-55, tAA=55 ns, 6502 @ 1.79 MHz):
    --   PHI2 rise -> CE# low        ~0 ns  (combinational)
    --   CE# low   -> data valid    <=55 ns
    --   PHI2 high -> 6502 samples  ~249 ns (279 ns - 30 ns tDSR)
    --   Read margin                ~194 ns (ok)
    --   Write: WE# pulse = PHI2-high ~279 ns >> tWC=55 ns (ok)
    -- =========================================================================
    SRAM_A_BANK <= rambo_bank;

    SRAM_CE_N <= '0' when sel_rambo='1' and PHI2='1'              else '1';
    SRAM_OE_N <= '0' when sel_rambo='1' and PHI2='1' and RNW='1'  else '1';
    SRAM_WE_N <= '0' when sel_rambo='1' and PHI2='1' and RNW='0'  else '1';

end architecture Behavioral;
