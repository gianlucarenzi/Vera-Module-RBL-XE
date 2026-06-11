#!/usr/bin/env python3
"""Convert a 6502 ROM binary into a VHDL package for iCE40 BRAM initialisation.

Usage:
    python3 gen_pbi_rom_pkg.py <input.bin> <output.vhd>

The script takes the first 2048 bytes of the binary (PBI ROM at $D800-$DFFF)
and emits a VHDL package containing a rom_array constant (custom_types.vhd).
Binaries shorter than 2048 bytes are zero-padded.
"""

import sys

ROM_SIZE = 2048  # bytes, covers $D800-$DFFF

if len(sys.argv) != 3:
    print(__doc__)
    sys.exit(1)

with open(sys.argv[1], 'rb') as f:
    data = f.read()

rom = bytearray(data[:ROM_SIZE])
if len(rom) < ROM_SIZE:
    rom += b'\x00' * (ROM_SIZE - len(rom))

entries = ',\n'.join('        %4d => x"%02X"' % (i, b) for i, b in enumerate(rom))

vhd = f"""\
-- Auto-generated from {sys.argv[1]}
-- Do not edit manually -- regenerate with: make fpga-rom
--   (in firmware/MCU/6502/)

library ieee;
use ieee.std_logic_1164.all;
use work.custom_types.all;

package pbi_rom_pkg is

    constant PBI_ROM_INIT : rom_array := (
{entries}
    );

end package pbi_rom_pkg;
"""

with open(sys.argv[2], 'w') as f:
    f.write(vhd)

print(f"Written {ROM_SIZE} bytes -> {sys.argv[2]}")
