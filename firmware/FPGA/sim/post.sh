#!/bin/sh
# Post-synthesis simulation: synthesise C6502_Interface with Yosys (GHDL
# plugin, same flow as the Makefile) and run tb_rambo_post.v on the mapped
# iCE40 netlist with Icarus Verilog, for both PBI and CCTL builds.
# Usage: sim/post.sh            (from firmware/FPGA or anywhere)
set -e
cd "$(dirname "$0")"
W=work/post
mkdir -p $W
CELLS="$(yosys-config --datdir)/ice40/cells_sim.v"

for pbi in true false; do
    [ $pbi = true ] && P=1 || P=0
    yosys -q -m ghdl -p \
        "ghdl --std=08 -gVERA_IS_PBI=$pbi ../custom_types.vhd ../pbi_rom_pkg.vhd ../C6502_Interface.vhd -e C6502_Interface; \
         synth_ice40 -top C6502_Interface; write_verilog -noattr $W/netlist_$pbi.v" > $W/yosys_$pbi.log 2>&1
    for t in 40 120 150; do
        iverilog -g2012 -o $W/tb_$pbi -DNO_ICE40_DEFAULT_ASSIGNMENTS= \
            -Ptb_rambo_post.PBI=$P -Ptb_rambo_post.TMDS=$t \
            tb_rambo_post.v $W/netlist_$pbi.v "$CELLS" 2> $W/iverilog.log
        vvp -n $W/tb_$pbi | grep -E 'FAIL|ERROR|==='
    done
done
