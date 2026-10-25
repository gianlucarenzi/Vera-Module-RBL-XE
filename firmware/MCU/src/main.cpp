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
 *   - RAMbo 320K ($4000–$7FFF, 16 banks of 16 KB): always present
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
#include <soc/timer_group_struct.h>
#include <hal/wdt_hal.h>
#include <stddef.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <driver/dedic_gpio.h>
#include <hal/cpu_ll.h>
#include "vera_pbi_handler.h"
#include "bus_decode.h"

/* ===========================================================================
 * Hardware Pin Mapping (ESP32-S3FN8 QFN56)
 * =========================================================================== */

/* --- Bank 0 Signals (GPIO 0-31) --- */
#define PIN_PHI2        1    /* CPU Clock Phase 2 (Pin 6) */
#define PIN_RW          2    /* Read/Write, High = Read (Pin 7) */

/* D0-D7: GPIO 4 to 11 (Pins 9-16, contiguous in Bank 0): D0 = GPIO 4 */
#define DBUS_SHIFT      4
#define DBUS_MASK       (0xFFUL << DBUS_SHIFT)

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
 *   Not used by the firmware (the RAMbo emulation is always present).  Per the
 *   ESP32-S3 datasheet (§3.4 "JTAG Signal Source Control") this pin has no
 *   internal pull resistors and "the strapping value must be controlled by the
 *   external circuit that cannot be in a high impedance state": the board needs
 *   an external strapping resistor here (netlist 2026-10-23: pin 8 unconnected).
 *   Polarity per Table 3-5: LOW → JTAG pins (MTDI/MTCK/MTMS/MTDO, GPIO39-42);
 *   HIGH → USB Serial/JTAG Controller.  With the default (unburned) eFuses —
 *   EFUSE_DIS_PAD_JTAG=0, EFUSE_DIS_USB_JTAG=0, EFUSE_STRAP_JTAG_SEL=0 — GPIO3
 *   is "Ignored" and the USB Serial/JTAG Controller is always selected; the
 *   polarity only matters if EFUSE_STRAP_JTAG_SEL has been burned to 1.
 *   gpio_reset_pin() reclaims GPIO39-42 in setup() either way.
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
 * Bus Timing
 *
 * The 6502 drives the address and R/W at most t_ADS = 140 ns after the start
 * of PHI1 (R6502A, 2 MHz grade), i.e. ~140 ns before PHI2 rises, and samples
 * read data 50 ns before PHI2 falls.  The bus loop therefore decodes during
 * PHI1: it waits BUS_ADDR_SETTLE_NS after it sees PHI2 fall (t_ADS plus the
 * 74LVC4245 delay, minus the shortest PHI2 detection path), samples the
 * address once, asserts the control signals and drives read data around the
 * PHI2 rising edge.  Write data is sampled BUS_WR_SAMPLE_NS after PHI2 rises
 * (6502 write data valid after t_MDS = 100 ns, held until 30 ns after PHI2
 * falls).  Both values can be tuned with -D after measuring the real bus.
 * =========================================================================== */
#ifndef BUS_ADDR_SETTLE_NS
#define BUS_ADDR_SETTLE_NS  150u
#endif
#ifndef BUS_WR_SAMPLE_NS
#define BUS_WR_SAMPLE_NS    120u
#endif

#define CPU_MHZ             (F_CPU / 1000000UL)
#define NS_TO_CYCLES(ns)    (((ns) * CPU_MHZ + 999u) / 1000u)

/* ===========================================================================
 * Helper Macros & Inline Functions
 * =========================================================================== */

/**
 * Decode D0-D7 from Bank 0 snapshot.
 */
static inline __attribute__((always_inline)) uint8_t decode_data(uint32_t lo)
{
    return (uint8_t)((lo >> DBUS_SHIFT) & 0xFFu);
}

/*
 * Dedicated GPIO channel mapping — ESP32-S3 Xtensa LX7 TIE instructions
 * (ee.get_gpio_in / ee.wr_mask_gpio_out bypass the APB bus: ~1 cycle).
 *
 * Input channels  (bit index in cpu_ll_read_dedic_gpio_in() result):
 *   Ch 0 = PHI2 (GPIO 1), Ch 1 = R/W (GPIO 2), Ch 2-7 = A10-A15
 *   The GPIO matrix feeds the dedicated inputs without taking the pins away:
 *   the same signals stay visible in GPIO.in / GPIO.in1.
 *
 * Output channels (bit index in cpu_ll_write_dedic_gpio_mask() mask/val):
 *   Ch 0 = DEV_SEL_N  (GPIO 40)  active LOW
 *   Ch 1 = EXTSEL_N   (GPIO 41)  active LOW
 *   Ch 2 = MPD        (GPIO 42)  active LOW
 *
 * Input and output channel spaces are independent on ESP32-S3.
 * Channel bits (DEDIC_IN_* / DEDIC_OUT_*) are defined in bus_decode.h.
 */

/* ===========================================================================
 * Compile-time Configuration
 * =========================================================================== */

#define PBI_DEV_ID        0x80u  /* PBI Device ID bit 7 */

/* Per-register trace (EVT_REG) for debugging.  The read trace samples the
 * data bus until PHI2 falls, which delays the next cycle: debug builds only. */
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
 * Log Ring (Core 1 bus loop → Core 0 LogTask, which prints on Serial)
 *
 * Single producer / single consumer, lock-free: the producer only stores the
 * event and advances `head` (a few instructions, no FreeRTOS call: the bus
 * loop runs with interrupts masked).  The consumer adds the timestamp when it
 * drains the ring.  When the ring is full new events are dropped and counted.
 * =========================================================================== */
#define LOG_RING_SIZE 64u   /* power of two */

#define EVT_LATCH 0   /* VCS/CCTL latch state change */
#define EVT_REG   1   /* VERA register ($D1xx) access */

/* flags: bit0=write(1)/read(0)  bit1=latch state (EVT_LATCH)  bit2=MPD asserted  bit3=EXTSEL asserted */
typedef struct
{
    uint8_t  type;
    uint8_t  offset;  /* register offset: 0x00-0xFF within address range */
    uint8_t  data;
    uint8_t  flags;
} LogEvt;

/* ===========================================================================
 * Shared Data
 * =========================================================================== */

/* Hot-loop data lives in DRAM (data bus): IRAM is not byte-accessible. */
static DRAM_ATTR uint8_t  ram_pbi[512];     /* 512B RAM at $D600-$D7FF */

/* Bus decode tables (see bus_decode.h), built in setup(). */
static DRAM_ATTR uint8_t        bus_ctrl[BUS_STATES][256];
static DRAM_ATTR uint8_t        bus_slow[2][256];   /* $D1xx / $D5xx by A0-A7, [selected] */
static DRAM_ATTR uint8_t        bus_pb_state[256];  /* bus_portb_state() for each PORTB */
static DRAM_ATTR const uint8_t *bus_rd_base[256];

/*
 * 256 KB extended RAM (RAMbo) — ordinary .bss in DRAM, zeroed at startup.
 * It must not go in IRAM: the bus loop accesses it with 8-bit loads/stores,
 * and a section outside the ESP-IDF IRAM layout is not reserved on the data
 * bus (it would overlap .data, .bss and the heap through the DRAM alias).
 */
static uint8_t extended_rambo_256k[256 * 1024];

static_assert(sizeof(vera_pbi_handler) == 2048u, "PBI ROM must be 2 KB ($D800-$DFFF)");

/*
 * PORTB ($D301) — PIA 6520 Output Register B, used for RAMbo bank switching.
 * Accessible at $D301 only when PBCTL bit 2 (CRB-2) = 1; when bit 2 = 0,
 * $D301 addresses the Data Direction Register (DDR).  The bank selection
 * follows the Port B pin levels: output register bits where DDR = 1, pull-up
 * (1) where DDR = 0.  After a PIA reset both registers are 0, so the pins
 * read $FF (RAMbo disabled, OS ROM on).
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
 * With the kernel ROM enabled (bit 0 = 1) and the self-test ROM enabled
 * (bit 7 = 0) the self-test ROM has priority at $5000-$57FF: the card leaves
 * those pages alone.
 *
 * Ref: RAMBO manual (atarimania.com/documents/rambo_manual.pdf), p.14;
 *      Altirra Hardware Reference Manual §3.4-3.5, §8; Motorola 6520 PIA datasheet.
 */

/*
 * PBCTL ($D303) — PIA 6520 Control Register B.
 * Bit 2 (CRB-2) selects which register is exposed at $D301:
 *   0 → Data Direction Register (DDR): each bit selects pin direction
 *       (0 = input, 1 = output).
 *   1 → Output Register (PORTB).
 *
 * "For external PIA emulation, you must not only save the data destined for
 *  $D301. You must also intercept and save writes to PBCTL $D303 bit 2 to
 *  enable writing to $D301 *only* when PBCTL bit 2 = 1. This further
 *  qualifies writes to the output register at $D301 (which is what you see
 *  externally on the PIA), not the data direction register at $D301, as there
 *  are two registers at the same location." — warerat, AtariAge forums.
 *
 * The PIA decodes only A0-A1, so $D300-$D3FF mirrors its four registers.
 *
 * Ref: Motorola 6520 PIA datasheet; Altirra Hardware Reference Manual §8.
 */

static DRAM_ATTR LogEvt   log_ring[LOG_RING_SIZE];
static DRAM_ATTR uint32_t log_head = 0;     /* written by the bus loop only */
static DRAM_ATTR uint32_t log_tail = 0;     /* written by LogTask only */
static DRAM_ATTR uint32_t log_dropped = 0;  /* written by the bus loop only */

static volatile bool log_ready = false;     /* Serial started by LogTask */

/* Card state that the critical path never reads: kept in memory so that the
 * bus loop's registers stay free for the decode. */
typedef struct
{
    uint8_t selected;   /* $D1FF / $D5FF latch */
    uint8_t pbctl;      /* PIA reset: DDR exposed at $D301 */
    uint8_t orb;        /* PIA reset: output register 0 */
    uint8_t ddrb;       /* PIA reset: all inputs (pins read 1) */
    uint8_t pstate;     /* state bits from the Port B pins (bus_pb_state) */
    uint32_t log_evt;   /* event waiting for an idle PHI2-high phase, 0 = none */
} BusCard;

static DRAM_ATTR BusCard card = { 0, 0x00u, 0x00u, 0x00u, 0x00u, 0u };

/* Bit positions hard-coded in the MonitorTask assembly. */
static_assert(CT_SLOW == (1u << 3) && CT_DRIVE == (1u << 4) && CT_RAMBO == (1u << 5),
              "CT_* bits used by the bus loop assembly");
static_assert(DEDIC_IN_RW == (1u << 1) && DEDIC_IN_A10_A15 == 0xFCu &&
              DEDIC_OUT_CTRL_MASK == 7u, "dedicated GPIO channels used by the bus loop assembly");

/* Control table for the current card state. */
static inline __attribute__((always_inline)) const uint8_t *card_ctrl(void)
{
    return bus_ctrl[card.pstate | (card.selected << 1)];
}

/* ===========================================================================
 * Bus Drive/Release Logic
 * =========================================================================== */

/*
 * Data bus drive (in the MonitorTask assembly): GPIO.out covers Bank 0
 * entirely, so writing the byte shifted onto D0-D7 (bits 4-11) zeros the
 * remaining bits, which are all input-configured and ignore GPIO.out; then
 * GPIO.enable_w1ts turns the outputs on.  The stores are issued back to back,
 * without the memw that the compiler puts before every volatile access:
 * Xtensa keeps stores in program order, so the value still reaches GPIO.out
 * before the outputs are enabled, but the CPU does not wait for each APB
 * write to complete.
 */

/**
 * Release the data bus (set to High-Z).  Posted store, as above.
 */
static inline __attribute__((always_inline)) void bus_release(void)
{
    asm volatile("s32i  %0, %1, %2"
                 :
                 : "r"(DBUS_MASK), "r"(&GPIO), "n"(offsetof(gpio_dev_t, enable_w1tc))
                 : "memory");
}

static inline __attribute__((always_inline)) uint32_t dedic_in(void)
{
    return cpu_ll_read_dedic_gpio_in();
}

/*
 * PHI2 edge waits: one ee.get_gpio_in and one bit branch per sample
 * (3 cycles), the shortest polling loop.  They return the last sample.
 */
static inline __attribute__((always_inline)) void wait_phi2_high(void)
{
    uint32_t t;
    asm volatile("1: ee.get_gpio_in %0\n\t"
                 "   bbci %0, 0, 1b"
                 : "=&r"(t));
}

static inline __attribute__((always_inline)) void wait_phi2_low(void)
{
    uint32_t t;
    asm volatile("1: ee.get_gpio_in %0\n\t"
                 "   bbsi %0, 0, 1b"
                 : "=&r"(t));
}

/* Push a log event into the ring without blocking (drops if full). */
static inline __attribute__((always_inline)) void log_send(uint8_t type, uint8_t offset,
                                                           uint8_t data, uint8_t flags)
{
    uint32_t h = log_head;
    if (h - __atomic_load_n(&log_tail, __ATOMIC_ACQUIRE) >= LOG_RING_SIZE)
    {
        log_dropped++;
        return;
    }
    LogEvt *e = &log_ring[h & (LOG_RING_SIZE - 1u)];
    e->type   = type;
    e->offset = offset;
    e->data   = data;
    e->flags  = flags;
    __atomic_store_n(&log_head, h + 1u, __ATOMIC_RELEASE);
}

/*
 * A latch change is only recorded here; the event is built and pushed into
 * log_ring during the next read cycle, while the bus loop waits for PHI2 to
 * fall, so that the write's side effects stay short.
 * Layout: valid << 31 | offset << 16 | data << 8 | table entry of the write.
 */
static inline __attribute__((always_inline)) void log_defer_latch(uint32_t offset,
                                                                  uint32_t data, uint32_t c)
{
    card.log_evt = (1u << 31) | (offset << 16) | (data << 8) | (c & 0xFFu);
}

static inline __attribute__((always_inline)) void log_flush_deferred(void)
{
    const uint32_t e = card.log_evt;
    if (e)
    {
        card.log_evt = 0;
        const uint8_t data = (uint8_t)(e >> 8);
        uint8_t fl = 0x01u | ((data & PBI_DEV_ID) ? 0x02u : 0x00u);
        if (!(e & DEDIC_OUT_MPD))    fl |= 0x04u;
        if (!(e & DEDIC_OUT_EXTSEL)) fl |= 0x08u;
        log_send(EVT_LATCH, (uint8_t)(e >> 16), data, fl);
    }
}

/*
 * The bus loop runs with interrupts masked, so Core 1 no longer runs the
 * FreeRTOS tick hook that feeds the interrupt watchdog (MWDT1, checked for
 * both CPUs).  Stop that watchdog, or it resets the chip after 300 ms.
 */
static void bus_int_wdt_disable(void)
{
    wdt_hal_context_t ctx = {};
    ctx.inst = WDT_MWDT1;
    ctx.mwdt_dev = &TIMERG1;
    wdt_hal_write_protect_disable(&ctx);
    wdt_hal_disable(&ctx);
    wdt_hal_write_protect_enable(&ctx);
}

/* ===========================================================================
 * MonitorTask -- Core 1, IRAM-resident, interrupts masked
 *
 * Core 1 runs only this task: it never yields and nothing else runs there
 * (no tick, no interrupts), so no bus cycle is missed.  Core 0 runs LogTask,
 * Serial and its UART interrupt, and prints the events pushed into log_ring.
 * =========================================================================== */
static void IRAM_ATTR MonitorTask(void *arg)
{
    /*
     * Dedicated GPIO bundles — MUST be created on the executing core.
     *
     * Input  bundle: [PHI2, R/W, A10..A15]       → input  ch 0-7
     * Output bundle: [DEV_SEL_N, EXTSEL_N, MPD]  → output ch 0-2
     */
    static const int dedic_in_pins[]  = { PIN_PHI2, PIN_RW, PIN_A10, PIN_A11,
                                          PIN_A12, PIN_A13, PIN_A14, PIN_A15 };
    static const int dedic_out_pins[] = { PIN_DEV_SEL_N, PIN_EXTSEL_N, PIN_MPD };
    dedic_gpio_bundle_handle_t dedic_in_bundle, dedic_out_bundle;
    const dedic_gpio_bundle_config_t in_cfg =
    {
        .gpio_array = dedic_in_pins,
        .array_size = 8,
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

    /* The decode relies on the channel positions above. */
    uint32_t in_mask = 0, out_mask = 0;
    dedic_gpio_get_in_mask(dedic_in_bundle, &in_mask);
    dedic_gpio_get_out_mask(dedic_out_bundle, &out_mask);
    if (in_mask != 0xFFu || out_mask != DEDIC_OUT_CTRL_MASK)
    {
        Serial.printf("[VeraX16] FATAL: dedicated GPIO channels in=%02lX out=%02lX\n",
                      (unsigned long)in_mask, (unsigned long)out_mask);
        Serial.flush();
        abort();
    }

    bus_release();

    const bool     is_pbi    = VERA_BOARD_IS_PBI;
    const uint32_t settle    = NS_TO_CYCLES(BUS_ADDR_SETTLE_NS);
    const uint32_t wr_sample = NS_TO_CYCLES(BUS_WR_SAMPLE_NS);

    /* Decode state, kept in registers; the tables are switched by pointer. */
    const uint8_t  pins      = bus_portb_pins(card.orb, card.ddrb);
    card.pstate              = bus_pb_state[pins];
    const uint8_t *ctrl_cur  = card_ctrl();
    const uint8_t *slow_cur  = bus_slow[card.selected];
    uint32_t       rambo_off = bus_rambo_bank(pins) << 14;

    /* From here on Core 1 belongs to the bus: no tick, no interrupts. */
    bus_int_wdt_disable();
    portDISABLE_INTERRUPTS();

    /* The VERA is configured (setup()): release the Atari, the bus goes live. */
    GPIO.out1_w1ts.val = (1UL << (PIN_ARESET - 32));

    /* Align to the end of a PHI2-high phase. */
    wait_phi2_high();
    wait_phi2_low();

    for (;;)
    {
        /* ---- PHI1: PHI2 has just fallen ----------------------------------- */
        const uint32_t t_sample = cpu_ll_get_cycle_count() + settle;

        /* 1-2. Close the previous cycle (releasing an idle bus is harmless). */
        bus_release();
        cpu_ll_write_dedic_gpio_mask(DEDIC_OUT_CTRL_MASK, DEDIC_OUT_CTRL_IDLE);


        /*
         * 3-6. Critical path, in assembly so that every operand stays in a
         * register and the constants are loaded before the wait:
         *   3. wait until address and R/W are guaranteed stable (one check
         *      every 5 cycles, leaving the loop without a taken branch);
         *   4. one dedicated read (PHI2, R/W, A10-A15) and one APB read (A0-A9;
         *      no memw: the last store, the bus release, left >= 150 ns ago),
         *      page = A15..A8, c = ctrl_cur[page]; on the CT_SLOW page
         *      ($D1xx / $D5xx) c = slow_cur[A0-A7] instead;
         *   5. control signals for this cycle, around the PHI2 rising edge
         *      (the VERA strobes ~mRE / ~mWE are gated by PHI2 in hardware);
         *   6. read cycle on a CT_DRIVE page: drive the byte now (bank offset
         *      only for RAMbo pages, without a branch).  U19 passes it to the
         *      Atari only while PHI2 is high.  The slow page never drives.
         */
        uint32_t c, page, din, lo, off8, t1, t2;
        asm volatile(
            "   movi   %[c], 0xFC                \n\t"   /* A10-A15 in din */
            "   movi   %[t2], 7                  \n\t"   /* DEDIC_OUT_CTRL_MASK */
            "1: rsr.ccount %[t1]                 \n\t"
            "   sub    %[t1], %[t1], %[ts]       \n\t"
            "   bltz   %[t1], 1b                 \n\t"
            "   ee.get_gpio_in %[din]            \n\t"
            "   l32i   %[lo], %[gpio], %[o_in]   \n\t"   /* GPIO.in */
            "   and    %[page], %[din], %[c]     \n\t"
            "   extui  %[t1], %[lo], 20, 2       \n\t"   /* A8, A9 */
            "   or     %[page], %[page], %[t1]   \n\t"
            "   add    %[t1], %[ctrl], %[page]   \n\t"
            "   l8ui   %[c], %[t1], 0            \n\t"
            "   extui  %[off], %[lo], 12, 8      \n\t"   /* A0-A7 */
            "   bbsi   %[c], 3, 5f               \n\t"   /* CT_SLOW */
            "3: ee.wr_mask_gpio_out %[c], %[t2]  \n\t"
            "   bbci   %[din], 1, 4f             \n\t"   /* write cycle */
            "   bbci   %[c], 4, 4f               \n\t"   /* not CT_DRIVE */
            "   addx4  %[t1], %[page], %[rdb]    \n\t"
            "   l32i   %[t1], %[t1], 0           \n\t"   /* bus_rd_base[page] */
            "   extui  %[t2], %[c], 5, 1         \n\t"   /* CT_RAMBO */
            "   neg    %[t2], %[t2]              \n\t"
            "   and    %[t2], %[t2], %[roff]     \n\t"
            "   add    %[t1], %[t1], %[off]      \n\t"
            "   add    %[t1], %[t1], %[t2]       \n\t"
            "   l8ui   %[t1], %[t1], 0           \n\t"
            "   movi   %[t2], %[m_dbus]          \n\t"
            "   slli   %[t1], %[t1], %[sh]       \n\t"
            "   s32i   %[t1], %[gpio], %[o_out]  \n\t"   /* GPIO.out */
            "   s32i   %[t2], %[gpio], %[o_en]   \n\t"   /* GPIO.enable_w1ts */
            "4: j      6f                        \n\t"
            "5: add    %[t1], %[slow], %[off]    \n\t"   /* CT_SLOW page */
            "   l8ui   %[c], %[t1], 0            \n\t"
            "   j      3b                        \n\t"
            "6:                                  \n\t"
            : [c] "=&r"(c), [page] "=&r"(page), [din] "=&r"(din), [lo] "=&r"(lo),
              [off] "=&r"(off8), [t1] "=&r"(t1), [t2] "=&r"(t2)
            : [ts] "r"(t_sample), [gpio] "r"(&GPIO), [ctrl] "r"(ctrl_cur),
              [slow] "r"(slow_cur), [rdb] "r"(bus_rd_base), [roff] "r"(rambo_off),
              [m_dbus] "n"(DBUS_MASK), [sh] "n"(DBUS_SHIFT),
              [o_in] "n"(offsetof(gpio_dev_t, in)),
              [o_out] "n"(offsetof(gpio_dev_t, out)),
              [o_en] "n"(offsetof(gpio_dev_t, enable_w1ts))
            : "memory");

        if (din & DEDIC_IN_RW)
        {
            wait_phi2_high();
            log_flush_deferred();
#if VERA_TRACE_REGS
            if ((c & CT_SLOW) && is_pbi && !(c & DEDIC_OUT_DEVSELN))
            {
                /* VERA drives the bus: sample it just before PHI2 falls. */
                uint32_t last = lo, now;
                while ((now = GPIO.in) & (1UL << PIN_PHI2))
                    last = now;
                uint8_t fl = 0x00;  /* read */
                if (!(c & DEDIC_OUT_MPD))    fl |= 0x04;
                if (!(c & DEDIC_OUT_EXTSEL)) fl |= 0x08;
                log_send(EVT_REG, (uint8_t)off8, decode_data(last), fl);
            }
#endif
            wait_phi2_low();
        }
        else
        {
            /* 6b. Write: sample the data at a fixed time after PHI2 rises
             *     (valid from t_MDS = 100 ns to 30 ns after PHI2 falls). */
            wait_phi2_high();
            const uint32_t t_wr = cpu_ll_get_cycle_count() + wr_sample;
            while ((int32_t)(cpu_ll_get_cycle_count() - t_wr) < 0);
            const uint32_t sample = GPIO.in;

            /* 7. Side effects, in the rest of PHI2 (and the start of the next
             *    PHI1 if they need it): this cycle's control signals are
             *    already out, the new state applies from the next cycle. */
            if (c & (CT_WR_MEM | CT_WR_REG))
            {
                const uint32_t wr_addr = (page << 8) | off8;    /* A15..A0 */
                const uint32_t wr_off  = off8;
                const uint8_t  wr_data = decode_data(sample);

                if (c & CT_WR_MEM)
                {
                    const uint32_t bank_off = rambo_off & (0u - ((c / CT_RAMBO) & 1u));
                    ((uint8_t *)bus_rd_base[page])[bank_off + wr_off] = wr_data;
                }

                if (c & CT_WR_REG)
                {
                    if (wr_addr == (is_pbi ? 0xD1FFu : 0xD5FFu))
                    {
                        /* PBI: each device latches only its own data bit.
                         * CCTL: write $80 selects, $00 deselects. */
                        const uint8_t new_sel = wr_data >> 7;   /* PBI_DEV_ID = bit 7 */
                        if (new_sel != card.selected)
                        {
                            card.selected = new_sel;
                            ctrl_cur = card_ctrl();
                            slow_cur = bus_slow[new_sel];
                            log_defer_latch(wr_off, wr_data, c);
                        }
                    }
                    /*
                     * RAMbo PIA snoop: PBCTL ($D303) selects whether $D301 is
                     * the output register or the DDR; both decide the pins.  The
                     * PIA decodes A0-A1 only: $D300-$D3FF mirrors its registers.
                     *
                     * Typical OS init sequence:
                     *   write 0x00 → $D303  (PBCTL bit2=0 → DDR selected at $D301)
                     *   write 0xFF → $D301  (all PORTB pins outputs — DDR write)
                     *   write 0x34 → $D303  (PBCTL bit2=1 → output register at $D301)
                     *   write 0xNN → $D301  (memory configuration)
                     */
                    else if ((wr_addr & 0xFF03u) == 0xD303u)
                    {
                        card.pbctl = wr_data;
                    }
                    else if ((wr_addr & 0xFF03u) == 0xD301u)
                    {
                        if (card.pbctl & 0x04u)
                            card.orb = wr_data;
                        else
                            card.ddrb = wr_data;
                        const uint8_t pb = bus_portb_pins(card.orb, card.ddrb);
                        card.pstate = bus_pb_state[pb];
                        rambo_off   = bus_rambo_bank(pb) << 14;
                        ctrl_cur    = card_ctrl();
                    }
#if VERA_TRACE_REGS
                    else if (is_pbi && (wr_addr >> 8) == 0xD1u && !(c & DEDIC_OUT_DEVSELN))
                    {
                        uint8_t fl = 0x01u;  /* write */
                        if (!(c & DEDIC_OUT_MPD))    fl |= 0x04u;
                        if (!(c & DEDIC_OUT_EXTSEL)) fl |= 0x08u;
                        log_send(EVT_REG, (uint8_t)wr_off, wr_data, fl);
                    }
#endif
                }
            }
            wait_phi2_low();
        }
    }
}

/* ---------------------------------------------------------------------------
 * VERA register name lookup — Core 0 only (LogTask), not IRAM-resident.
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
 * LogTask -- Core 0: owns Serial (its UART interrupt is allocated on the
 * core that calls Serial.begin) and drains log_ring filled by the bus loop.
 * =========================================================================== */
static void log_drain(void)
{
    /* Shadow of VERA_CTRL DCSEL bits [2:1]; updated on every write to $D105. */
    static uint8_t  dcsel = 0;
    static uint32_t dropped_seen = 0;

    uint32_t t = log_tail;
    while (t != __atomic_load_n(&log_head, __ATOMIC_ACQUIRE))
    {
        LogEvt evt = log_ring[t & (LOG_RING_SIZE - 1u)];
        __atomic_store_n(&log_tail, ++t, __ATOMIC_RELEASE);

        uint32_t now = millis();
        uint32_t sec = now / 1000u;
        uint32_t ms  = now % 1000u;
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

static void LogTask(void *arg)
{
    Serial.begin(115200, SERIAL_8N1, PIN_UART_RX, PIN_UART_TX);
    log_ready = true;
    for (;;)
    {
        log_drain();
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

/* ===========================================================================
 * System Setup -- runs on Core 1 (ARDUINO_RUNNING_CORE) until MonitorTask
 * takes the core over.
 * =========================================================================== */
void setup(void)
{
    /* Reclaim JTAG pins (GPIO 39-42) for regular GPIO usage */
    gpio_reset_pin((gpio_num_t)PIN_CDONE);     /* GPIO 39 */
    gpio_reset_pin((gpio_num_t)PIN_DEV_SEL_N); /* GPIO 40 */
    gpio_reset_pin((gpio_num_t)PIN_EXTSEL_N);  /* GPIO 41 */
    gpio_reset_pin((gpio_num_t)PIN_MPD);       /* GPIO 42 */

    /* Hold Atari and VERA in reset immediately — the VERA is released below,
     * the Atari only by MonitorTask once the bus loop is ready. */
    pinMode(PIN_ARESET, OUTPUT);
    digitalWrite(PIN_ARESET, LOW);
    pinMode(PIN_CRESET, OUTPUT);
    digitalWrite(PIN_CRESET, LOW);

    /* Serial and the log printer live on Core 0. */
    xTaskCreatePinnedToCore(LogTask, "BusLog", 4096, NULL, 1, NULL, 0);
    while (!log_ready)
        delay(1);

    if (VERA_BOARD_IS_PBI)
    {
        Serial.println("[VeraX16] Mode: PBI ($D1xx / $D6xx / $D8xx, latch $D1FF)");
        memset(ram_pbi, 0x00, sizeof(ram_pbi));
    }
    else
    {
        Serial.println("[VeraX16] Mode: CCTL ($D5xx, latch $D5FF)");
    }
    Serial.println("[VeraX16] RAMbo 320K: 256 KB at $4000-$7FFF (PORTB bits 2,3,5,6; bit 4 = enable)");

    bus_build_tables(bus_ctrl, bus_rd_base, VERA_BOARD_IS_PBI,
                     vera_pbi_handler, ram_pbi, extended_rambo_256k);
    bus_build_slow_tables(bus_slow, bus_ctrl, VERA_BOARD_IS_PBI);
    for (uint32_t pb = 0; pb < 256u; pb++)
        bus_pb_state[pb] = bus_portb_state((uint8_t)pb);

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

    /*
     * VERA bring-up:
     *  1. Release the VERA FPGA from reset → it loads the bitstream from SPI flash.
     *  2. Poll CDONE until HIGH (ICE40UP5K typically < 100 ms).
     *  3. Wait for the VERA power-on reset; MonitorTask then releases the Atari.
     */
    digitalWrite(PIN_CRESET, HIGH);
    Serial.println("[VeraX16] VERA: released from reset, waiting CDONE...");
    uint32_t elapsed_ms = 0u;
    while (!digitalRead(PIN_CDONE) && elapsed_ms < 5000u)
    {
        delay(1);
        elapsed_ms++;
    }
    if (elapsed_ms < 5000u)
        Serial.printf("[VeraX16] VERA: CDONE OK (~%lu ms)\n", (unsigned long)elapsed_ms);
    else
        Serial.println("[VeraX16] VERA: CDONE timeout — FPGA not configured!");
    /* CDONE only says the bitstream is loaded: the VERA still runs its
     * internal power-on reset (128 clk25 cycles + reset synchroniser, a few
     * us).  Give it a comfortable margin before the 6502 can touch the bus. */
    delayMicroseconds(VERA_POST_CDONE_DELAY_US);

    Serial.printf("[VeraX16] Bus loop on Core 1 (settle %u ns, write sample %u ns); "
                  "ATARI leaves reset\n", BUS_ADDR_SETTLE_NS, BUS_WR_SAMPLE_NS);
    Serial.flush();

    /* MonitorTask takes Core 1 over (highest priority, never yields): this
     * call does not return. */
    xTaskCreatePinnedToCore(MonitorTask, "BusMonitor", 4096, NULL,
                            configMAX_PRIORITIES - 1, NULL, 1);
    Serial.println("[VeraX16] FATAL: cannot start the bus loop");
}

void loop(void)
{
    /* Not reached: MonitorTask owns Core 1. */
    vTaskDelay(portMAX_DELAY);
}
