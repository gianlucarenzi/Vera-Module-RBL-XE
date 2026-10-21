#!/bin/sh
# Simulate C6502_Interface + AS6C4008 RAMbo with GHDL (OSS CAD Suite).
# Usage: sim/run.sh            (from firmware/FPGA or anywhere)
set -e
cd "$(dirname "$0")"
W=work
mkdir -p $W
OPT="--std=08 --workdir=$W"
ghdl -a $OPT sb_io.vhd ../custom_types.vhd ../pbi_rom_pkg.vhd ../C6502_Interface.vhd as6c4008.vhd tb_rambo.vhd
ghdl -e $OPT tb_rambo

run() {
    echo "##### $*"
    ghdl -r $OPT tb_rambo "$@" --ieee-asserts=disable-at-0 2>&1 | grep -E 'FAIL|AS6C4008|---|===' | sed 's/^.*(report note): //'
}

for pbi in true false; do
    run -gVERA_IS_PBI=$pbi -gtMDS_NS=40
done
# write-data timing sweep (6502 tMDS, PBI build)
for t in 60 80 100 120 150; do
    run -gVERA_IS_PBI=true -gtMDS_NS=$t
done
# data hold vs. FPGA WE# delay
run -gVERA_IS_PBI=true -gtHW_NS=10 -gtFPGA_NS=12
