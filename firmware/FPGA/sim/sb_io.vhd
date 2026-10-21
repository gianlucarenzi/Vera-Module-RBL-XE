library ieee;
use ieee.std_logic_1164.all;

-- =============================================================================
-- SB_IO  --  simulation model of the iCE40 I/O primitive (subset used by
-- C6502_Interface: PIN_TYPE 101001 = combinational OE output, simple input).
-- Synthesis uses the Yosys/nextpnr primitive instead (black box in GHDL).
-- =============================================================================

entity SB_IO is
    generic (
        PIN_TYPE : std_logic_vector(5 downto 0) := "000000";
        PULLUP   : std_logic := '0'
    );
    port (
        PACKAGE_PIN   : inout std_logic := 'Z';
        OUTPUT_ENABLE : in    std_logic;
        D_OUT_0       : in    std_logic;
        D_IN_0        : out   std_logic
    );
end entity SB_IO;

architecture sim of SB_IO is
begin
    PACKAGE_PIN <= D_OUT_0 when OUTPUT_ENABLE = '1' else 'Z';
    D_IN_0      <= PACKAGE_PIN;
end architecture sim;
