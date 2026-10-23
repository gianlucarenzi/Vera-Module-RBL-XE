library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

-- =============================================================================
-- tb_rambo  --  C6502_Interface + AS6C4008 + Atari XL/XE bus model
--
-- Reference behaviour (RAMBO manual p.14, Altirra HW ref. §8, and the ESP32
-- implementation in firmware/MCU/src/main.cpp):
--   * $D303 PBCTL bit 2 selects what $D301 is: 0 = DDR, 1 = PORTB output reg.
--     Only PORTB writes (PBCTL[2]=1) update the bank register.
--   * PORTB bit 4 (RAME) = 0 -> extended RAM visible at $4000-$7FFF,
--                         = 1 -> Atari internal RAM visible.
--   * bank = { PORTB[6], PORTB[5], PORTB[3], PORTB[2] }  -> SRAM A[17:14]
--   * RAMBO_EN pin = 0 disables everything.
--
-- 6502 bus model (Atari PHI2 = 1.79 MHz):
--   address/RNW valid tADS after PHI2 fall, write data valid tMDS after
--   PHI2 rise and held tHW after PHI2 fall; read data sampled at PHI2 fall,
--   must be stable for tDSU before and tHR after.
--   All FPGA outputs are delayed by tFPGA (pad-to-pad combinational delay).
-- =============================================================================

entity tb_rambo is
    generic (
        VERA_IS_PBI : boolean := true;
        tADS_NS  : natural := 100;
        tMDS_NS  : natural := 40;
        tHW_NS   : natural := 30;
        tDSU_NS  : natural := 30;
        tHR_NS   : natural := 10;
        tFPGA_NS : natural := 10
    );
end entity tb_rambo;

architecture sim of tb_rambo is

    constant tADS  : time := tADS_NS  * 1 ns;
    constant tMDS  : time := tMDS_NS  * 1 ns;
    constant tHW   : time := tHW_NS   * 1 ns;
    constant tDSU  : time := tDSU_NS  * 1 ns;
    constant tHR   : time := tHR_NS   * 1 ns;
    constant tFPGA : time := tFPGA_NS * 1 ns;

    constant T_CLK      : time := 40 ns;    -- 25 MHz
    constant T_PHI2     : time := 559 ns;   -- 1.79 MHz
    constant T_PHI2_HI  : time := 280 ns;

    signal CLK        : std_logic := '0';
    signal PHI2       : std_logic := '0';
    signal A          : std_logic_vector(15 downto 0) := x"FFFF";
    signal D          : std_logic_vector(7 downto 0)  := (others => 'Z');
    signal RNW        : std_logic := '1';
    signal PBI_ID_SEL : std_logic;
    signal RAMBO_EN   : std_logic := '1';

    signal vera_cs_i, mpd_i, extsel_i       : std_logic;
    signal bank_i                           : std_logic_vector(3 downto 0);
    signal ce_n_i, oe_n_i, we_n_i           : std_logic;

    signal VERA_CS, MPD, EXTSEL             : std_logic;
    signal SRAM_A_BANK                      : std_logic_vector(3 downto 0);
    signal SRAM_CE_N, SRAM_OE_N, SRAM_WE_N  : std_logic;
    signal sram_a                           : std_logic_vector(18 downto 0);
    signal sram_viol                        : natural;
    signal sram_last_wr                     : std_logic_vector(18 downto 0);
    signal sram_ce_count                    : natural := 0;
    signal done                             : boolean := false;
    -- control levels of the last bus cycle, sampled at PHI2 fall
    signal cs_seen, mpd_seen, ext_seen      : std_logic := '1';

begin

    CLK  <= not CLK after T_CLK / 2 when not done;

    -- PHI2 asynchronous to CLK (odd start offset)
    p_phi2 : process
    begin
        wait for 7 ns;
        while not done loop
            PHI2 <= '1'; wait for T_PHI2_HI;
            PHI2 <= '0'; wait for T_PHI2 - T_PHI2_HI;
        end loop;
        wait;
    end process;

    -- PBI_ID_SEL jumper on D[7]
    PBI_ID_SEL <= D(7);

    dut : entity work.C6502_Interface
        generic map (VERA_IS_PBI => VERA_IS_PBI)
        port map (
            CLK => CLK, A => A, D => D, RNW => RNW, PHI2 => PHI2,
            PBI_ID_SEL => PBI_ID_SEL, RAMBO_EN => RAMBO_EN,
            VERA_CS => vera_cs_i, MPD => mpd_i, EXTSEL => extsel_i,
            SRAM_A_BANK => bank_i,
            SRAM_CE_N => ce_n_i, SRAM_OE_N => oe_n_i, SRAM_WE_N => we_n_i);

    VERA_CS     <= transport vera_cs_i after tFPGA;
    MPD         <= transport mpd_i     after tFPGA;
    EXTSEL      <= transport extsel_i  after tFPGA;
    SRAM_A_BANK <= transport bank_i    after tFPGA;
    SRAM_CE_N   <= transport ce_n_i    after tFPGA;
    SRAM_OE_N   <= transport oe_n_i    after tFPGA;
    SRAM_WE_N   <= transport we_n_i    after tFPGA;

    -- SRAM A18 tied to GND, A[13:0] straight from the 6502 bus
    sram_a <= '0' & SRAM_A_BANK & A(13 downto 0);

    sram : entity work.as6c4008
        port map (A => sram_a, DQ => D, CE_N => SRAM_CE_N, OE_N => SRAM_OE_N,
                  WE_N => SRAM_WE_N, viol => sram_viol, last_wr_addr => sram_last_wr);

    p_ce_count : process (SRAM_CE_N)
    begin
        if falling_edge(SRAM_CE_N) then
            sram_ce_count <= sram_ce_count + 1;
        end if;
    end process;

    p_ctrl_seen : process (PHI2)
    begin
        if falling_edge(PHI2) then
            cs_seen  <= VERA_CS;
            mpd_seen <= MPD;
            ext_seen <= EXTSEL;
        end if;
    end process;

    -- -------------------------------------------------------------------------
    -- Atari internal RAM ($0000-$CFFF), disabled by EXTSEL=0.
    -- Drives the bus 40 ns after PHI2 rise (DRAM CAS), data valid at +150 ns.
    -- -------------------------------------------------------------------------
    p_atari_ram : process (PHI2, EXTSEL, A, RNW)
        type mem_t is array (0 to 16#CFFF#) of std_logic_vector(7 downto 0);
        variable mem     : mem_t := (others => x"00");
        variable driving : boolean := false;
    begin
        if falling_edge(PHI2) and RNW = '0' and EXTSEL = '1' and
           unsigned(A) < x"D000" then
            mem(to_integer(unsigned(A))) := D;
        end if;

        if PHI2 = '1' and RNW = '1' and EXTSEL = '1' and unsigned(A) < x"D000" then
            if not driving then
                driving := true;
                D <= transport (others => 'X') after 40 ns;
                D <= transport mem(to_integer(unsigned(A))) after 150 ns;
            end if;
        elsif driving then
            driving := false;
            D <= transport (others => 'Z') after 25 ns;   -- DRAM output hold
        end if;
    end process;

    -- -------------------------------------------------------------------------
    -- 6502 CPU / test sequence
    -- -------------------------------------------------------------------------
    p_cpu : process
        variable errors  : natural := 0;
        variable checks  : natural := 0;
        variable t_fall  : time := 0 ns;
        variable rdata   : std_logic_vector(7 downto 0);
        variable ce_prev : natural;

        procedure fail (msg : string) is
        begin
            errors := errors + 1;
            report "FAIL: " & msg severity error;
        end procedure;

        procedure check (cond : boolean; msg : string) is
        begin
            checks := checks + 1;
            if not cond then
                fail(msg);
            end if;
        end procedure;

        -- one bus cycle; returns tHR after PHI2 fall
        procedure cyc (addr : std_logic_vector(15 downto 0); rw : std_logic;
                       wdata : std_logic_vector(7 downto 0);
                       rd : out std_logic_vector(7 downto 0)) is
        begin
            wait for t_fall + tADS - now;
            A   <= addr;
            RNW <= rw;
            wait until PHI2 = '1';
            if rw = '0' then
                D <= transport wdata after tMDS;
            end if;
            wait until PHI2 = '0';
            t_fall := now;
            rd := D;
            if rw = '0' then
                D <= transport (others => 'Z') after tHW;
            elsif D'last_event < tDSU then
                fail("read $" & to_hstring(addr) & ": data changed " &
                     time'image(D'last_event) & " before PHI2 fall (< tDSU)");
            end if;
            wait for tHR;
            if rw = '1' and D /= rd then
                if unsigned(addr) >= x"D600" and unsigned(addr) <= x"DFFF" then
                    -- FPGA BRAM data: hold comes from the board data buffer
                    -- (disabled by PHI2 fall), not modelled here
                    report "note: read $" & to_hstring(addr) &
                           " hold after PHI2 fall set by the board buffer";
                else
                    fail("read $" & to_hstring(addr) & ": data not held tHR after PHI2 fall");
                end if;
            end if;
        end procedure;

        procedure wr (addr : std_logic_vector(15 downto 0);
                      data : std_logic_vector(7 downto 0)) is
            variable dummy : std_logic_vector(7 downto 0);
        begin
            cyc(addr, '0', data, dummy);
        end procedure;

        procedure rd_chk (addr : std_logic_vector(15 downto 0);
                          exp  : std_logic_vector(7 downto 0); msg : string) is
            variable v : std_logic_vector(7 downto 0);
        begin
            cyc(addr, '1', x"00", v);
            check(v = exp, msg & ": read $" & to_hstring(addr) & " = " &
                  to_hstring(v) & ", expected " & to_hstring(exp));
        end procedure;

        -- one read cycle, then check the control levels seen during it
        procedure ctl_chk (addr : std_logic_vector(15 downto 0);
                           cs, mpd_e, ext : std_logic; msg : string) is
            variable v : std_logic_vector(7 downto 0);
        begin
            cyc(addr, '1', x"00", v);
            check(cs_seen = cs and mpd_seen = mpd_e and ext_seen = ext,
                  msg & ": $" & to_hstring(addr) & " VERA_CS/MPD/EXTSEL = " &
                  to_string(cs_seen) & to_string(mpd_seen) & to_string(ext_seen) &
                  ", expected " & to_string(cs) & to_string(mpd_e) & to_string(ext));
        end procedure;

        -- PORTB value selecting extended bank `b` with RAME (bit 4) = 0
        function portb_ext (b : natural) return std_logic_vector is
            variable v : std_logic_vector(3 downto 0);
        begin
            v := std_logic_vector(to_unsigned(b, 4));
            return '1' & v(3) & v(2) & '0' & v(1) & v(0) & "11";
        end function;

        function sig (b : natural; k : natural) return std_logic_vector is
        begin
            return std_logic_vector(to_unsigned((b * 16 + k * 5 + 7) mod 256, 8));
        end function;

        type addr_list is array (natural range <>) of std_logic_vector(15 downto 0);
        constant WIN : addr_list := (x"4000", x"5A5A", x"6AB5", x"7FFF");

    begin
        wait until PHI2 = '0';
        t_fall := now;
        wait for tHR;
        for i in 1 to 4 loop                 -- let the synchroniser settle
            rd_chk(x"0100", x"00", "idle");
        end loop;

        -- ------------------------------------------------------------- OS init
        report "--- T1: OS-like PIA init, internal RAM at $4000-$7FFF";
        wr(x"D303", x"30");                  -- PBCTL bit2=0 -> $D301 = DDR
        wr(x"D301", x"FF");                  -- DDR: all outputs
        wr(x"D303", x"34");                  -- PBCTL bit2=1 -> $D301 = PORTB
        wr(x"D301", x"FF");                  -- PORTB = $FF (RAME=1)

        ce_prev := sram_ce_count;
        for k in WIN'range loop
            wr(WIN(k), sig(99, k));
        end loop;
        wr(x"3FFF", x"C3");
        wr(x"8000", x"3C");
        for k in WIN'range loop
            rd_chk(WIN(k), sig(99, k), "T1 main RAM (PORTB=$FF)");
        end loop;
        check(sram_ce_count = ce_prev,
              "T1: SRAM selected " & integer'image(sram_ce_count - ce_prev) &
              " times with PORTB bit4=1 (RAME=1): internal RAM must respond");

        -- ------------------------------------------------- DDR writes ignored
        report "--- T2: write to $D301 while PBCTL[2]=0 (DDR) must not switch banks";
        wr(x"D303", x"30");
        wr(x"D301", portb_ext(0));           -- DDR write, looks like bank 0/RAME=0
        wr(x"D303", x"34");
        ce_prev := sram_ce_count;
        rd_chk(x"4000", sig(99, 0), "T2 main RAM after DDR write");
        check(sram_ce_count = ce_prev, "T2: DDR write to $D301 enabled RAMbo");

        -- -------------------------------------------- 16 banks, write pattern
        report "--- T3: write signature into all 16 banks";
        for b in 0 to 15 loop
            wr(x"D301", portb_ext(b));
            for k in WIN'range loop
                wr(WIN(k), sig(b, k));
                wait for tFPGA;                  -- let WE# rise reach the SRAM
                check(sram_last_wr = '0' & std_logic_vector(to_unsigned(b, 4)) & WIN(k)(13 downto 0),
                      "T3 bank " & integer'image(b) & " $" & to_hstring(WIN(k)) &
                      ": SRAM physical address $" & to_hstring(sram_last_wr) &
                      ", expected bank in A[17:14] and A18=0");
            end loop;
        end loop;
        -- outside the window: still internal RAM while RAME=0
        wr(x"3FFF", x"A1");
        wr(x"8000", x"1A");

        report "--- T4: read back all 16 banks (no aliasing)";
        for b in 15 downto 0 loop
            wr(x"D301", portb_ext(b));
            for k in WIN'range loop
                rd_chk(WIN(k), sig(b, k), "T4 bank " & integer'image(b));
            end loop;
        end loop;

        report "--- T5: back to main memory (PORTB=$FF): internal RAM untouched";
        wr(x"D301", x"FF");
        for k in WIN'range loop
            rd_chk(WIN(k), sig(99, k), "T5 main RAM after bank traffic");
        end loop;
        rd_chk(x"3FFF", x"A1", "T5 $3FFF (outside window)");
        rd_chk(x"8000", x"1A", "T5 $8000 (outside window)");

        report "--- T6: RAMBO_EN=0 hardware override";
        RAMBO_EN <= '0';
        wr(x"D301", portb_ext(5));
        ce_prev := sram_ce_count;
        wr(x"4000", x"77");
        rd_chk(x"4000", x"77", "T6 RAMBO_EN=0");
        check(sram_ce_count = ce_prev, "T6: SRAM selected with RAMBO_EN=0");
        wr(x"4000", sig(99, 0));             -- restore main RAM
        RAMBO_EN <= '1';
        wr(x"D301", portb_ext(5));
        rd_chk(x"4000", sig(5, 0), "T6 bank 5 intact after override");
        wr(x"D301", x"FF");

        -- ------------------------------------------ select latch / chip selects
        -- VERA_CS only for the VERA registers with the card selected, never
        -- for other addresses (ESP32 bus_decode.h, Atari PBI rule).
        if VERA_IS_PBI then
            report "--- T7: PBI latch $D1FF (PBI_ID_SEL on D7), VERA_CS, MPD, EXTSEL";
            ctl_chk(x"D100", '1', '1', '1', "T7 not selected");
            ctl_chk(x"D11F", '1', '1', '1', "T7 not selected");
            ctl_chk(x"D600", '1', '1', '1', "T7 not selected");
            ctl_chk(x"D803", '1', '1', '1', "T7 not selected (Math Pack)");
            wr(x"D1FF", x"80");
            ctl_chk(x"D100", '0', '1', '0', "T7 selected");
            ctl_chk(x"D11F", '0', '1', '0', "T7 selected");
            ctl_chk(x"D120", '1', '1', '0', "T7 selected");
            ctl_chk(x"D1FE", '1', '1', '0', "T7 selected");
            ctl_chk(x"401F", '1', '1', '1', "T7 selected");
            ctl_chk(x"A010", '1', '1', '1', "T7 selected");
            ctl_chk(x"D010", '1', '1', '1', "T7 selected");
            ctl_chk(x"D803", '1', '0', '1', "T7 selected (PBI ROM)");
            wr(x"D600", x"5A");
            wr(x"D7FF", x"A5");
            rd_chk(x"D600", x"5A", "T7 PBI RAM");
            rd_chk(x"D7FF", x"A5", "T7 PBI RAM");
            wr(x"D1FF", x"00");
            ctl_chk(x"D100", '1', '1', '1', "T7 deselected");
            ctl_chk(x"D600", '1', '1', '1', "T7 deselected");
            ctl_chk(x"D803", '1', '1', '1', "T7 deselected (Math Pack)");
        else
            report "--- T7: CCTL latch $D5FF, VERA_CS";
            ctl_chk(x"D500", '1', '1', '1', "T7 not selected");
            wr(x"D5FF", x"80");
            ctl_chk(x"D500", '0', '1', '1', "T7 selected");
            ctl_chk(x"D5FE", '0', '1', '1', "T7 selected");
            ctl_chk(x"D5FF", '1', '1', '1', "T7 selected (latch)");
            ctl_chk(x"D100", '1', '1', '1', "T7 selected");
            ctl_chk(x"401F", '1', '1', '1', "T7 selected");
            ctl_chk(x"A010", '1', '1', '1', "T7 selected");
            wr(x"D5FF", x"00");
            ctl_chk(x"D500", '1', '1', '1', "T7 deselected");
        end if;

        rd_chk(x"0100", x"00", "idle");
        report "=== RESULT (PBI=" & boolean'image(VERA_IS_PBI) &
               ", tMDS=" & integer'image(tMDS_NS) & "ns, tHW=" & integer'image(tHW_NS) &
               "ns, tFPGA=" & integer'image(tFPGA_NS) & "ns): " &
               integer'image(checks) & " checks, " & integer'image(errors) &
               " functional errors, " & integer'image(sram_viol) &
               " SRAM timing violations";
        done <= true;
        wait;
    end process;

end architecture sim;
