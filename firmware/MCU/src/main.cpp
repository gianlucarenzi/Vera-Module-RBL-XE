/**
 * esp32-main.cpp  --  Atari PBI / CCTL  VeraX16 Bus Controller
 *
 * Target MCU: ESP32-S3FN8 (QFN56)
 *
 * This firmware allows an ESP32-S3 to interface with the 6502 bus of an
 * Atari XL/XE computer. Both address spaces are handled at runtime from
 * the full 16-bit address bus (A0–A15):
 *   - PBI ($D100–$D1FF, $D600–$D7FF, $D800–$DFFF): VERA card registers and ROM
 *   - CCTL ($D500–$D5FF): cartridge control line (DEV_SEL_N only)
 *
 * This version uses a definitive 34-GPIO mapping (plus Serial on 43/44)
 * with software-based 16-bit address decoding.
 *
 * Style: Allman
 * Language: English
 */

#include <Arduino.h>
#include <driver/gpio.h>
#include <soc/gpio_struct.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include <driver/dedic_gpio.h>
#include <hal/cpu_ll.h>
#include <esp_timer.h>
#include "pbi-driver.h"
#include "bus_decode.h"

/* ===========================================================================
 * Hardware Pin Mapping (ESP32-S3FN8 QFN56)
 * =========================================================================== */

/* --- Bank 0 Signals (GPIO 0-31) --- */
#define PIN_PHI2        1    /* CPU Clock Phase 2 (Pin 6) */
#define PIN_RW          2    /* Read/Write, High = Read (Pin 7) */

/* D0-D7: GPIO 4 to 11 (Pins 9-16, contiguous in Bank 0) */
#define DBUS_MASK       0x00000FF0UL
static const uint8_t DBUS_PINS[8] = { 4, 5, 6, 7, 8, 9, 10, 11 };

/* A0-A9: GPIO 12 to 21 (Pins 17-27, pin 20 = VDD3P3_RTC, not GPIO) */
#define ABUS_LO_MASK    0x003FF000UL

/* --- Bank 1 Signals (GPIO 32-48) --- */
#define PIN_A12         33   /* Address Bit 12 (Pin 38) */
#define PIN_A13         34   /* Address Bit 13 (Pin 39) */
#define PIN_A10         35   /* Address Bit 10 (Pin 40) */
#define PIN_A11         36   /* Address Bit 11 (Pin 41) */
#define PIN_ARESET      37   /* Atari System Reset Output (Pin 42) */
#define PIN_CRESET      38   /* Vera Chip Reset Output (Pin 43) */
#define PIN_CDONE       39   /* Vera Programmed Status Input (Pin 44) */
#define PIN_DEV_SEL_N   40   /* Vera Chip Select Output (Pin 45, active LOW) */
#define PIN_EXTSEL_N    41   /* Atari MMU Disable Output (Pin 47, active LOW) */
#define PIN_MPD         42   /* Math Pack Disable — disables internal Atari Math Pack ROM $D800-$DFFF (Pin 48, active LOW) */
#define PIN_A14         45   /* Address Bit 14 (Pin 51) */
#define PIN_A15         46   /* Address Bit 15 (Pin 52) */

/* Serial Debug (UART0 default pins on ESP32-S3 QFN56) */
#define PIN_UART_TX     43   /* UART0 TX (Pin 49) */
#define PIN_UART_RX     44   /* UART0 RX (Pin 50) */

/* RAMbo hardware enable: external 10K pull-up = RAMbo present, pull-down = no RAMbo */
#define PIN_RAMBO_EN     3   /* GPIO 3 (Pin 8) — read once at boot */

/*
 * Strapping Pins — Power-On Boot Configuration (ESP32-S3FN8 QFN56)
 *
 * The ESP32-S3 samples four strapping pins during the power-on reset window,
 * before any firmware code executes.  The sampled levels select the boot mode
 * and several chip-level options.
 *
 * Timing note: PIN_ARESET (GPIO37) is configured as OUTPUT only inside setup().
 * At strapping-sample time it is still an input (high-Z), so the ESP32 has NOT
 * yet asserted the Atari reset line.  The Atari's own power-on reset circuitry
 * (MMU/POKEY RC network) holds the 6502 RESET# pin LOW, which tri-states the
 * 6502 address bus.  The TXS0108E level-shifters (U1–U4) auto-sense direction:
 * with floating inputs on the Atari side the outputs are high-Z, and the
 * ESP32-side internal pull resistors determine the final strapping level.
 *
 * GPIO0  —  Pin  5  |  Boot mode selection  |  Internal weak PULL-UP ~45 kΩ
 *   Net BOOT0: R33 4.7 kΩ pull-up, BOOT button SW3, C61, and R54 -> Q4 (auto-reset
 *   from the programmer).  Not wired to any Atari signal.
 *   Pull-up holds GPIO0 HIGH throughout the strapping window.
 *   Sampled value: HIGH → SPI boot from embedded flash (correct for normal
 *   operation).  LOW would enter UART/JTAG download mode and halt the system.
 *
 * GPIO3  —  Pin  8  |  JTAG signal source   |  NO internal pull (truly floating)
 *   Wired to RAMBO_EN with an external 10 kΩ resistor to VCC or GND.
 *   Per the ESP32-S3 datasheet (§3.4 "JTAG Signal Source Control"): this pin has
 *   no internal pull resistors and "the strapping value must be controlled by
 *   the external circuit that cannot be in a high impedance state" — an external
 *   resistor is therefore mandatory, not optional, regardless of RAMbo detection.
 *   If GPIO3's level ever mattered for JTAG source (see below), the polarity per
 *   Table 3-5 is: LOW → JTAG pins (MTDI/MTCK/MTMS/MTDO, GPIO39-42);
 *   HIGH → USB Serial/JTAG Controller.
 *     Pull-up  (RAMbo hardware present) → HIGH → USB Serial/JTAG Controller.
 *       Irrelevant: ARDUINO_USB_CDC_ON_BOOT=0 disables USB CDC; GPIO19/20
 *       are reassigned to A7/A8.
 *     Pull-down (no RAMbo)              → LOW  → JTAG pins (GPIO39-42).
 *       Irrelevant: gpio_reset_pin() reclaims all JTAG pins in setup() before
 *       the Dedicated GPIO bundle is created.
 *   In practice GPIO3's level is moot either way: Table 3-5 shows the default
 *   (unburned) eFuse row — EFUSE_DIS_PAD_JTAG=0, EFUSE_DIS_USB_JTAG=0,
 *   EFUSE_STRAP_JTAG_SEL=0 — makes GPIO3 "Ignored" and always selects the USB
 *   Serial/JTAG Controller. The GPIO39-42 polarity above only applies if
 *   EFUSE_STRAP_JTAG_SEL has been deliberately burned to 1 (one-time
 *   programmable; not done by default in Arduino/ESP-IDF).
 *
 * GPIO45 —  Pin 51  |  VDD_SPI voltage      |  Internal weak PULL-DOWN ~5 kΩ
 *   Wired to address bit A14 via TXS0108E U4 (Atari 5 V → ESP32 3.3 V).
 *   At power-on the 6502 RESET# is LOW → A14 pin is high-Z on the Atari side
 *   → U4 output floats → internal pull-down drives GPIO45 LOW.
 *   Sampled value: LOW → VDD_SPI = 3.3 V from VDD3P3_RTC (datasheet v2.2,
 *   Table 3-4, default eFuses).  This is the value the FN8 needs: its in-package
 *   flash runs from VDD_SPI at 3.3 V (Table 1-1).
 *   Risk: if A14 is HIGH during the sampling window, VDD_SPI = 1.8 V from the
 *   flash regulator and the in-package flash is under-powered: the chip does
 *   not boot.  A14 must be LOW at power-on, or burn EFUSE_VDD_SPI_FORCE = 1 and
 *   EFUSE_VDD_SPI_TIEH = 1 (one-time) to fix VDD_SPI at 3.3 V and ignore GPIO45.
 *
 * GPIO46 —  Pin 52  |  ROM messages on UART |  Internal weak PULL-DOWN ~5 kΩ
 *   Wired to address bit A15 via TXS0108E U4.  Identical power-on scenario:
 *   6502 reset → A15 high-Z → U4 floats → pull-down → GPIO46 LOW.
 *   Sampled value: LOW → first-stage ROM boot messages on UART0 suppressed.
 *   Correct for production: the firmware initialises UART0 independently via
 *   Serial.begin() and uses it exclusively for debug output.  Enabling ROM
 *   messages (HIGH) would prepend Espressif boot log lines before the firmware
 *   output, which is undesirable but not harmful.
 */

/* ===========================================================================
 * Helper Macros & Inline Functions
 * =========================================================================== */

/**
 * Decode D0-D7 from Bank 0 snapshot.
 */
static inline uint8_t IRAM_ATTR decode_data(uint32_t lo)
{
    return (uint8_t)((lo >> 4) & 0xFFu);
}

/*
 * Dedicated GPIO channel mapping — ESP32-S3 Xtensa LX7 TIE instructions
 * (ee.get_gpio_in / ee.wr_mask_gpio_out bypass the APB bus: ~1 cycle vs ~6).
 *
 * Input channels  (bit index in cpu_ll_read_dedic_gpio_in() result):
 *   Ch 0 = PHI2       (GPIO  1)
 *
 * Output channels (bit index in cpu_ll_write_dedic_gpio_mask() mask/val):
 *   Ch 0 = DEV_SEL_N  (GPIO 40)  active LOW
 *   Ch 1 = EXTSEL_N   (GPIO 41)  active LOW
 *   Ch 2 = MPD        (GPIO 42)  active LOW
 *
 * Input and output channel spaces are independent on ESP32-S3.
 */
#define DEDIC_IN_PHI2        (1u << 0)
/* Output channel bits (DEDIC_OUT_*) are defined in bus_decode.h */

/* ===========================================================================
 * Compile-time Configuration
 * =========================================================================== */

#define PBI_DEV_ID        0x80u  /* PBI Device ID bit 7 */

/* Per-register trace (EVT_REG) costs ~1 us per access inside the bus loop:
 * enough to miss the next PHI2 edge or leave DEV_SEL_N asserted into the
 * following cycle (spurious VERA accesses).  Debug builds only. */
#ifndef VERA_TRACE_REGS
#define VERA_TRACE_REGS 0
#endif

/* Delay between CDONE high and the release of the Atari reset (VERA POR). */
#ifndef VERA_POST_CDONE_DELAY_US
#define VERA_POST_CDONE_DELAY_US 1000u
#endif

/* VERA_BOARD_IS_PBI is injected by the build system (-D flag in platformio.ini) */
#ifndef VERA_BOARD_IS_PBI
#define VERA_BOARD_IS_PBI 0x01u  /* fallback: PBI */
#endif

/* ===========================================================================
 * Log Ring (Core 0 bus loop → Core 1 loop(), which prints on Serial)
 *
 * Single producer / single consumer, lock-free: the producer only stores the
 * event and advances `head` (a few instructions).  xQueueSend() would take a
 * spinlock and a critical section (~1 us), losing the bus cycles that follow
 * the logged one -- after a $D1FF write those are the PBI ROM fetches.
 * When the ring is full new events are dropped and counted.
 * =========================================================================== */
#define LOG_RING_SIZE 64u   /* power of two */

#define EVT_LATCH 0   /* VCS/CCTL latch state change */
#define EVT_REG   1   /* VERA register ($D1xx) access */

/* flags: bit0=write(1)/read(0)  bit1=latch state (EVT_LATCH)  bit2=MPD asserted  bit3=EXTSEL asserted */
typedef struct
{
    uint32_t ts_ms;   /* FreeRTOS tick count (1 ms) on Core 0 */
    uint8_t  type;
    uint8_t  offset;  /* register offset: 0x00-0xFF within address range */
    uint8_t  data;
    uint8_t  flags;
} LogEvt;

/* ===========================================================================
 * Shared Data
 * =========================================================================== */

/* Hot-loop data lives in DRAM (data bus), not in IRAM (instruction bus). */
static DRAM_ATTR uint32_t lut_drive[256];   /* Lookup table for fast data bus drive */
static DRAM_ATTR uint8_t  ram_pbi[512];     /* 512B RAM at $D600-$D7FF */

/* Bus decode tables (see bus_decode.h), built in setup(). */
static DRAM_ATTR uint8_t        bus_ctrl[BUS_STATES][256];
static DRAM_ATTR const uint8_t *bus_rd_base[256];

static bool rambo_hw_enabled = false; /* set in setup() reading PIN_RAMBO_EN */

/* 256 KB extended RAM (RAMbo) — IRAM BSS: resides in IRAM, zero Flash cost. */
static uint8_t extended_rambo_256k[256 * 1024] __attribute__((section(".iram0.bss")));

/*
 * PORTB ($D301) — PIA 6520 Output Register B, used for RAMbo bank switching.
 * Accessible at $D301 only when PBCTL bit 2 (CRB-2) = 1; when bit 2 = 0,
 * $D301 addresses the Data Direction Register (DDR) — those writes are ignored here.
 *
 * Bit 4 (RAME — RAM Enable):
 *   0 = RAMbo active: all 16 banks of $4000-$7FFF accessible; EXTSEL_N asserted
 *       to suppress Atari internal RAM in that window.
 *   1 = RAMbo disabled: Atari internal RAM responds at $4000-$7FFF normally.
 *
 * Bank number (0-15) from PORTB bits 6, 5, 3, 2:
 *   bank = ((portb >> 2) & 0x03) | ((portb >> 3) & 0x0C)
 *   Bit layout: 7   6   5   4    3   2   1   0
 *               -   b3  b2  RAME b1  b0  -   -    (b3:b0 = bank number)
 *
 * Ref: RAMBO manual (atarimania.com/documents/rambo_manual.pdf), p.14;
 *      Altirra Hardware Reference Manual §8; Motorola 6520 PIA datasheet.
 * Initialised to 0xFF (RAME = 1) — RAMbo disabled until OS writes $D301.
 */
static DRAM_ATTR uint8_t PORTB = 0xFFu;

/*
 * PBCTL ($D303) — PIA 6520 Control Register B.
 * Bit 2 (CRB-2) selects which register is exposed at $D301:
 *   0 → Data Direction Register (DDR): each bit selects pin direction
 *       (0 = input, 1 = output); DDR writes are NOT bank-selection writes.
 *   1 → Output Register (PORTB): bank-selection register.
 *
 * "For external PIA emulation, you must not only save the data destined for
 *  $D301. You must also intercept and save writes to PBCTL $D303 bit 2 to
 *  enable writing to $D301 *only* when PBCTL bit 2 = 1. This further
 *  qualifies writes to the output register at $D301 (which is what you see
 *  externally on the PIA), not the data direction register at $D301, as there
 *  are two registers at the same location." — warerat, AtariAge forums.
 *
 * Ref: Motorola 6520 PIA datasheet; Altirra Hardware Reference Manual §8.
 * Initialised to 0x00 (bit 2 = 0 → DDR exposed at $D301); the OS sets bit 2
 * before using $D301 as the bank-selection register.
 */
static DRAM_ATTR uint8_t PBCTL = 0x00u;

static DRAM_ATTR LogEvt   log_ring[LOG_RING_SIZE];
static DRAM_ATTR uint32_t log_head = 0;     /* written by Core 0 only */
static DRAM_ATTR uint32_t log_tail = 0;     /* written by Core 1 only */
static DRAM_ATTR uint32_t log_dropped = 0;  /* written by Core 0 only */

/* ===========================================================================
 * Bus Drive/Release Logic
 * =========================================================================== */

/**
 * Precalculate bitmasks for each possible byte to avoid runtime logic.
 */
static void build_drive_lut(void)
{
    for (int v = 0; v < 256; v++)
    {
        uint32_t m = 0;
        for (int b = 0; b < 8; b++)
        {
            if (v & (1 << b))
            {
                m |= (1UL << DBUS_PINS[b]);
            }
        }
        lut_drive[v] = m;
    }
}

/**
 * Drive the data bus with a byte value.
 * GPIO.out covers Bank 0 entirely; writing lut_drive[val] sets D0-D7 (bits 4-11)
 * and zeros the remaining bits, which are all input-configured and ignore GPIO.out.
 * This saves one APB write compared to the w1tc+w1ts pattern.
 */
static inline void IRAM_ATTR bus_drive(uint8_t val)
{
    GPIO.out = lut_drive[val];
    GPIO.enable_w1ts = DBUS_MASK;
}

/**
 * Release the data bus (set to High-Z).
 */
static inline void IRAM_ATTR bus_release(void)
{
    GPIO.enable_w1tc = DBUS_MASK;
}

/* Push a log event into the ring without blocking (drops if full). */
static inline void IRAM_ATTR log_send(uint8_t type, uint8_t offset,
                                      uint8_t data, uint8_t flags)
{
    uint32_t h = log_head;
    if (h - __atomic_load_n(&log_tail, __ATOMIC_ACQUIRE) >= LOG_RING_SIZE)
    {
        log_dropped++;
        return;
    }
    LogEvt *e = &log_ring[h & (LOG_RING_SIZE - 1u)];
    e->ts_ms  = (uint32_t)xTaskGetTickCount();
    e->type   = type;
    e->offset = offset;
    e->data   = data;
    e->flags  = flags;
    __atomic_store_n(&log_head, h + 1u, __ATOMIC_RELEASE);
}

/* ===========================================================================
 * MonitorTask -- Core 0, IRAM-resident
 *
 * Core 0 runs only this task (it never yields); Core 1 runs setup()/loop(),
 * Serial and its UART interrupt, and prints the events pushed into log_ring.
 * =========================================================================== */
static void IRAM_ATTR MonitorTask(void *arg)
{
    /*
     * Dedicated GPIO bundles — MUST be created on the executing core (Core 0).
     *
     * Input  bundle: [PHI2]                    → input  ch 0 (bit 0 of ee.get_gpio_in)
     * Output bundle: [DEV_SEL_N, EXTSEL_N, MPD] → output ch 0-2 (bits 0-2 of
     *                                              ee.wr_mask_gpio_out)
     *
     * Input and output channel pools are independent; ch 0 means different pins
     * in each pool.
     */
    static const int dedic_in_pins[]  = { PIN_PHI2 };
    static const int dedic_out_pins[] = { PIN_DEV_SEL_N, PIN_EXTSEL_N, PIN_MPD };
    dedic_gpio_bundle_handle_t dedic_in_bundle, dedic_out_bundle;
    const dedic_gpio_bundle_config_t in_cfg =
    {
        .gpio_array = dedic_in_pins,
        .array_size = 1,
        .flags      = { .in_en = 1 }
    };
    const dedic_gpio_bundle_config_t out_cfg =
    {
        .gpio_array = dedic_out_pins,
        .array_size = 3,
        .flags      = { .out_en = 1 }
    };
    ESP_ERROR_CHECK(dedic_gpio_new_bundle(&in_cfg,  &dedic_in_bundle));
    ESP_ERROR_CHECK(dedic_gpio_new_bundle(&out_cfg, &dedic_out_bundle));
    /* Immediately set all control signals HIGH (deasserted) to prevent the brief
     * glitch that would occur if the dedicated GPIO output register starts at 0. */
    cpu_ll_write_dedic_gpio_mask(DEDIC_OUT_CTRL_MASK, DEDIC_OUT_CTRL_IDLE);

    bus_release();

    const bool is_pbi = VERA_BOARD_IS_PBI;

    /* Card state, kept in registers; the tables are switched by pointer. */
    bool           selected  = false;
    uint8_t        portb     = PORTB;
    uint8_t        pbctl     = PBCTL;
    bool           rambo_on  = rambo_hw_enabled && !(portb & 0x10u);
    const uint8_t *ctrl_cur  = bus_ctrl[bus_state(selected, rambo_on)];
    uint32_t       rambo_off = bus_rambo_bank(portb) << 14;

    /*
     * Startup sync sequence — executed once before the hot loop:
     *  1. Release VERA FPGA from reset → starts loading bitstream from SPI flash.
     *  2. Poll CDONE until HIGH (ICE40UP5K typically < 100 ms).
     *  3. Release Atari from reset → bus goes live; hot loop must be ready.
     */
    GPIO.out1_w1ts.val = (1UL << (PIN_CRESET - 32));
    Serial.println("[VeraX16] VERA: released from reset, waiting CDONE...");
    {
        uint32_t elapsed_ms = 0u;
        while (!(GPIO.in1.val & (1UL << (PIN_CDONE - 32))))
        {
            vTaskDelay(1);
            elapsed_ms += portTICK_PERIOD_MS;
            if (elapsed_ms >= 5000u)
            {
                Serial.println("[VeraX16] VERA: CDONE timeout — FPGA not configured!");
                break;
            }
        }
        if (elapsed_ms < 5000u)
            Serial.printf("[VeraX16] VERA: CDONE OK (~%u ms)\n", elapsed_ms);
    }
    /* CDONE only says the bitstream is loaded: the VERA still runs its
     * internal power-on reset (128 clk25 cycles + reset synchroniser, a few
     * us).  Give it a comfortable margin before the 6502 can touch the bus. */
    delayMicroseconds(VERA_POST_CDONE_DELAY_US);
    GPIO.out1_w1ts.val = (1UL << (PIN_ARESET - 32));
    Serial.println("[VeraX16] ATARI: released from reset — bus active");

    for (;;)
    {
        /* 1. Wait for PHI2 rising edge.
         *    ee.get_gpio_in is a single Xtensa TIE instruction (~1 cycle),
         *    vs ~6 cycles for a GPIO.in read through the APB bus. */
        while (!(cpu_ll_read_dedic_gpio_in() & DEDIC_IN_PHI2));
        uint32_t g_lo = GPIO.in;
        uint32_t g_hi = GPIO.in1.val;

        /* 2. Page lookup: control levels and actions for A8-A15 in the
         *    current state.  Only $D1xx (PBI) / $D5xx (CCTL) also need A0-A7. */
        uint32_t idx  = bus_page_idx(g_lo, g_hi);
        uint32_t c    = ctrl_cur[idx];
        uint32_t off8 = (g_lo >> 12) & 0xFFu;
        if (c & CT_SLOW)
            c = bus_slow_ctrl(c, off8, selected, is_pbi);

        /* 3. Assert control signals atomically (one ee.wr_mask_gpio_out;
         *    the mask keeps the CT_* flag bits off the pins). */
        cpu_ll_write_dedic_gpio_mask(DEDIC_OUT_CTRL_MASK, c);

        if (g_lo & (1UL << PIN_RW))
        {
            /* 4a. Read cycle: drive PBI ROM / PBI RAM / RAMbo data. */
            if (c & CT_DRIVE)
            {
                const uint8_t *p = bus_rd_base[idx];
                if (c & CT_RAMBO)
                    p += rambo_off;
                bus_drive(p[off8]);
            }
#if VERA_TRACE_REGS
            if ((c & CT_SLOW) && is_pbi && !(c & DEDIC_OUT_DEVSELN))
            {
                /* VERA drives the bus: sample it just before PHI2 falls. */
                uint32_t last = g_lo, now;
                while ((now = GPIO.in) & (1UL << PIN_PHI2))
                    last = now;
                uint8_t fl = 0x00;  /* read */
                if (!(c & DEDIC_OUT_MPD))    fl |= 0x04;
                if (!(c & DEDIC_OUT_EXTSEL)) fl |= 0x08;
                log_send(EVT_REG, (uint8_t)off8, decode_data(last), fl);
            }
#endif
            /* 5a. Wait for PHI2 falling edge, then release bus and controls. */
            while (cpu_ll_read_dedic_gpio_in() & DEDIC_IN_PHI2);
            if (c & CT_DRIVE)
                bus_release();
            cpu_ll_write_dedic_gpio_mask(DEDIC_OUT_CTRL_MASK, DEDIC_OUT_CTRL_IDLE);
            continue;
        }

        /* 4b. Write cycle: the 6502 data is valid only ~100-150 ns after PHI2
         *     rise and held ~10-30 ns after PHI2 fall.  Keep re-reading GPIO.in
         *     (PHI2 is in the same register) and use the last snapshot taken
         *     with PHI2 still high. */
        uint32_t last = g_lo, now;
        while ((now = GPIO.in) & (1UL << PIN_PHI2))
            last = now;
        cpu_ll_write_dedic_gpio_mask(DEDIC_OUT_CTRL_MASK, DEDIC_OUT_CTRL_IDLE);

        /* 5b. Side effects, during PHI1 (bus already released). */
        uint8_t data = decode_data(last);

        if (c & CT_WR_MEM)
        {
            uint8_t *p = (uint8_t *)bus_rd_base[idx];
            if (c & CT_RAMBO)
                p += rambo_off;
            p[off8] = data;
        }

        if (c & CT_WR_REG)
        {
            uint8_t page = bus_idx_to_page(idx);
            bool    state_changed = false;

            if (off8 == 0xFFu && page == (is_pbi ? 0xD1u : 0xD5u))
            {
                /* PBI: each device latches only its own data bit.
                 * CCTL: write $80 selects, $00 deselects. */
                bool new_sel = ((data & PBI_DEV_ID) != 0u);
                if (new_sel != selected)
                {
                    selected      = new_sel;
                    state_changed = true;
                    uint8_t fl = 0x01u | (selected ? 0x02u : 0x00u);
                    if (!(c & DEDIC_OUT_MPD))    fl |= 0x04u;
                    if (!(c & DEDIC_OUT_EXTSEL)) fl |= 0x08u;
                    log_send(EVT_LATCH, (uint8_t)off8, data, fl);
                }
            }
            else if (page == 0xD3u)
            {
                /*
                 * RAMbo PIA emulation: snoop PBCTL ($D303) and PORTB ($D301).
                 *
                 * $D303 must be intercepted first: it controls which register
                 * is accessible at $D301. Only update PORTB when PBCTL bit 2 = 1
                 * (output register mode). When bit 2 = 0, $D301 addresses the
                 * Data Direction Register — those writes must be silently discarded,
                 * otherwise the DDR value would corrupt the bank-selection state.
                 *
                 * Typical OS init sequence:
                 *   write 0x00 → $D303  (PBCTL bit2=0 → DDR selected at $D301)
                 *   write 0xFF → $D301  (set all PORTB pins as outputs — DDR write)
                 *   write 0x34 → $D303  (PBCTL bit2=1 → output register at $D301)
                 *   write 0xNN → $D301  (bank-selection write — now captured here)
                 */
                if (off8 == 0x03u)
                {
                    pbctl = data;
                    PBCTL = data;
                }
                else if (off8 == 0x01u && (pbctl & 0x04u))
                {
                    portb         = data;
                    PORTB         = data;
                    rambo_off     = bus_rambo_bank(portb) << 14;
                    rambo_on      = rambo_hw_enabled && !(portb & 0x10u);
                    state_changed = true;
                }
            }
#if VERA_TRACE_REGS
            else if (is_pbi && page == 0xD1u && !(c & DEDIC_OUT_DEVSELN))
            {
                uint8_t fl = 0x01u;  /* write */
                if (!(c & DEDIC_OUT_MPD))    fl |= 0x04u;
                if (!(c & DEDIC_OUT_EXTSEL)) fl |= 0x08u;
                log_send(EVT_REG, (uint8_t)off8, data, fl);
            }
#endif
            if (state_changed)
                ctrl_cur = bus_ctrl[bus_state(selected, rambo_on)];
        }
    }
}

/* ===========================================================================
 * System Setup
 * =========================================================================== */
void setup(void)
{
    /* Reclaim JTAG pins (GPIO 39-42) for regular GPIO usage */
    gpio_reset_pin((gpio_num_t)PIN_CDONE);     /* GPIO 39 */
    gpio_reset_pin((gpio_num_t)PIN_DEV_SEL_N); /* GPIO 40 */
    gpio_reset_pin((gpio_num_t)PIN_EXTSEL_N);  /* GPIO 41 */
    gpio_reset_pin((gpio_num_t)PIN_MPD);       /* GPIO 42 */

    /* Hold Atari and VERA in reset immediately — released by MonitorTask
     * only after CDONE confirms the FPGA bitstream is loaded. */
    pinMode(PIN_ARESET, OUTPUT);
    digitalWrite(PIN_ARESET, LOW);
    pinMode(PIN_CRESET, OUTPUT);
    digitalWrite(PIN_CRESET, LOW);

    /* Initialize Serial for Debug (UART0 default pins 43/44) */
    Serial.begin(115200, SERIAL_8N1, PIN_UART_RX, PIN_UART_TX);

    pinMode(PIN_RAMBO_EN, INPUT);
    rambo_hw_enabled = (digitalRead(PIN_RAMBO_EN) == HIGH);
    if (rambo_hw_enabled)
        memset(extended_rambo_256k, 0x00, sizeof(extended_rambo_256k));

    if (VERA_BOARD_IS_PBI)
    {
        Serial.println("[VeraX16] Mode: PBI ($D1xx / $D6xx / $D8xx, latch $D1FF)");
        memset(ram_pbi, 0x00, sizeof(ram_pbi));
        build_drive_lut();
    }
    else
    {
        Serial.println("[VeraX16] Mode: CCTL ($D5xx, latch $D5FF)");
        if (rambo_hw_enabled)
            build_drive_lut(); /* RAMbo reads need bus_drive() also in CCTL mode */
    }

    Serial.printf("[VeraX16] RAMbo 256K: %s\n", rambo_hw_enabled ? "ENABLED" : "DISABLED");

    bus_build_tables(bus_ctrl, bus_rd_base, VERA_BOARD_IS_PBI, rambo_hw_enabled,
                     pbi_driver, ram_pbi, extended_rambo_256k);

    /* Configure GPIO Modes */
    gpio_config_t cfg = {};
    cfg.intr_type    = GPIO_INTR_DISABLE;
    cfg.pull_up_en   = GPIO_PULLUP_DISABLE;
    cfg.pull_down_en = GPIO_PULLDOWN_DISABLE;
    cfg.mode         = GPIO_MODE_INPUT;

    /* Bank 0 inputs: PHI2, RW, D0-D7, A0-A9 */
    cfg.pin_bit_mask = (1ULL << PIN_PHI2) | (1ULL << PIN_RW) | DBUS_MASK | ABUS_LO_MASK;
    gpio_config(&cfg);


    /* Bank 1 inputs: A10-A15, CDONE */
    cfg.pin_bit_mask = (1ULL << PIN_A10) | (1ULL << PIN_A11) |
                       (1ULL << PIN_A12) | (1ULL << PIN_A13) |
                       (1ULL << PIN_A14) | (1ULL << PIN_A15) |
                       (1ULL << PIN_CDONE);
    gpio_config(&cfg);

    /* Output Signal Initialization */
    pinMode(PIN_EXTSEL_N, OUTPUT);
    digitalWrite(PIN_EXTSEL_N, HIGH);

    pinMode(PIN_DEV_SEL_N, OUTPUT);
    digitalWrite(PIN_DEV_SEL_N, HIGH);

    pinMode(PIN_MPD, OUTPUT);
    digitalWrite(PIN_MPD, HIGH);

    /* Start High-Priority Monitor Task on Core 0 (dedicated bus management).
     * It never yields, so the Core 0 idle task must leave the task watchdog.
     * setup()/loop() stay on Core 1 (ARDUINO_RUNNING_CORE) with Serial. */
    disableCore0WDT();
    xTaskCreatePinnedToCore(
        MonitorTask, "BusMonitor",
        4096, NULL,
        configMAX_PRIORITIES - 1,
        NULL, 0
    );

    Serial.println("[VeraX16] Bus Monitor active on Core 0");
}

/* ---------------------------------------------------------------------------
 * VERA register name lookup — Core 1 only (loop()), not IRAM-resident.
 * Registers $09-$0C are muxed by DCSEL (bits [2:1] of VERA_CTRL, offset $05).
 * --------------------------------------------------------------------------- */
static const char *vera_reg_name(uint8_t offset, uint8_t dcsel)
{
    static const char *base[8] = {
        "VERA_ADDR_L",  /* $00 */
        "VERA_ADDR_M",  /* $01 */
        "VERA_ADDR_H",  /* $02 */
        "VERA_DATA0",   /* $03 */
        "VERA_DATA1",   /* $04 */
        "VERA_CTRL",    /* $05 */
        "VERA_IEN",     /* $06 */
        "VERA_ISR",     /* $07 */
    };
    if (offset < 8)
        return base[offset];

    if (offset == 0x08)
        return "VERA_IRQLINE_L";

    if (offset >= 0x09 && offset <= 0x0C)
    {
        static const char *mux[7][4] = {
            /* DCSEL=0 */
            { "VERA_DC_VIDEO",    "VERA_DC_HSCALE",       "VERA_DC_VSCALE",       "VERA_DC_BORDER"      },
            /* DCSEL=1 */
            { "VERA_DC_HSTART",   "VERA_DC_HSTOP",        "VERA_DC_VSTART",       "VERA_DC_VSTOP"       },
            /* DCSEL=2 */
            { "VERA_FX_CTRL",     "VERA_FX_TILEBASE",     "VERA_FX_MAPBASE",      "VERA_FX_MULT"        },
            /* DCSEL=3 */
            { "VERA_FX_X_INCR_L", "VERA_FX_X_INCR_H",    "VERA_FX_Y_INCR_L",    "VERA_FX_Y_INCR_H"   },
            /* DCSEL=4 */
            { "VERA_FX_X_POS_L",  "VERA_FX_X_POS_H",     "VERA_FX_Y_POS_L",     "VERA_FX_Y_POS_H"    },
            /* DCSEL=5 */
            { "VERA_FX_X_POS_S",  "VERA_FX_Y_POS_S",     "VERA_FX_POLY_FILL_L", "VERA_FX_POLY_FILL_H" },
            /* DCSEL=6 */
            { "VERA_FX_CACHE_L",  "VERA_FX_CACHE_M",     "VERA_FX_CACHE_H",     "VERA_FX_CACHE_U"     },
        };
        return mux[(dcsel < 7) ? dcsel : 0][offset - 0x09];
    }

    if (offset >= 0x0D && offset <= 0x13)
    {
        static const char *l0[7] = {
            "VERA_L0_CONFIG",    /* $0D */
            "VERA_L0_MAPBASE",   /* $0E */
            "VERA_L0_TILEBASE",  /* $0F */
            "VERA_L0_HSCR_L",   /* $10 */
            "VERA_L0_HSCR_H",   /* $11 */
            "VERA_L0_VSCR_L",   /* $12 */
            "VERA_L0_VSCR_H",   /* $13 */
        };
        return l0[offset - 0x0D];
    }

    if (offset >= 0x14 && offset <= 0x1A)
    {
        static const char *l1[7] = {
            "VERA_L1_CONFIG",    /* $14 */
            "VERA_L1_MAPBASE",   /* $15 */
            "VERA_L1_TILEBASE",  /* $16 */
            "VERA_L1_HSCR_L",   /* $17 */
            "VERA_L1_HSCR_H",   /* $18 */
            "VERA_L1_VSCR_L",   /* $19 */
            "VERA_L1_VSCR_H",   /* $1A */
        };
        return l1[offset - 0x14];
    }

    return "?";
}

/* ===========================================================================
 * Main Loop -- Core 1: drain log_ring (filled by Core 0) and print
 * =========================================================================== */
void loop(void)
{
    /* Shadow of VERA_CTRL DCSEL bits [2:1]; updated on every write to $D105. */
    static uint8_t  dcsel = 0;
    static uint32_t dropped_seen = 0;

    uint32_t t = log_tail;
    if (t == __atomic_load_n(&log_head, __ATOMIC_ACQUIRE))
    {
        vTaskDelay(pdMS_TO_TICKS(10));
        return;
    }
    while (t != __atomic_load_n(&log_head, __ATOMIC_ACQUIRE))
    {
        LogEvt evt = log_ring[t & (LOG_RING_SIZE - 1u)];
        __atomic_store_n(&log_tail, ++t, __ATOMIC_RELEASE);

        uint32_t sec = evt.ts_ms / 1000u;
        uint32_t ms  = evt.ts_ms % 1000u;
        if (evt.type == EVT_LATCH)
        {
            bool enabled = (evt.flags & 0x02) != 0;
            Serial.printf("[%5lu.%03lu] [VCS ] Latch %s ($%02X written to %s) MPD=%s EXTSEL=%s\n",
                          sec, ms,
                          enabled ? "ENABLED " : "DISABLED", evt.data,
                          VERA_BOARD_IS_PBI ? "$D1FF" : "$D5FF",
                          (evt.flags & 0x04) ? "LO" : "HI",
                          (evt.flags & 0x08) ? "LO" : "HI");
        }
        else  /* EVT_REG */
        {
            /* Track DCSEL so muxed registers ($09-$0C) resolve correctly. */
            if (evt.offset == 0x05 && (evt.flags & 0x01))
                dcsel = (evt.data >> 1) & 0x07;

            Serial.printf("[%5lu.%03lu] [D1%02X - %-20s] %c $%02X  MPD=%s EXTSEL=%s\n",
                          sec, ms,
                          evt.offset,
                          vera_reg_name(evt.offset, dcsel),
                          (evt.flags & 0x01) ? 'W' : 'R',
                          evt.data,
                          (evt.flags & 0x04) ? "LO" : "HI",
                          (evt.flags & 0x08) ? "LO" : "HI");
        }
    }

    uint32_t dropped = log_dropped;
    if (dropped != dropped_seen)
    {
        Serial.printf("[VeraX16] log: %lu events dropped (ring full)\n",
                      (unsigned long)(dropped - dropped_seen));
        dropped_seen = dropped;
    }
}
