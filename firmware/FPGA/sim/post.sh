#!/bin/sh
# Post-synthesis simulation: synthesise C6502_Interface with Yosys (GHDL
# plugin, or ghdl synth --out=verilog without it: same flow as the Makefile)
# and run tb_rambo_post.v on the mapped iCE40 netlist with Icarus Verilog,
# for both PBI and CCTL builds.
# Usage: sim/post.sh            (from firmware/FPGA or anywhere)
set -e
cd "$(dirname "$0")"
W=work/post
mkdir -p $W
CELLS="$(yosys-config --datdir 2>/dev/null || echo /usr/share/yosys)/ice40/cells_sim.v"
SRCS="../custom_types.vhd ../pbi_rom_pkg.vhd ../C6502_Interface.vhd"
# ghdl-yosys-plugin if yosys has it, otherwise GHDL writes Verilog for yosys
PLUGIN=$(yosys -m ghdl -p '' >/dev/null 2>&1 && echo yes || true)

for pbi in true false; do
    [ $pbi = true ] && P=1 || P=0
    if [ -n "$PLUGIN" ]; then
        yosys -q -m ghdl -p \
            "ghdl --std=08 -gVERA_IS_PBI=$pbi $SRCS -e C6502_Interface; \
             synth_ice40 -top C6502_Interface; write_verilog -noattr $W/netlist_$pbi.v" > $W/yosys_$pbi.log 2>&1
    else
        ghdl synth --std=08 --workdir=$W -gVERA_IS_PBI=$pbi --out=verilog $SRCS -e C6502_Interface \
            > $W/ghdl_$pbi.v 2> $W/ghdl_$pbi.log
        yosys -q -p "read_verilog $W/ghdl_$pbi.v; synth_ice40 -top C6502_Interface; \
             write_verilog -noattr $W/netlist_$pbi.v" > $W/yosys_$pbi.log 2>&1
    fi
    for t in 40 120 150; do
        iverilog -g2012 -o $W/tb_$pbi -DNO_ICE40_DEFAULT_ASSIGNMENTS= \
            -Ptb_rambo_post.PBI=$P -Ptb_rambo_post.TMDS=$t \
            tb_rambo_post.v $W/netlist_$pbi.v "$CELLS" 2> $W/iverilog.log
        vvp -n $W/tb_$pbi | grep -E 'FAIL|ERROR|==='
    done
done
