library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

package custom_types is
    -- ROM (2048 x 8 bit) per l'area $D800-$DFFF
    type rom_array is array (0 to 2047) of std_logic_vector(7 downto 0);
    
    -- RAM (512 x 8 bit) per l'area $D600-$D7FF
    type ram_512_array is array (0 to 511) of std_logic_vector(7 downto 0);

end package custom_types;
