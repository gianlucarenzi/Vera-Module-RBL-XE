library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

-- =============================================================================
-- as6c4008  --  behavioural model of the Alliance AS6C4008-55 (512K x 8 async SRAM)
--
-- Read : DQ goes 'X' tOH after any change, then valid at
--        max(addr + tAA, CE# fall + tACE, OE# fall + tOE).
--        On CE#/OE# release DQ holds tOH, is 'X' until tOHZ, then 'Z'.
-- Write: CE#=0 and WE#=0; data latched at the first rising edge of CE#/WE#.
--        Checks tWP/tCW, tAW, tAS (address stable for the whole pulse),
--        tDW and data not 'X'/'Z'/'U' at the end of the write.
-- Every timing violation increments the `viol` output.
-- =============================================================================

entity as6c4008 is
    generic (
        tAA  : time := 55 ns;
        tACE : time := 55 ns;
        tOE  : time := 30 ns;
        tOH  : time := 10 ns;
        tOHZ : time := 20 ns;
        tWP  : time := 45 ns;
        tAW  : time := 50 ns;
        tDW  : time := 25 ns
    );
    port (
        A    : in    std_logic_vector(18 downto 0);
        DQ   : inout std_logic_vector(7 downto 0) := (others => 'Z');
        CE_N : in    std_logic;
        OE_N : in    std_logic;
        WE_N : in    std_logic;
        viol : out   natural := 0;
        -- debug: physical address of the last completed write
        last_wr_addr : out std_logic_vector(18 downto 0)
    );
end entity as6c4008;

architecture behav of as6c4008 is
begin

    p_sram : process (A, CE_N, OE_N, WE_N, DQ)
        type mem_t is array (0 to 2**19 - 1) of std_logic_vector(7 downto 0);
        variable mem      : mem_t := (others => (others => 'X'));
        variable t_a      : time := 0 ns;
        variable t_ce     : time := 0 ns;
        variable t_oe     : time := 0 ns;
        variable t_dq     : time := 0 ns;
        variable t_wstart : time := 0 ns;
        variable writing  : boolean := false;
        variable reading  : boolean := false;
        variable t_valid  : time;
        variable data     : std_logic_vector(7 downto 0);
        variable nviol    : natural := 0;

        impure function rd return std_logic_vector is
        begin
            if is_x(A) then
                return (7 downto 0 => 'X');
            end if;
            return mem(to_integer(unsigned(A)));
        end function;

        procedure violation (msg : string) is
        begin
            nviol := nviol + 1;
            viol  <= nviol;
            report "AS6C4008 @" & to_hstring(A) & ": " & msg severity error;
        end procedure;
    begin
        if A'event    then t_a  := now; end if;
        if CE_N'event and CE_N = '0' then t_ce := now; end if;
        if OE_N'event and OE_N = '0' then t_oe := now; end if;
        if DQ'event   then t_dq := now; end if;

        -- ---------------------------------------------------------------- write
        if writing and A'event then
            violation("address changed during write pulse (tAS/tWR)");
        end if;

        if writing and (CE_N /= '0' or WE_N /= '0') then
            writing := false;
            if now - t_wstart < tWP then
                violation("write pulse " & time'image(now - t_wstart) & " < tWP");
            end if;
            if now - t_a < tAW then
                violation("address setup to end of write " & time'image(now - t_a) & " < tAW");
            end if;
            if now - t_dq < tDW then
                violation("data setup to end of write " & time'image(now - t_dq) & " < tDW");
            end if;
            if is_x(DQ) then
                violation("written data is not valid: " & to_string(DQ));
            end if;
            if not is_x(A) then
                mem(to_integer(unsigned(A))) := DQ;
            end if;
            last_wr_addr <= A;
        elsif not writing and CE_N = '0' and WE_N = '0' then
            writing  := true;
            t_wstart := now;
            if is_x(A) then
                violation("write started with undefined address");
            end if;
        end if;

        -- ----------------------------------------------------------------- read
        if CE_N = '0' and OE_N = '0' and WE_N = '1' then
            if A'event or CE_N'event or OE_N'event or WE_N'event or not reading then
                reading := true;
                t_valid := maximum(t_a + tAA, maximum(t_ce + tACE, t_oe + tOE));
                data    := rd;
                DQ <= transport (others => 'X') after tOH;
                if t_valid - now > tOH then
                    DQ <= transport data after t_valid - now;
                else
                    DQ <= transport data after tOH;
                end if;
            end if;
        elsif reading then
            reading := false;
            DQ <= transport (others => 'X') after tOH;
            DQ <= transport (others => 'Z') after tOHZ;
        end if;
    end process p_sram;

end architecture behav;
