/**
 * bus_decode.h  --  Table-driven 6502 bus decode for MonitorTask
 *
 * The hot loop must assert EXTSEL_N / MPD / DEV_SEL_N and drive read data
 * well inside the PHI2-high window (~279 ns at 1.79 MHz, ~67 CPU cycles at
 * 240 MHz).  Comparing address ranges at run time costs ~95 instructions, so
 * everything that depends only on the page (A8-A15) and on the card state
 * (latch selected, RAMbo active) is precomputed here into 256-entry tables.
 *
 * Page index: A8-A15 taken straight from the two GPIO snapshots, in pin order
 * (no bit reordering in the hot loop):
 *   idx bit 0,1 = A8, A9    (GPIO.in   bits 20,21)
 *   idx bit 2,3 = A12, A13  (GPIO.in1  bits 1,2  -> GPIO33,34)
 *   idx bit 4,5 = A10, A11  (GPIO.in1  bits 3,4  -> GPIO35,36)
 *   idx bit 6,7 = A14, A15  (GPIO.in1  bits 13,14 -> GPIO45,46)
 *
 * Table entry (uint8_t):
 *   bits 0-2  dedicated GPIO output levels: DEV_SEL_N, EXTSEL_N, MPD
 *   CT_SLOW   ctrl also depends on A0-A7 ($D1xx in PBI, $D5xx in CCTL)
 *   CT_DRIVE  read cycle: drive rd_base[idx][A0-A7] on the data bus
 *   CT_RAMBO  add the current RAMbo bank offset to rd_base[idx]
 *   CT_WR_MEM write cycle: store data into rd_base[idx][A0-A7]
 *   CT_WR_REG write cycle: latch / PIA snoop on this page
 *
 * No ESP-IDF dependencies: also compiled by the host test (test/).
 * Style: Allman
 */

#ifndef BUS_DECODE_H
#define BUS_DECODE_H

#include <stdint.h>
#include <stdbool.h>

#define DEDIC_OUT_DEVSELN    (1u << 0)
#define DEDIC_OUT_EXTSEL     (1u << 1)
#define DEDIC_OUT_MPD        (1u << 2)
#define DEDIC_OUT_CTRL_MASK  0x07u
#define DEDIC_OUT_CTRL_IDLE  0x07u   /* all HIGH = deasserted (active LOW) */

#define CT_SLOW    (1u << 3)
#define CT_DRIVE   (1u << 4)
#define CT_RAMBO   (1u << 5)
#define CT_WR_MEM  (1u << 6)
#define CT_WR_REG  (1u << 7)

/* Table set: [ (selected << 1) | rambo_active ][ page index ] */
#define BUS_STATES 4u

/* Page index from the GPIO.in / GPIO.in1 snapshots (6 instructions). */
static inline uint32_t bus_page_idx(uint32_t lo, uint32_t hi)
{
    return ((lo >> 20) & 0x03u) | ((hi << 1) & 0x3Cu) | ((hi >> 7) & 0xC0u);
}

/* Page number (A15..A8) for a page index. */
static inline uint8_t bus_idx_to_page(uint32_t idx)
{
    return (uint8_t)((idx & 0x03u) |
                     (((idx >> 4) & 0x03u) << 2) |
                     (((idx >> 2) & 0x03u) << 4) |
                     (idx & 0xC0u));
}

static inline uint32_t bus_state(bool selected, bool rambo_active)
{
    return ((selected ? 1u : 0u) << 1) | (rambo_active ? 1u : 0u);
}

/*
 * Control levels for the CT_SLOW pages, which also depend on A0-A7:
 *   PBI  $D1xx: VERA registers $D100-$D11F always (DEV_SEL_N + EXTSEL_N);
 *               EXTSEL_N for the rest of $D1xx except the $D1FF latch when
 *               the card is selected.
 *   CCTL $D5xx: DEV_SEL_N for $D500-$D5FE when selected.
 * Flag bits above bit 2 are preserved.
 */
static inline uint32_t bus_slow_ctrl(uint32_t c, uint32_t off8, bool selected, bool is_pbi)
{
    if (is_pbi)
    {
        if (off8 <= 0x1Fu)
            c &= ~(DEDIC_OUT_DEVSELN | DEDIC_OUT_EXTSEL);
        if (selected && off8 != 0xFFu)
            c &= ~DEDIC_OUT_EXTSEL;
    }
    else
    {
        if (selected && off8 != 0xFFu)
            c &= ~DEDIC_OUT_DEVSELN;
    }
    return c;
}

/*
 * Fill the control tables and the per-page data pointers.
 *   ctrl    : BUS_STATES x 256 entries
 *   rd_base : 256 page pointers (state independent; CT_* bits gate their use)
 *   rom     : 2 KB PBI ROM ($D800-$DFFF), ram: 512 B PBI RAM ($D600-$D7FF),
 *   rambo   : bank 0 of the 256 KB RAMbo array (CT_RAMBO adds bank * 16 KB)
 */
static inline void bus_build_tables(uint8_t ctrl[BUS_STATES][256],
                                    const uint8_t *rd_base[256],
                                    bool is_pbi, bool rambo_hw,
                                    const uint8_t *rom, const uint8_t *ram,
                                    const uint8_t *rambo)
{
    for (uint32_t idx = 0; idx < 256u; idx++)
    {
        uint8_t page = bus_idx_to_page(idx);

        rd_base[idx] = 0;
        if (page == 0xD6u || page == 0xD7u)
            rd_base[idx] = ram + (uint32_t)(page - 0xD6u) * 256u;
        else if (page >= 0xD8u && page <= 0xDFu)
            rd_base[idx] = rom + (uint32_t)(page - 0xD8u) * 256u;
        else if (page >= 0x40u && page <= 0x7Fu)
            rd_base[idx] = rambo + (uint32_t)(page - 0x40u) * 256u;

        for (uint32_t s = 0; s < BUS_STATES; s++)
        {
            bool selected     = (s & 2u) != 0;
            bool rambo_active = rambo_hw && (s & 1u) != 0;
            uint32_t c = DEDIC_OUT_CTRL_IDLE;

            if (is_pbi)
            {
                if (page == 0xD1u)
                    c |= CT_SLOW | CT_WR_REG;
                else if (selected && (page == 0xD6u || page == 0xD7u))
                    c = (c & ~DEDIC_OUT_EXTSEL) | CT_DRIVE | CT_WR_MEM;
                else if (selected && page >= 0xD8u && page <= 0xDFu)
                    c = (c & ~DEDIC_OUT_MPD) | CT_DRIVE;
            }
            else if (page == 0xD5u)
            {
                c |= CT_SLOW | CT_WR_REG;
            }

            if (page == 0xD3u)                     /* PIA: PORTB / PBCTL snoop */
                c |= CT_WR_REG;

            if (rambo_active && page >= 0x40u && page <= 0x7Fu)
                c = (c & ~DEDIC_OUT_EXTSEL) | CT_DRIVE | CT_RAMBO | CT_WR_MEM;

            ctrl[s][idx] = (uint8_t)c;
        }
    }
}

/* RAMbo bank (0-15) from PORTB bits 6, 5, 3, 2. */
static inline uint32_t bus_rambo_bank(uint8_t portb)
{
    return ((portb >> 2) & 0x03u) | ((portb >> 3) & 0x0Cu);
}

#endif /* BUS_DECODE_H */
