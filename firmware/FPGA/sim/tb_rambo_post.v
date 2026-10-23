`timescale 1ns / 1ps
// =============================================================================
// tb_rambo_post  --  post-synthesis check of the Yosys iCE40 netlist
//
// Same scenario as tb_rambo.vhd (T1-T7), run on the mapped netlist with the
// Yosys cells_sim.v primitives, so it covers what RTL simulation cannot:
// SB_IO tri-state on D[7:0], BRAM mapping, register init values.
// The SRAM and Atari RAM models are functional (no timing checks).
//
// Parameters: PBI = 1/0 (must match the netlist), TMDS = 6502 write data delay.
// =============================================================================

module tb_rambo_post;
    parameter PBI  = 1;
    parameter TMDS = 120;

    reg CLK = 0;
    always #20 CLK = ~CLK;                      // 25 MHz

    reg PHI2 = 0;                               // 1.79 MHz, async to CLK
    initial begin
        #7;
        forever begin PHI2 = 1; #280; PHI2 = 0; #279; end
    end

    reg  [15:0] A = 16'hFFFF;
    reg         RNW = 1;
    reg  [7:0]  cpu_d = 8'h00;
    reg         cpu_oe = 0;
    reg         RAMBO_EN = 1;
    wire [7:0]  D;
    wire        VERA_CS, MPD, EXTSEL, CE_N, OE_N, WE_N;
    wire [3:0]  BANK;

    assign D = cpu_oe ? cpu_d : 8'hzz;

    C6502_Interface dut (
        .CLK(CLK), .A(A), .D(D), .RNW(RNW), .PHI2(PHI2),
        .PBI_ID_SEL(D[7]), .RAMBO_EN(RAMBO_EN),
        .VERA_CS(VERA_CS), .MPD(MPD), .EXTSEL(EXTSEL),
        .SRAM_A_BANK(BANK), .SRAM_CE_N(CE_N), .SRAM_OE_N(OE_N), .SRAM_WE_N(WE_N));

    // ---------------------------------------------------------------- SRAM
    reg  [7:0]  sram [0:(1 << 19) - 1];
    wire [18:0] sa = {1'b0, BANK, A[13:0]};
    reg  [18:0] sram_last_wr = 0;
    integer     sram_bad_wr = 0;
    integer     ce_cnt = 0;

    assign #30 D = (!CE_N && !OE_N && WE_N) ? sram[sa] : 8'hzz;

    always @(negedge CE_N) ce_cnt = ce_cnt + 1;
    always @(posedge WE_N) if (!CE_N) begin
        if (^D === 1'bx) begin
            sram_bad_wr = sram_bad_wr + 1;
            $display("ERROR @%0t: SRAM write $%h with invalid data %b", $time, sa, D);
        end
        sram[sa]     = D;
        sram_last_wr = sa;
    end

    // ---------------------------------------- Atari internal RAM $0000-$CFFF
    reg [7:0] mram [0:16'hCFFF];
    integer i;
    initial for (i = 0; i <= 16'hCFFF; i = i + 1) mram[i] = 8'h00;

    assign #40 D = (PHI2 && RNW && EXTSEL && A < 16'hD000) ? mram[A] : 8'hzz;
    always @(negedge PHI2) if (!RNW && EXTSEL && A < 16'hD000) mram[A] = D;

    // ----------------------------------------------------------------- CPU
    integer errors = 0, checks = 0;
    reg [7:0] rd;
    reg [2:0] ctl;                              // {VERA_CS, MPD, EXTSEL} seen in the cycle

    // entered 20 ns after PHI2 fall, returns 20 ns after the next PHI2 fall
    task cyc(input [15:0] addr, input rw, input [7:0] wdata);
        begin
            #80;
            A = addr; RNW = rw;
            @(posedge PHI2);
            if (!rw) begin
                #(TMDS); cpu_d = wdata; cpu_oe = 1;
                #(250 - TMDS);
            end else begin
                #250;
            end
            rd = D;
            ctl = {VERA_CS, MPD, EXTSEL};
            @(negedge PHI2);
            #20 cpu_oe = 0;
        end
    endtask

    task wr(input [15:0] addr, input [7:0] data);
        cyc(addr, 1'b0, data);
    endtask

    task chk(input cond, input [8*48-1:0] msg);
        begin
            checks = checks + 1;
            if (!cond) begin
                errors = errors + 1;
                $display("FAIL @%0t: %0s", $time, msg);
            end
        end
    endtask

    task rd_chk(input [15:0] addr, input [7:0] exp, input [8*48-1:0] msg);
        begin
            cyc(addr, 1'b1, 8'h00);
            checks = checks + 1;
            if (rd !== exp) begin
                errors = errors + 1;
                $display("FAIL @%0t: %0s: read $%h = %h, expected %h", $time, msg, addr, rd, exp);
            end
        end
    endtask

    // one read cycle, then check {VERA_CS, MPD, EXTSEL} seen during it
    task ctl_chk(input [15:0] addr, input [2:0] exp, input [8*48-1:0] msg);
        begin
            cyc(addr, 1'b1, 8'h00);
            checks = checks + 1;
            if (ctl !== exp) begin
                errors = errors + 1;
                $display("FAIL @%0t: %0s: $%h VERA_CS/MPD/EXTSEL = %b, expected %b", $time, msg, addr, ctl, exp);
            end
        end
    endtask

    function [7:0] portb_ext(input [3:0] b);
        portb_ext = {1'b1, b[3], b[2], 1'b0, b[1], b[0], 2'b11};
    endfunction

    function [7:0] sig(input [7:0] b, input [1:0] k);
        sig = b * 16 + k * 5 + 7;
    endfunction

    reg [15:0] win [0:3];
    integer b, k, ce_prev;

    initial begin
        win[0] = 16'h4000; win[1] = 16'h5A5A; win[2] = 16'h6AB5; win[3] = 16'h7FFF;
        @(negedge PHI2); #20;
        for (k = 0; k < 4; k = k + 1) rd_chk(16'h0100, 8'h00, "idle");

        // T1: OS init, PORTB=$FF -> internal RAM
        wr(16'hD303, 8'h30); wr(16'hD301, 8'hFF);
        wr(16'hD303, 8'h34); wr(16'hD301, 8'hFF);
        ce_prev = ce_cnt;
        for (k = 0; k < 4; k = k + 1) wr(win[k], sig(99, k));
        wr(16'h3FFF, 8'hC3); wr(16'h8000, 8'h3C);
        for (k = 0; k < 4; k = k + 1) rd_chk(win[k], sig(99, k), "T1 main RAM");
        chk(ce_cnt == ce_prev, "T1 SRAM selected with RAME=1");

        // T2: DDR write ignored
        wr(16'hD303, 8'h30); wr(16'hD301, portb_ext(0)); wr(16'hD303, 8'h34);
        ce_prev = ce_cnt;
        rd_chk(16'h4000, sig(99, 0), "T2 main RAM after DDR write");
        chk(ce_cnt == ce_prev, "T2 DDR write enabled RAMbo");

        // T3/T4: 16 banks
        for (b = 0; b < 16; b = b + 1) begin
            wr(16'hD301, portb_ext(b));
            for (k = 0; k < 4; k = k + 1) begin
                wr(win[k], sig(b, k));
                chk(sram_last_wr == {1'b0, b[3:0], win[k][13:0]}, "T3 SRAM physical address");
            end
        end
        wr(16'h3FFF, 8'hA1); wr(16'h8000, 8'h1A);
        for (b = 15; b >= 0; b = b - 1) begin
            wr(16'hD301, portb_ext(b));
            for (k = 0; k < 4; k = k + 1) rd_chk(win[k], sig(b, k), "T4 bank readback");
        end

        // T5: main RAM untouched
        wr(16'hD301, 8'hFF);
        for (k = 0; k < 4; k = k + 1) rd_chk(win[k], sig(99, k), "T5 main RAM");
        rd_chk(16'h3FFF, 8'hA1, "T5 $3FFF");
        rd_chk(16'h8000, 8'h1A, "T5 $8000");

        // T6: RAMBO_EN=0
        RAMBO_EN = 0;
        wr(16'hD301, portb_ext(5));
        ce_prev = ce_cnt;
        wr(16'h4000, 8'h77);
        rd_chk(16'h4000, 8'h77, "T6 RAMBO_EN=0");
        chk(ce_cnt == ce_prev, "T6 SRAM selected with RAMBO_EN=0");
        wr(16'h4000, sig(99, 0));
        RAMBO_EN = 1;
        wr(16'hD301, portb_ext(5));
        rd_chk(16'h4000, sig(5, 0), "T6 bank 5 intact");
        wr(16'hD301, 8'hFF);

        // T7: select latch, VERA_CS / MPD / EXTSEL (+ PBI RAM / ROM in PBI mode)
        if (PBI) begin
            ctl_chk(16'hD100, 3'b111, "T7 not selected");
            ctl_chk(16'hD11F, 3'b111, "T7 not selected");
            ctl_chk(16'hD600, 3'b111, "T7 not selected");
            ctl_chk(16'hD803, 3'b111, "T7 not selected (Math Pack)");
            wr(16'hD1FF, 8'h80);
            ctl_chk(16'hD100, 3'b010, "T7 selected");
            ctl_chk(16'hD11F, 3'b010, "T7 selected");
            ctl_chk(16'hD120, 3'b110, "T7 selected");
            ctl_chk(16'hD1FE, 3'b110, "T7 selected");
            ctl_chk(16'h401F, 3'b111, "T7 selected");
            ctl_chk(16'hA010, 3'b111, "T7 selected");
            ctl_chk(16'hD010, 3'b111, "T7 selected");
            ctl_chk(16'hD803, 3'b101, "T7 selected (PBI ROM)");
            wr(16'hD600, 8'h5A); wr(16'hD7FF, 8'hA5);
            rd_chk(16'hD600, 8'h5A, "T7 PBI RAM");
            rd_chk(16'hD7FF, 8'hA5, "T7 PBI RAM");
            rd_chk(16'hD803, 8'h80, "T7 PBI ROM ID byte");
            wr(16'hD1FF, 8'h00);
            ctl_chk(16'hD100, 3'b111, "T7 deselected");
            ctl_chk(16'hD600, 3'b111, "T7 deselected");
            ctl_chk(16'hD803, 3'b111, "T7 deselected (Math Pack)");
        end else begin
            ctl_chk(16'hD500, 3'b111, "T7 not selected");
            wr(16'hD5FF, 8'h80);
            ctl_chk(16'hD500, 3'b011, "T7 selected");
            ctl_chk(16'hD5FE, 3'b011, "T7 selected");
            ctl_chk(16'hD5FF, 3'b111, "T7 selected (latch)");
            ctl_chk(16'hD100, 3'b111, "T7 selected");
            ctl_chk(16'h401F, 3'b111, "T7 selected");
            ctl_chk(16'hA010, 3'b111, "T7 selected");
            wr(16'hD5FF, 8'h00);
            ctl_chk(16'hD500, 3'b111, "T7 deselected");
        end

        $display("=== POST-SYNTH RESULT (PBI=%0d, tMDS=%0dns): %0d checks, %0d functional errors, %0d invalid SRAM writes",
                 PBI, TMDS, checks, errors, sram_bad_wr);
        $finish;
    end
endmodule
