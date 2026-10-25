#!/usr/bin/env python3
"""
timing_model.py -- Timing budget of the ESP32-S3 bus loop (Vera-Module-RBL-XE)

All times in ns.  Unless stated otherwise they are measured from the PHI2
edge at the Atari PBI/ECI connector.  Instruction counts come from the
disassembly of MonitorTask (firmware.elf, env esp32s3fn8, arduino-esp32
2.0.14 / IDF 4.4.6, -O3):

  initial firmware   commit f311311 (decode after PHI2 rises, two APB reads)
  current firmware   commit 82c429f (decode during PHI1, assembly critical path)

The board is assumed to have the U19 ~OE gating (one 74LVC20, see
VERA-ATARI-HW-REQUIREMENTS.md section 2.1).

Writes:
  data/numbers.tex   \\newcommand macros used in the thesis text and tables
  data/slack_*.dat   series for pgfplots (slack vs APB latency)
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
LVC4245_EN = (1.0, 5.0, 9.0)            # output enable (~OE low -> outputs driven)
LVC4245_DIS = (1.0, 4.0, 8.0)           # output disable
LVC20 = (1.0, 2.5, 4.5)                 # 74LVC20 NAND4 @ 3.3 V, per gate
HCT04 = (4.0, 10.0, 24.0)               # SN74HCT04 @ 5 V
HC00_3V3 = (5.0, 12.0, 30.0)            # 74HC00 @ 3.3 V (interpolated)
FPGA_PAD = (6.0, 10.0, 15.0)            # iCE40UP5K pad -> logic -> pad

# ESP32-S3
SYNC = (0.0, 12.5, 25.0)                # GPIO input synchroniser, 0..2 APB clocks
PAD_OUT = (2.0, 4.0, 6.0)
POLL = 3                                # ee.get_gpio_in + taken bbci/bbsi

SCENARIOS = {"ott": 6, "nom": 12, "pes": 20}   # APB access latency, CPU cycles
SFX = {"ott": "Ott", "nom": "Nom", "pes": "Pes"}
CASES = {"min": 0, "typ": 1, "max": 2}

# --- initial firmware (f311311) -------------------------------------------
OLD_DECODE_FAST = 2 + 16 + 2            # 2 memw, 16 instr, 1 taken branch
OLD_DECODE_SLOW = OLD_DECODE_FAST + 10  # $D1xx / $D5xx: A0-A7 checks
OLD_DRIVE = 25 + 2                      # lookup + 2 memw
OLD_WR_LOOP = 6                         # write sampling loop, excluding the APB read

# --- current firmware (82c429f) -------------------------------------------
SETTLE_NS = 150.0                       # BUS_ADDR_SETTLE_NS
SETTLE_OVERSHOOT = (0, 2.5, 5)          # rsr.ccount / sub / bltz loop, cycles
WR_SAMPLE_NS = 120.0                    # BUS_WR_SAMPLE_NS
PHI2_HIGH_MAX_NS = 292.0                # BUS_PHI2_HIGH_MAX_NS (predicted fall)
FX_RAM = 15                             # PBI RAM / RAMbo store, cycles (done in the next PHI1)
FX_PORTB = 45                           # write side effect: PORTB (bank switch), cycles
NEW_DECODE = 10                         # dedicated read .. ee.wr_mask_gpio_out (+1 APB read)
NEW_DECODE_SLOW = NEW_DECODE + 7        # $D1xx / $D5xx through bus_slow[] (+1 APB read)
NEW_DRIVE = 14                          # .. second posted store (+2 APB writes)
NEW_KEEP_DRIVE = 13                     # option: outputs kept enabled (+1 APB write)
NEW_WR_DISABLE = 3                      # option: din .. GPIO.enable_w1tc issued


def half(f_mhz):
    return 1000.0 / f_mhz / 2.0


def phi2_high(f_mhz, case):
    return half(f_mhz) - (PHI2_ASYM if case == "max" else 0.0)


def deadline(f_mhz, case="max"):
    """Read data must be on the bus by this time after PHI2 rises."""
    return phi2_high(f_mhz, case) - T_DSU


def t_detect(case):
    """PHI2 edge at the connector -> ESP32 loop leaves its polling loop."""
    i = CASES[case]
    poll = (0, POLL / 2, POLL)[i]
    return LVC4245[i] + SYNC[i] + (poll + 1) * TC


# --- initial firmware --------------------------------------------------------
def old_ctrl(l_apb, slow=False, case="max"):
    i = CASES[case]
    cyc = (OLD_DECODE_SLOW if slow else OLD_DECODE_FAST) + 2 * l_apb
    return t_detect(case) + cyc * TC + PAD_OUT[i]


def old_data_esp(l_apb, case="max"):
    i = CASES[case]
    return old_ctrl(l_apb, case=case) + (OLD_DRIVE + 2 * l_apb) * TC + LVC4245[i]


def old_data_vera(l_apb, case="max"):
    i = CASES[case]
    return old_ctrl(l_apb, slow=True, case=case) + FPGA_PAD[i] + LVC4245[i]


# --- current firmware: times relative to the PHI2 rising edge ----------------
def new_sample(f_mhz, case="max"):
    """Address sample point (end of the settle wait)."""
    phi1 = half(f_mhz) + (PHI2_ASYM if case == "max" else 0.0)
    return t_detect(case) + SETTLE_NS + SETTLE_OVERSHOOT[CASES[case]] * TC - phi1


def new_ctrl(f_mhz, l_apb, slow=False, case="max"):
    """Control signal at the ESP32 pin."""
    i = CASES[case]
    cyc = (NEW_DECODE_SLOW if slow else NEW_DECODE) + l_apb
    return new_sample(f_mhz, case) + cyc * TC + PAD_OUT[i]


def u19_enabled(t_ctrl, case="max"):
    """U19 drives the Atari side: control signal -> 74LVC20 (2 gates) -> 4245 enable."""
    i = CASES[case]
    return t_ctrl + 2 * LVC20[i] + LVC4245_EN[i]


def new_data_esp(f_mhz, l_apb, case="max", keep_enabled=False):
    """Read data served by the ESP32 on the Atari bus."""
    i = CASES[case]
    t_c = new_ctrl(f_mhz, l_apb, case=case)
    drive = (NEW_KEEP_DRIVE + l_apb) if keep_enabled else (NEW_DRIVE + 2 * l_apb)
    t_pin = t_c + drive * TC
    return max(t_pin + LVC4245[i], u19_enabled(t_c, case))


def new_data_vera(f_mhz, l_apb, case="max"):
    """Read data from the VERA on the Atari bus (CS = DEV_SEL_N, RD = ~mRE)."""
    i = CASES[case]
    t_cs = new_ctrl(f_mhz, l_apb, slow=True, case=case)
    t_rd = LVC4245[i] + HC00_3V3[i]                 # ~mRE = NAND(mPHI2, mR/W)
    t_fpga = max(t_cs, t_rd) + FPGA_PAD[i]
    return max(t_fpga + LVC4245[i], u19_enabled(t_cs, case))


def new_ext_bus(f_mhz, l_apb, case="max"):
    """EXTSEL / MPD on the Atari bus (through U16)."""
    return new_ctrl(f_mhz, l_apb, case=case) + LVC4245[CASES[case]]


def new_write_sample(f_mhz, l_apb):
    """(margin after t_MDS, margin before the end of the hold), worst case."""
    early = t_detect("min") + WR_SAMPLE_NS                       # earliest sample
    late = t_detect("max") + WR_SAMPLE_NS + SETTLE_OVERSHOOT[2] * TC + l_apb * TC
    valid_from = T_MDS + LVC4245[2]
    valid_to = phi2_high(f_mhz, "max") + T_HW + LVC4245[0]
    return early - valid_from, valid_to - late


def new_wr_tail_cycles(f_mhz, l_apb):
    """CPU cycles between the write sample and PHI2 falling (worst case)."""
    late = t_detect("max") + WR_SAMPLE_NS + SETTLE_OVERSHOOT[2] * TC + l_apb * TC
    return (phi2_high(f_mhz, "max") + t_detect("min") - late) / TC


def after_write_delay(f_mhz, l_apb, fx_cycles):
    """Extra delay of the next address sample after a write whose side effects
    run past PHI2 falling: the settle time then starts from the predicted
    falling edge (rise + PHI2_HIGH_MAX_NS) instead of the detected one."""
    overrun = (fx_cycles - new_wr_tail_cycles(f_mhz, l_apb)) * TC
    if overrun <= 0:
        return 0.0
    return min(overrun, PHI2_HIGH_MAX_NS - phi2_high(f_mhz, "max"))


def keep_disable_slack(f_mhz, l_apb):
    """Option 'outputs kept enabled': in a write cycle the ESP32 must release
    D0-D7 before U19 starts driving the board side after PHI2 rises."""
    t_off = new_sample(f_mhz, "max") + (NEW_WR_DISABLE + l_apb) * TC + PAD_OUT[2]
    u19_on = LVC4245[0] + 2 * LVC20[0] + LVC4245_EN[0]          # earliest
    return u19_on - t_off


def zero_cross(fn):
    """Largest APB latency (cycles, 0.1 step) with fn(l) >= 0."""
    best = None
    l = 4.0
    while l <= 40.0:
        if fn(l) >= 0:
            best = l
        l = round(l + 0.1, 1)
    return best


def tex(x):
    """Integer for the text (math mode handles the minus sign)."""
    return f"{x:.0f}"


def tex1(x):
    return f"{x:.1f}".replace(".", "{,}")


def main():
    lines = []

    def macro(name, value):
        lines.append(f"\\newcommand{{\\{name}}}{{{value}}}")

    for tag, f in CLOCKS.items():
        macro(f"T{tag}", tex1(1000.0 / f))
        macro(f"H{tag}", tex1(half(f)))
        macro(f"Hn{tag}", tex(half(f)))
        macro(f"DL{tag}", tex(deadline(f)))
        macro(f"DLt{tag}", tex(deadline(f, "typ")))
    macro("CycAvail", tex1(half(CLOCKS["NTSC"]) / TC))
    macro("DetTyp", tex(t_detect("typ")))
    macro("DetMax", tex(t_detect("max")))

    # initial firmware
    macro("OldDecFast", str(OLD_DECODE_FAST))
    macro("OldDecSlow", str(OLD_DECODE_SLOW))
    macro("OldDrive", str(OLD_DRIVE))
    for s, l in SCENARIOS.items():
        x = SFX[s]
        macro(f"Lapb{x}", str(l))
        macro(f"LapbNs{x}", tex(l * TC))
        macro(f"OldCtrl{x}", tex(old_ctrl(l)))
        macro(f"OldCtrlT{x}", tex(old_ctrl(l, case="typ")))
        macro(f"OldDataEsp{x}", tex(old_data_esp(l)))
        macro(f"OldDataEspT{x}", tex(old_data_esp(l, case="typ")))
        for tag, f in CLOCKS.items():
            macro(f"OldSlEsp{x}{tag}", tex(deadline(f) - old_data_esp(l)))
            macro(f"OldSlEspT{x}{tag}", tex(deadline(f, "typ") - old_data_esp(l, case="typ")))

    # segments of the initial firmware read cycle, typical, optimistic APB
    l = SCENARIOS["ott"]
    t = t_detect("typ")
    macro("SgDet", tex(t))
    t += (2 + 2 * l) * TC
    macro("SgRd", tex(t))
    t += (OLD_DECODE_FAST - 2) * TC
    macro("SgDec", tex(t))
    macro("SgExt", tex(t + PAD_OUT[1] + LVC4245[1]))
    t += (OLD_DRIVE - 2) * TC
    macro("SgLook", tex(t))
    t += (2 + 2 * l) * TC
    macro("SgWr", tex(t))
    macro("SgBus", tex(t + PAD_OUT[1] + LVC4245[1]))

    # current firmware
    macro("NewDec", str(NEW_DECODE))
    macro("NewDecSlow", str(NEW_DECODE_SLOW))
    macro("NewDrive", str(NEW_DRIVE))
    macro("SettleNs", tex(SETTLE_NS))
    macro("WrSampleNs", tex(WR_SAMPLE_NS))
    macro("SettleCyc", tex(SETTLE_NS / TC))
    macro("PhiMaxNs", tex(PHI2_HIGH_MAX_NS))
    macro("FxRam", str(FX_RAM))
    macro("FxPortb", str(FX_PORTB))
    for s, l in SCENARIOS.items():
        x = SFX[s]
        for tag, f in CLOCKS.items():
            for case, c in (("max", ""), ("typ", "T")):
                macro(f"NwCtrl{c}{x}{tag}", tex(new_ctrl(f, l, case=case)))
                macro(f"NwExt{c}{x}{tag}", tex(new_ext_bus(f, l, case=case)))
                macro(f"NwData{c}{x}{tag}", tex(new_data_esp(f, l, case=case)))
                macro(f"NwSl{c}{x}{tag}", tex(deadline(f, case) - new_data_esp(f, l, case=case)))
                macro(f"NwVera{c}{x}{tag}", tex(new_data_vera(f, l, case=case)))
                macro(f"NwSlVera{c}{x}{tag}", tex(deadline(f, case) - new_data_vera(f, l, case=case)))
            a, b = new_write_sample(f, l)
            macro(f"NwWrA{x}{tag}", tex(a))
            macro(f"NwWrB{x}{tag}", tex(b))
            macro(f"NwTail{x}{tag}", tex(new_wr_tail_cycles(f, l)))
            macro(f"KpSl{x}{tag}", tex(deadline(f) - new_data_esp(f, l, keep_enabled=True)))
            macro(f"AwDly{x}{tag}", tex(after_write_delay(f, l, FX_PORTB)))
            macro(f"AwSl{x}{tag}", tex(deadline(f) - new_data_esp(f, l) - after_write_delay(f, l, FX_PORTB)))
            macro(f"KpDis{x}{tag}", tex(keep_disable_slack(f, l)))
    for tag, f in CLOCKS.items():
        macro(f"NwSample{tag}", tex(new_sample(f)))
        macro(f"NwSampleT{tag}", tex(new_sample(f, "typ")))
        macro(f"LimNew{tag}", tex1(zero_cross(lambda l: deadline(f) - new_data_esp(f, l))))
        macro(f"LimNewT{tag}", tex1(zero_cross(lambda l: deadline(f, "typ") -
                                                new_data_esp(f, l, case="typ"))))
        macro(f"LimKeep{tag}", tex1(zero_cross(lambda l: min(
            deadline(f) - new_data_esp(f, l, keep_enabled=True), keep_disable_slack(f, l)))))
        macro(f"LimKeepData{tag}", tex1(zero_cross(
            lambda l: deadline(f) - new_data_esp(f, l, keep_enabled=True))))
        macro(f"LimVera{tag}", tex1(zero_cross(lambda l: deadline(f) - new_data_vera(f, l))))
        macro(f"LimAw{tag}", tex1(zero_cross(lambda l: deadline(f) - new_data_esp(f, l) -
                                              after_write_delay(f, l, FX_PORTB))))
        macro(f"AwMax{tag}", tex(PHI2_HIGH_MAX_NS - phi2_high(f, "max")))

    # segments of the current firmware read cycle: worst case, nominal APB, NTSC
    f = CLOCKS["NTSC"]
    l = SCENARIOS["nom"]
    phi1 = half(f) + PHI2_ASYM
    macro("PgFall", tex(-phi1))
    macro("PgAddr", tex(T_ADS - phi1))
    macro("PgDet", tex(t_detect("max") - phi1))
    s0 = new_sample(f)
    macro("PgSample", tex(s0))
    macro("PgRd", tex(s0 + (1 + l) * TC))
    t_c = new_ctrl(f, l)
    macro("PgCtrl", tex(t_c))
    macro("PgExt", tex(new_ext_bus(f, l)))
    macro("PgOe", tex(u19_enabled(t_c)))
    macro("PgDrvIssue", tex(t_c + NEW_DRIVE * TC))
    macro("PgData", tex(new_data_esp(f, l)))
    macro("PgSlack", tex(deadline(f) - new_data_esp(f, l)))
    macro("PgFallNext", tex(phi2_high(f, "max")))

    with open(os.path.join(OUT, "numbers.tex"), "w") as fp:
        fp.write("% generated by timing_model.py -- do not edit\n")
        fp.write("\n".join(lines) + "\n")

    for tag, f in CLOCKS.items():
        with open(os.path.join(OUT, f"slack_{tag.lower()}.dat"), "w") as fp:
            fp.write("lapb old oldt new newt vera wr keep keepdis aw\n")
            for l in range(4, 31):
                fp.write(f"{l} "
                         f"{deadline(f) - old_data_esp(l):.1f} "
                         f"{deadline(f, 'typ') - old_data_esp(l, case='typ'):.1f} "
                         f"{deadline(f) - new_data_esp(f, l):.1f} "
                         f"{deadline(f, 'typ') - new_data_esp(f, l, case='typ'):.1f} "
                         f"{deadline(f) - new_data_vera(f, l):.1f} "
                         f"{min(new_write_sample(f, l)):.1f} "
                         f"{deadline(f) - new_data_esp(f, l, keep_enabled=True):.1f} "
                         f"{keep_disable_slack(f, l):.1f} "
                         f"{deadline(f) - new_data_esp(f, l) - after_write_delay(f, l, FX_PORTB):.1f}\n")

    f = CLOCKS["NTSC"]
    for l in (6, 9, 12, 14, 15, 20):
        print(f"L={l:2d} new data {new_data_esp(f, l):6.1f} slack {deadline(f) - new_data_esp(f, l):6.1f} "
              f"vera slack {deadline(f) - new_data_vera(f, l):6.1f} "
              f"wr {new_write_sample(f, l)} tail {new_wr_tail_cycles(f, l):5.1f} "
              f"keep {deadline(f) - new_data_esp(f, l, keep_enabled=True):6.1f}/{keep_disable_slack(f, l):6.1f} "
              f"ext {new_ext_bus(f, l):6.1f} oe {u19_enabled(new_ctrl(f, l)):6.1f}")
    for tag, f in CLOCKS.items():
        print(tag, "lim new", zero_cross(lambda l: deadline(f) - new_data_esp(f, l)),
              "typ", zero_cross(lambda l: deadline(f, "typ") - new_data_esp(f, l, case="typ")),
              "keep", zero_cross(lambda l: min(deadline(f) - new_data_esp(f, l, keep_enabled=True),
                                               keep_disable_slack(f, l))),
              "vera", zero_cross(lambda l: deadline(f) - new_data_vera(f, l)),
              "after write", zero_cross(lambda l: deadline(f) - new_data_esp(f, l) -
                                        after_write_delay(f, l, FX_PORTB)))


if __name__ == "__main__":
    main()
