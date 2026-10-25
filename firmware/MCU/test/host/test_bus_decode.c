/**
 * test_bus_decode.c  --  host-side equivalence test for include/bus_decode.h
 *
 * Builds the GPIO.in and dedicated GPIO input snapshots exactly as the
 * ESP32-S3 pins would deliver them, runs the table-driven decode used by
 * MonitorTask, and compares it with reference range-compare logic, for every
 * address x selected x RAMbo x self-test state x PBI/CCTL mode (the RAMbo
 * 320K emulation is always present).
 *
 *   cc -O2 -Wall -I../../include test_bus_decode.c -o test_bus_decode && ./test_bus_decode
 *
 * The VERA registers ($D100-$D11F) answer only when the card is selected,
 * as the Atari PBI rules require.  With the self-test ROM visible (PORTB
 * bit 0 = 1, bit 7 = 0) the RAMbo window leaves $5000-$57FF to the ROM.
 */

#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include "bus_decode.h"

static uint8_t rom[2048], ram[512], rambo[256 * 1024];

/* GPIO.in: A0-A9 on bits 12-21.  Dedicated input: PHI2, R/W, A10-A15. */
static void pins_from_addr(uint16_t a, uint32_t *lo, uint32_t *din)
{
    *lo = (uint32_t)(a & 0x3FFu) << 12;
    *din = (uint32_t)((a >> 10) & 0x3Fu) << 2;
    /* unrelated bits set, to catch masking mistakes */
    *lo |= 0xFFC00000u | 0x00000FF6u;
    *din |= DEDIC_IN_PHI2 | DEDIC_IN_RW;
}

/* Reference: control levels. */
static uint32_t ref_ctrl(uint16_t addr, bool is_pbi, bool selected, bool rambo_active,
                         bool selftest)
{
    uint8_t off8 = addr & 0xFFu;
    bool is_d1xx = addr >= 0xD100u && addr <= 0xD1FFu;
    bool is_d6xx = addr >= 0xD600u && addr <= 0xD7FFu;
    bool is_d8xx = addr >= 0xD800u && addr <= 0xDFFFu;
    bool is_vera_range = is_d1xx && off8 <= 0x1Fu;
    bool is_vcs_latch  = is_d1xx && off8 == 0xFFu;
    bool is_d5xx = addr >= 0xD500u && addr <= 0xD5FFu;
    bool is_cctl_range = is_d5xx && off8 != 0xFFu;
    bool is_rambo_window = addr >= 0x4000u && addr <= 0x7FFFu &&
                           !(selftest && addr >= 0x5000u && addr <= 0x57FFu);

    uint32_t ctrl = DEDIC_OUT_CTRL_IDLE;
    if (is_pbi)
    {
        if (selected)
        {
            if (is_vera_range)
                ctrl &= ~DEDIC_OUT_DEVSELN;
            if ((is_d1xx && !is_vcs_latch) || is_d6xx)
                ctrl &= ~DEDIC_OUT_EXTSEL;
            if (is_d8xx)
                ctrl &= ~DEDIC_OUT_MPD;
        }
    }
    else if (selected && is_cctl_range)
    {
        ctrl &= ~DEDIC_OUT_DEVSELN;
    }
    if (rambo_active && is_rambo_window)
        ctrl &= ~DEDIC_OUT_EXTSEL;
    return ctrl;
}

static bool in_rambo(uint16_t addr, bool rambo_active, bool selftest)
{
    return rambo_active && addr >= 0x4000u && addr <= 0x7FFFu &&
           !(selftest && addr >= 0x5000u && addr <= 0x57FFu);
}

/* Reference: byte driven on a read (NULL = bus not driven). */
static const uint8_t *ref_drive(uint16_t addr, bool is_pbi, bool selected,
                                bool rambo_active, bool selftest, uint32_t bank)
{
    const uint8_t *p = NULL;
    if (is_pbi && selected)
    {
        if (addr >= 0xD800u && addr <= 0xDFFFu)
            p = &rom[addr & 0x7FFu];
        else if (addr >= 0xD600u && addr <= 0xD7FFu)
            p = &ram[addr & 0x1FFu];
    }
    if (in_rambo(addr, rambo_active, selftest))
        p = &rambo[(bank << 14) | (addr & 0x3FFFu)];
    return p;
}

/* Reference: write target. */
static uint8_t *ref_wr_mem(uint16_t addr, bool is_pbi, bool selected,
                           bool rambo_active, bool selftest, uint32_t bank)
{
    if (is_pbi && selected && addr >= 0xD600u && addr <= 0xD7FFu)
        return &ram[addr & 0x1FFu];
    if (in_rambo(addr, rambo_active, selftest))
        return &rambo[(bank << 14) | (addr & 0x3FFFu)];
    return NULL;
}

/* Reference: addresses whose writes have register side effects. */
static bool ref_wr_reg(uint16_t addr, bool is_pbi)
{
    return addr == (is_pbi ? 0xD1FFu : 0xD5FFu) || addr == 0xD301u || addr == 0xD303u;
}

static unsigned long checks, errors;

static void expect(bool ok, const char *what, unsigned v)
{
    checks++;
    if (!ok && ++errors <= 20)
        printf("FAIL %s (%u)\n", what, v);
}

int main(void)
{
    static uint8_t        ctrl[BUS_STATES][256];
    static const uint8_t *rd_base[256];

    /* page from the two snapshots */
    for (uint32_t a = 0; a < 0x10000u; a += 0x100u)
    {
        uint32_t lo, din;
        pins_from_addr((uint16_t)a, &lo, &din);
        expect(bus_page(lo, din) == (a >> 8), "page", a);
    }

    /* PORTB helpers */
    expect(bus_rambo_bank(0x83) == 0 && bus_rambo_bank(0xEF) == 15 &&
           bus_rambo_bank(0xA7) == 5 && bus_rambo_bank(0xCB) == 10, "bank", 0);
    expect(bus_portb_pins(0x00, 0x00) == 0xFF, "pins after PIA reset", 0);
    expect(bus_portb_pins(0x83, 0xFF) == 0x83, "pins all outputs", 0);
    expect(bus_portb_pins(0x00, 0xEF) == 0x10, "pins bit 4 input", 0);
    expect(bus_selftest_visible(0x7F) && !bus_selftest_visible(0xFF) &&
           !bus_selftest_visible(0x7E), "self-test", 0);

    for (int pbi = 0; pbi <= 1; pbi++)
    {
        static uint8_t slow[2][256];
        bus_build_tables(ctrl, rd_base, pbi, rom, ram, rambo);
        bus_build_slow_tables(slow, ctrl, pbi);
        for (int sel = 0; sel <= 1; sel++)
        for (int portb_bit4 = 0; portb_bit4 <= 1; portb_bit4++)
        for (int st = 0; st <= 1; st++)
        for (uint32_t bank = 0; bank < 16u; bank += 5u)
        {
            bool rambo_active = !portb_bit4;
            const uint8_t *tab = ctrl[bus_state(sel, rambo_active, st)];
            uint32_t rambo_off = bank << 14;

            for (uint32_t a = 0; a < 0x10000u; a++)
            {
                uint32_t lo, din;
                pins_from_addr((uint16_t)a, &lo, &din);
                uint32_t page = bus_page(lo, din);
                uint32_t c    = tab[page];
                uint32_t off8 = (lo >> 12) & 0xFFu;
                if (c & CT_SLOW)
                {
                    uint32_t ref = bus_slow_ctrl(c, off8, sel, pbi);
                    c = slow[sel][off8];          /* as in the bus loop */
                    if (c != ref && ++errors <= 20)
                        printf("FAIL slow table pbi=%d sel=%d $%04X\n", pbi, sel, a);
                    checks++;
                }

                const uint8_t *drv = NULL;
                if (c & CT_DRIVE)
                    drv = rd_base[page] + ((c & CT_RAMBO) ? rambo_off : 0u) + off8;
                uint8_t *wr = NULL;
                if (c & CT_WR_MEM)
                    wr = (uint8_t *)rd_base[page] + ((c & CT_RAMBO) ? rambo_off : 0u) + off8;

                uint32_t       e_ctrl = ref_ctrl((uint16_t)a, pbi, sel, rambo_active, st);
                const uint8_t *e_drv  = ref_drive((uint16_t)a, pbi, sel, rambo_active, st, bank);
                uint8_t       *e_wr   = ref_wr_mem((uint16_t)a, pbi, sel, rambo_active, st, bank);
                bool           e_reg  = ref_wr_reg((uint16_t)a, pbi);

                checks += 4;
                if ((c & DEDIC_OUT_CTRL_MASK) != e_ctrl || drv != e_drv || wr != e_wr ||
                    (e_reg && !(c & CT_WR_REG)))
                {
                    if (++errors <= 20)
                        printf("FAIL pbi=%d sel=%d rame=%d st=%d bank=%u $%04X: "
                               "ctrl %X/%X drive %s wr %s reg %d/%d\n",
                               pbi, sel, portb_bit4, st, bank, a,
                               c & 7u, e_ctrl,
                               drv == e_drv ? "ok" : "DIFF", wr == e_wr ? "ok" : "DIFF",
                               (c & CT_WR_REG) != 0, e_reg);
                }
            }
        }
    }

    printf("=== %lu checks, %lu errors\n", checks, errors);
    return errors != 0;
}
