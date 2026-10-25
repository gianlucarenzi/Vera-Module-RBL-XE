/**
 * bus_decode.h  --  Table-driven 6502 bus decode for MonitorTask
 *
 * MonitorTask decodes the address during PHI1, as soon as it is stable, so
 * that EXTSEL_N / MPD / DEV_SEL_N and the read data are ready around the
 * PHI2 rising edge (~279 ns of PHI2 high at 1.79 MHz, 50 ns of it being the
 * 6502 read setup time).  Everything that depends only on the page (A8-A15)
 * and on the card state is precomputed here into 256-entry tables.
 *
 * Page: A8-A15 in natural order, built from two snapshots:
 *   GPIO.in               bits 20,21 = A8, A9
 *   dedicated GPIO input  bits 2..7  = A10..A15   (bit 0 = PHI2, bit 1 = R/W)
 *
 * Table entry (uint8_t):
 *   bits 0-2  dedicated GPIO output levels: DEV_SEL_N, EXTSEL_N, MPD
 *   CT_SLOW   ctrl also depends on A0-A7 ($D1xx in PBI, $D5xx in CCTL)
 *   CT_DRIVE  read cycle: drive rd_base[page][A0-A7] on the data bus
 *   CT_RAMBO  add the current RAMbo bank offset to rd_base[page]
 *   CT_WR_MEM write cycle: store data into rd_base[page][A0-A7]
 *   CT_WR_REG write cycle: latch / PIA snoop on this page
 *
 * The RAMbo 320K emulation is always present (no hardware enable).
 *
 * Card state (table set index):
 *   bit 0  RAMbo window active   (PORTB bit 4 = 0)
 *   bit 1  card selected         ($D1FF / $D5FF latch)
 *   bit 2  self-test ROM visible (PORTB bit 0 = 1, bit 7 = 0): it has priority
 *          over extended memory at $5000-$57FF, so those pages are not claimed
 *
 * No ESP-IDF dependencies: also compiled by the host test (test/).
 * Style: Allman
 */

#ifndef BUS_DECODE_H
#define BUS_DECODE_H

#include <stdint.h>
#include <stdbool.h>

/* The hot loop runs from IRAM: helpers must never become calls into flash. */
#define BUS_INLINE static inline __attribute__((always_inline))

#define DEDIC_OUT_DEVSELN    (1u << 0)
#define DEDIC_OUT_EXTSEL     (1u << 1)
#define DEDIC_OUT_MPD        (1u << 2)
#define DEDIC_OUT_CTRL_MASK  0x07u
#define DEDIC_OUT_CTRL_IDLE  0x07u   /* all HIGH = deasserted (active LOW) */

/* Dedicated GPIO input channels */
#define DEDIC_IN_PHI2        (1u << 0)
#define DEDIC_IN_RW          (1u << 1)
#define DEDIC_IN_A10_A15     0xFCu

#define CT_SLOW    (1u << 3)
#define CT_DRIVE   (1u << 4)
#define CT_RAMBO   (1u << 5)
#define CT_WR_MEM  (1u << 6)
#define CT_WR_REG  (1u << 7)

#define BUS_STATES 8u

/* Page number A15..A8 from the GPIO.in and dedicated input snapshots. */
BUS_INLINE uint32_t bus_page(uint32_t lo, uint32_t din)
{
    return ((lo >> 20) & 0x03u) | (din & DEDIC_IN_A10_A15);
}

BUS_INLINE uint32_t bus_state(bool selected, bool rambo_active, bool selftest)
{
    return (selftest ? 4u : 0u) | (selected ? 2u : 0u) | (rambo_active ? 1u : 0u);
}

/*
 * Control levels for the CT_SLOW pages, which also depend on A0-A7:
 *   PBI  $D1xx: only when the card is selected (Atari PBI rule: a device
 *               not selected through $D1FF must not answer): DEV_SEL_N for
 *               the VERA registers $D100-$D11F, EXTSEL_N for $D100-$D1FE.
 *               The $D1FF latch itself is never claimed.
 *   CCTL $D5xx: DEV_SEL_N for $D500-$D5FE when selected.
 * Flag bits above bit 2 are preserved.
 */
BUS_INLINE uint32_t bus_slow_ctrl(uint32_t c, uint32_t off8, bool selected, bool is_pbi)
{
    if (is_pbi)
    {
        if (selected && off8 <= 0x1Fu)
            c &= ~DEDIC_OUT_DEVSELN;
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
 *   ctrl    : BUS_STATES x 256 entries, indexed by page
 *   rd_base : 256 page pointers (state independent; CT_* bits gate their use)
 *   rom     : 2 KB PBI ROM ($D800-$DFFF), ram: 512 B PBI RAM ($D600-$D7FF),
 *   rambo   : bank 0 of the 256 KB RAMbo array (CT_RAMBO adds bank * 16 KB)
 */
static inline void bus_build_tables(uint8_t ctrl[BUS_STATES][256],
                                    const uint8_t *rd_base[256],
                                    bool is_pbi,
                                    const uint8_t *rom, const uint8_t *ram,
                                    const uint8_t *rambo)
{
    for (uint32_t page = 0; page < 256u; page++)
    {
        rd_base[page] = 0;
        if (page == 0xD6u || page == 0xD7u)
            rd_base[page] = ram + (page - 0xD6u) * 256u;
        else if (page >= 0xD8u && page <= 0xDFu)
            rd_base[page] = rom + (page - 0xD8u) * 256u;
        else if (page >= 0x40u && page <= 0x7Fu)
            rd_base[page] = rambo + (page - 0x40u) * 256u;

        for (uint32_t s = 0; s < BUS_STATES; s++)
        {
            bool selected     = (s & 2u) != 0;
            bool rambo_active = (s & 1u) != 0;
            bool selftest     = (s & 4u) != 0;
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

            if (rambo_active && page >= 0x40u && page <= 0x7Fu &&
                !(selftest && page >= 0x50u && page <= 0x57u))
                c = (c & ~DEDIC_OUT_EXTSEL) | CT_DRIVE | CT_RAMBO | CT_WR_MEM;

            ctrl[s][page] = (uint8_t)c;
        }
    }
}

/* RAMbo bank (0-15) from PORTB bits 6, 5, 3, 2. */
BUS_INLINE uint32_t bus_rambo_bank(uint8_t portb)
{
    return ((portb >> 2) & 0x03u) | ((portb >> 3) & 0x0Cu);
}

/* Port B pin levels: bits configured as inputs (DDR = 0) are pulled up. */
BUS_INLINE uint8_t bus_portb_pins(uint8_t out, uint8_t ddr)
{
    return (uint8_t)(out | (uint8_t)~ddr);
}

/* Self-test ROM visible at $5000-$57FF: kernel ROM on (bit 0), self-test on (bit 7 = 0). */
BUS_INLINE bool bus_selftest_visible(uint8_t portb)
{
    return (portb & 0x81u) == 0x01u;
}

#endif /* BUS_DECODE_H */
