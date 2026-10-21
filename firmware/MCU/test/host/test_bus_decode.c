/**
 * test_bus_decode.c  --  host-side equivalence test for include/bus_decode.h
 *
 * Builds GPIO.in / GPIO.in1 snapshots exactly as the ESP32-S3 pins would
 * deliver them, runs the table-driven decode used by MonitorTask, and
 * compares it with the reference range-compare logic of the previous
 * firmware, for every address x selected x RAMbo state x PBI/CCTL mode.
 *
 *   cc -O2 -Wall -I../../include test_bus_decode.c -o test_bus_decode && ./test_bus_decode
 *
 * Expected difference from the old firmware: PBI RAM writes ($D600-$D7FF,
 * card selected) are now stored (CT_WR_MEM); the old loop never wrote ram_pbi.
 */

#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include "bus_decode.h"

/* Pin mapping (src/main.cpp) */
#define PIN_A12 33
#define PIN_A13 34
#define PIN_A10 35
#define PIN_A11 36
#define PIN_A14 45
#define PIN_A15 46

static uint8_t rom[2048], ram[512], rambo[256 * 1024];

static void pins_from_addr(uint16_t a, uint32_t *lo, uint32_t *hi)
{
    *lo = (uint32_t)(a & 0x3FFu) << 12;
    *hi = (((a >> 10) & 1u) << (PIN_A10 - 32)) |
          (((a >> 11) & 1u) << (PIN_A11 - 32)) |
          (((a >> 12) & 1u) << (PIN_A12 - 32)) |
          (((a >> 13) & 1u) << (PIN_A13 - 32)) |
          (((a >> 14) & 1u) << (PIN_A14 - 32)) |
          (((a >> 15) & 1u) << (PIN_A15 - 32));
    /* unrelated bits set, to catch masking mistakes */
    *lo |= 0xFFC00000u | 0x00000FF6u;
    *hi |= ~((1u << 1) | (1u << 2) | (1u << 3) | (1u << 4) | (1u << 13) | (1u << 14)) & 0x0001FFFFu;
}

/* Reference: control levels of the previous MonitorTask. */
static uint32_t ref_ctrl(uint16_t addr, bool is_pbi, bool selected, bool rambo_active)
{
    uint8_t off8 = addr & 0xFFu;
    bool is_d1xx = addr >= 0xD100u && addr <= 0xD1FFu;
    bool is_d6xx = addr >= 0xD600u && addr <= 0xD7FFu;
    bool is_d8xx = addr >= 0xD800u && addr <= 0xDFFFu;
    bool is_vera_range = is_d1xx && off8 <= 0x1Fu;
    bool is_vcs_latch  = is_d1xx && off8 == 0xFFu;
    bool is_d5xx = addr >= 0xD500u && addr <= 0xD5FFu;
    bool is_cctl_range = is_d5xx && off8 != 0xFFu;
    bool is_rambo_window = addr >= 0x4000u && addr <= 0x7FFFu;

    uint32_t ctrl = DEDIC_OUT_CTRL_IDLE;
    if (is_pbi)
    {
        if (is_vera_range)
            ctrl &= ~(DEDIC_OUT_DEVSELN | DEDIC_OUT_EXTSEL);
        if (selected)
        {
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

/* Reference: byte driven on a read (NULL = bus not driven). */
static const uint8_t *ref_drive(uint16_t addr, bool is_pbi, bool selected,
                                bool rambo_active, uint32_t bank)
{
    const uint8_t *p = NULL;
    if (is_pbi && selected)
    {
        if (addr >= 0xD800u && addr <= 0xDFFFu)
            p = &rom[addr & 0x7FFu];
        else if (addr >= 0xD600u && addr <= 0xD7FFu)
            p = &ram[addr & 0x1FFu];
    }
    if (rambo_active && addr >= 0x4000u && addr <= 0x7FFFu)
        p = &rambo[(bank << 14) | (addr & 0x3FFFu)];
    return p;
}

/* Reference: write target (PBI RAM included, see header comment). */
static uint8_t *ref_wr_mem(uint16_t addr, bool is_pbi, bool selected,
                           bool rambo_active, uint32_t bank)
{
    if (is_pbi && selected && addr >= 0xD600u && addr <= 0xD7FFu)
        return &ram[addr & 0x1FFu];
    if (rambo_active && addr >= 0x4000u && addr <= 0x7FFFu)
        return &rambo[(bank << 14) | (addr & 0x3FFFu)];
    return NULL;
}

/* Reference: addresses whose writes have register side effects. */
static bool ref_wr_reg(uint16_t addr, bool is_pbi)
{
    return addr == (is_pbi ? 0xD1FFu : 0xD5FFu) || addr == 0xD301u || addr == 0xD303u;
}

int main(void)
{
    static uint8_t        ctrl[BUS_STATES][256];
    static const uint8_t *rd_base[256];
    unsigned long checks = 0, errors = 0;

    /* page index <-> page round trip */
    for (uint32_t a = 0; a < 0x10000u; a += 0x100u)
    {
        uint32_t lo, hi;
        pins_from_addr((uint16_t)a, &lo, &hi);
        uint32_t idx = bus_page_idx(lo, hi);
        checks++;
        if (bus_idx_to_page(idx) != (a >> 8))
        {
            errors++;
            printf("FAIL page index: $%04X -> idx %02X -> page %02X\n", a, idx, bus_idx_to_page(idx));
        }
    }

    for (int pbi = 0; pbi <= 1; pbi++)
    for (int hw = 0; hw <= 1; hw++)
    {
        bus_build_tables(ctrl, rd_base, pbi, hw, rom, ram, rambo);
        for (int sel = 0; sel <= 1; sel++)
        for (int portb_bit4 = 0; portb_bit4 <= 1; portb_bit4++)
        for (uint32_t bank = 0; bank < 16u; bank += 5u)
        {
            bool rambo_active = hw && !portb_bit4;
            const uint8_t *tab = ctrl[bus_state(sel, rambo_active)];
            uint32_t rambo_off = bank << 14;

            for (uint32_t a = 0; a < 0x10000u; a++)
            {
                uint32_t lo, hi;
                pins_from_addr((uint16_t)a, &lo, &hi);
                uint32_t idx  = bus_page_idx(lo, hi);
                uint32_t c    = tab[idx];
                uint32_t off8 = (lo >> 12) & 0xFFu;
                if (c & CT_SLOW)
                    c = bus_slow_ctrl(c, off8, sel, pbi);

                const uint8_t *drv = NULL;
                if (c & CT_DRIVE)
                    drv = rd_base[idx] + ((c & CT_RAMBO) ? rambo_off : 0u) + off8;
                uint8_t *wr = NULL;
                if (c & CT_WR_MEM)
                    wr = (uint8_t *)rd_base[idx] + ((c & CT_RAMBO) ? rambo_off : 0u) + off8;

                uint32_t       e_ctrl = ref_ctrl((uint16_t)a, pbi, sel, rambo_active);
                const uint8_t *e_drv  = ref_drive((uint16_t)a, pbi, sel, rambo_active, bank);
                uint8_t       *e_wr   = ref_wr_mem((uint16_t)a, pbi, sel, rambo_active, bank);
                bool           e_reg  = ref_wr_reg((uint16_t)a, pbi);

                checks += 4;
                if ((c & DEDIC_OUT_CTRL_MASK) != e_ctrl || drv != e_drv || wr != e_wr ||
                    (e_reg && !(c & CT_WR_REG)))
                {
                    if (++errors <= 20)
                        printf("FAIL pbi=%d hw=%d sel=%d rame=%d bank=%u $%04X: "
                               "ctrl %X/%X drive %s wr %s reg %d/%d\n",
                               pbi, hw, sel, portb_bit4, bank, a,
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
