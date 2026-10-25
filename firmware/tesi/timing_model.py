#!/usr/bin/env python3
"""
timing_model.py -- Timing budget of the ESP32-S3 bus loop (Vera-Module-RBL-XE)

All times in ns, measured from the PHI2 edge at the Atari PBI/ECI connector.
Instruction counts come from the disassembly of MonitorTask (firmware.elf,
env esp32s3fn8, arduino-esp32 2.0.14 / IDF 4.4.6, -O3).

Writes:
  data/slack_*.dat   series for pgfplots (slack vs APB latency)
  data/numbers.tex   \newcommand macros used in the thesis text and tables
"""

import os

OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "data")
os.makedirs(OUT, exist_ok=True)

TC = 1000.0 / 240.0                     # ESP32-S3 CPU cycle @ 240 MHz

CLOCKS = {
    "NTSC": 1.789773,                   # MHz, Altirra HRM 2.2
    "PAL":  1.773447,
}

# 6502, 2 MHz grade (Rockwell R6502A data sheet)
T_ADS, T_MDS, T_DSU, T_HW = 140.0, 100.0, 50.0, 30.0
PHI2_ASYM = 10.0                        # assumed PHI2-high shortening (duty error)

# Glue logic, (min, typ, max)
LVC4245 = (1.0, 4.0, 6.5)               # 74LVC4245A propagation 5 V <-> 3.3 V
LVC4245_DIS = (1.0, 4.0, 8.0)           # output disable
HCT04 = (4.0, 10.0, 24.0)               # SN74HCT04 @ 5 V
HC00_3V3 = (5.0, 12.0, 30.0)            # 74HC00 @ 3.3 V (interpolated)
FPGA_PAD = (6.0, 10.0, 15.0)            # iCE40UP5K pad -> logic -> pad

# ESP32-S3
SYNC = (0.0, 12.5, 25.0)                # GPIO input synchroniser, 0..2 APB clocks
PAD_OUT = (2.0, 4.0, 6.0)
POLL = 3                                # ee.get_gpio_in + taken bbci

# Instruction cycles after the PHI2 poll exits (from the disassembly)
CYC_DECODE_FAST = 2 + 16 + 2            # 2 memw, 16 instr, 1 taken branch
CYC_DECODE_SLOW = CYC_DECODE_FAST + 10  # $D1xx / $D5xx: A0-A7 checks
CYC_DRIVE = 25 + 2                      # ROM/RAM/RAMbo lookup + 2 memw
CYC_WR_LOOP = 6                         # write sampling loop, excluding the APB read

SCENARIOS = {"ott": 6, "nom": 12, "pes": 20}   # APB access latency, CPU cycles


def half(f_mhz):
    return 1000.0 / f_mhz / 2.0


def t_detect(case):
    i = {"min": 0, "typ": 1, "max": 2}[case]
    poll = {"min": 0, "typ": POLL / 2, "max": POLL}[case]
    return LVC4245[i] + SYNC[i] + (poll + 1) * TC


def t_ctrl(l_apb, slow=False, case="max"):
    """Control signal (DEV_SEL_N) at the ESP32 pin."""
    i = {"min": 0, "typ": 1, "max": 2}[case]
    cyc = (CYC_DECODE_SLOW if slow else CYC_DECODE_FAST) + 2 * l_apb
    return t_detect(case) + cyc * TC + PAD_OUT[i]


def t_data_esp(l_apb, case="max"):
    """Read data served by the ESP32, on the Atari bus."""
    i = {"min": 0, "typ": 1, "max": 2}[case]
    return t_ctrl(l_apb, case=case) + (CYC_DRIVE + 2 * l_apb) * TC + LVC4245[i]


def t_data_vera(l_apb, case="max"):
    """Read data served by the VERA (DEV_SEL_N -> FPGA -> U19), on the Atari bus."""
    i = {"min": 0, "typ": 1, "max": 2}[case]
    return t_ctrl(l_apb, slow=True, case=case) + FPGA_PAD[i] + LVC4245[i]


def deadline(f_mhz, case="max"):
    asym = PHI2_ASYM if case == "max" else 0.0
    return half(f_mhz) - asym - T_DSU


def write_sample_slack(f_mhz, l_apb):
    """Last PHI2-high sample must come after the 6502 write data is valid."""
    period = (l_apb + CYC_WR_LOOP) * TC
    last = half(f_mhz) - PHI2_ASYM - period - SYNC[2]
    valid = T_MDS + LVC4245[2]
    return last - valid


def vera_write_hold():
    """WR_n rise at the FPGA vs end of valid data at the FPGA (after PHI2 fall)."""
    wr_rise = {"min": LVC4245[0] + HC00_3V3[0], "typ": LVC4245[1] + HC00_3V3[1],
               "max": LVC4245[2] + HC00_3V3[2]}
    data_end = {"min": min(T_HW + LVC4245[0], HCT04[0] + LVC4245_DIS[0]),
                "typ": min(T_HW + LVC4245[1], HCT04[1] + LVC4245_DIS[1]),
                "max": min(T_HW + LVC4245[2], HCT04[2] + LVC4245_DIS[2])}
    worst = data_end["min"] - wr_rise["max"]
    typ = data_end["typ"] - wr_rise["typ"]
    return worst, typ


# --- new loop (firmware with PHI1 decode), counts from its disassembly ------
# Settle is counted from the detection of PHI2 falling; then one dedicated
# read + one APB read, decode (18 cycles), ctrl write; data drive 22 cycles +
# two APB writes.
SETTLE_NS = 150.0
CYC_P_DECODE = 18
CYC_P_DRIVE = 22


def proposed(f_mhz, l_apb, case="max"):
    """Returns (ctrl, data) times relative to the PHI2 rise."""
    i = {"min": 0, "typ": 1, "max": 2}[case]
    asym = PHI2_ASYM if case == "max" else 0.0
    phi1 = half(f_mhz) + asym           # PHI1 is longer if PHI2 is shorter
    t_s = t_detect(case) + SETTLE_NS    # address sampled, from PHI2 falling
    t_ctrl_p = t_s + (CYC_P_DECODE + l_apb) * TC + PAD_OUT[i]
    t_data_p = t_ctrl_p + (CYC_P_DRIVE + 2 * l_apb) * TC + LVC4245[i]
    return t_ctrl_p - phi1, t_data_p - phi1


def tex_num(x):
    return f"{x:.0f}"


def main():
    lines = []

    def macro(name, value):
        lines.append(f"\\newcommand{{\\{name}}}{{{value}}}")

    for tag, f in CLOCKS.items():
        macro(f"T{tag}", f"{1000.0 / f:.1f}".replace(".", "{,}"))
        macro(f"H{tag}", f"{half(f):.1f}".replace(".", "{,}"))
        macro(f"Hn{tag}", tex_num(half(f)))
        macro(f"DL{tag}", f"{deadline(f):.0f}")

    for s, l in SCENARIOS.items():
        sfx = {"ott": "Ott", "nom": "Nom", "pes": "Pes"}[s]
        macro(f"Lapb{sfx}", str(l))
        macro(f"LapbNs{sfx}", tex_num(l * TC))
        macro(f"Det{sfx}", tex_num(t_detect("max")))
        macro(f"Ctrl{sfx}", tex_num(t_ctrl(l)))
        macro(f"CtrlSlow{sfx}", tex_num(t_ctrl(l, slow=True)))
        macro(f"DataEsp{sfx}", tex_num(t_data_esp(l)))
        macro(f"DataVera{sfx}", tex_num(t_data_vera(l)))
        macro(f"CtrlT{sfx}", tex_num(t_ctrl(l, case="typ")))
        macro(f"DataEspT{sfx}", tex_num(t_data_esp(l, case="typ")))
        macro(f"DataVeraT{sfx}", tex_num(t_data_vera(l, case="typ")))
        for tag, f in CLOCKS.items():
            macro(f"SlEsp{sfx}{tag}", tex_num(deadline(f) - t_data_esp(l)))
            macro(f"SlVera{sfx}{tag}", tex_num(deadline(f) - t_data_vera(l)))
            macro(f"SlWr{sfx}{tag}", tex_num(write_sample_slack(f, l)))
            macro(f"SlEspT{sfx}{tag}",
                  tex_num(deadline(f, "typ") - t_data_esp(l, case="typ")))
            macro(f"SlVeraT{sfx}{tag}",
                  tex_num(deadline(f, "typ") - t_data_vera(l, case="typ")))
            pc, pd = proposed(f, l)
            macro(f"PrCtrl{sfx}{tag}", tex_num(pc))
            macro(f"PrData{sfx}{tag}", tex_num(pd))
            macro(f"SlPr{sfx}{tag}", tex_num(deadline(f) - pd))
    w, t = vera_write_hold()
    macro("HoldWorst", tex_num(w))
    macro("HoldTyp", tex_num(t))
    macro("DetTyp", tex_num(t_detect("typ")))
    macro("CtrlTypNom", tex_num(t_ctrl(SCENARIOS["nom"], case="typ")))
    macro("DataEspTypOtt", tex_num(t_data_esp(SCENARIOS["ott"], case="typ")))
    macro("CycAvail", tex_num(half(CLOCKS["NTSC"]) / TC))
    macro("CycDecFast", str(CYC_DECODE_FAST))
    macro("CycDecSlow", str(CYC_DECODE_SLOW))
    macro("CycDrive", str(CYC_DRIVE))
    macro("DLtNTSC", tex_num(deadline(CLOCKS["NTSC"], "typ")))
    macro("DLtPAL", tex_num(deadline(CLOCKS["PAL"], "typ")))
    macro("TsOtt", tex_num((SCENARIOS["ott"] + CYC_WR_LOOP) * TC))
    macro("TsPes", tex_num((SCENARIOS["pes"] + CYC_WR_LOOP) * TC))

    # Segment boundaries of the read cycle served by the ESP32, typical case,
    # optimistic APB latency (used by the timing diagrams).
    l = SCENARIOS["ott"]
    t = t_detect("typ")
    macro("SgDet", tex_num(t))
    t += (2 + 2 * l) * TC
    macro("SgRd", tex_num(t))
    t += (CYC_DECODE_FAST - 2) * TC
    macro("SgDec", tex_num(t))
    macro("SgCtrl", tex_num(t + PAD_OUT[1]))
    macro("SgExt", tex_num(t + PAD_OUT[1] + LVC4245[1]))
    t += (CYC_DRIVE - 2) * TC
    macro("SgLook", tex_num(t))
    t += (2 + 2 * l) * TC
    macro("SgWr", tex_num(t))
    macro("SgBus", tex_num(t + PAD_OUT[1] + LVC4245[1]))
    macro("SgVera", tex_num(t_data_vera(l, case="typ")))
    macro("SgCtrlSlow", tex_num(t_ctrl(l, slow=True, case="typ")))

    # Proposed loop, nominal APB latency, worst case (relative to PHI2 rise)
    f = CLOCKS["NTSC"]
    l = SCENARIOS["nom"]
    phi1 = half(f) + PHI2_ASYM
    addr_ok = t_detect("max") + SETTLE_NS
    macro("PgAddr", tex_num(T_ADS - phi1))
    macro("PgAddrOk", tex_num(addr_ok - phi1))
    macro("PgRd", tex_num(addr_ok + (2 + l) * TC - phi1))

    with open(os.path.join(OUT, "numbers.tex"), "w") as fp:
        fp.write("% generated by timing_model.py -- do not edit\n")
        fp.write("\n".join(lines) + "\n")

    # slack vs APB latency, 4..30 CPU cycles
    for tag, f in CLOCKS.items():
        with open(os.path.join(OUT, f"slack_{tag.lower()}.dat"), "w") as fp:
            fp.write("lapb esp vera ctrl wr prop espt verat\n")
            for l in range(4, 31):
                pc, pd = proposed(f, l)
                fp.write(f"{l} {deadline(f) - t_data_esp(l):.1f} "
                         f"{deadline(f) - t_data_vera(l):.1f} "
                         f"{deadline(f) - t_ctrl(l):.1f} "
                         f"{write_sample_slack(f, l):.1f} "
                         f"{deadline(f) - pd:.1f} "
                         f"{deadline(f, 'typ') - t_data_esp(l, case='typ'):.1f} "
                         f"{deadline(f, 'typ') - t_data_vera(l, case='typ'):.1f}\n")

    for s, l in SCENARIOS.items():
        print(f"L={l:2d}  ctrl={t_ctrl(l):6.1f}  dataESP={t_data_esp(l):6.1f}  "
              f"dataVERA={t_data_vera(l):6.1f}  deadlineNTSC={deadline(1.789773):6.1f}  "
              f"wrslack={write_sample_slack(1.789773, l):6.1f}  prop={proposed(1.789773, l)}")
    print("vera write hold (worst, typ):", vera_write_hold())


if __name__ == "__main__":
    main()
