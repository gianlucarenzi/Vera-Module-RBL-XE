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
#define PIN_EXTSEL_N    41   /* Atari MMU Disable Output (Pin 46, active LOW) */
#define PIN_MPD         42   /* Math Pack Disable — disables internal Atari Math Pack ROM $D800-$DFFF (Pin 48, active LOW) */
#define PIN_A14         47   /* Address Bit 14 (Pin 53) */
#define PIN_A15         48   /* Address Bit 15 (Pin 54) */

/* Serial Debug (UART0 default pins on ESP32-S3 QFN56) */
#define PIN_UART_TX     43   /* UART0 TX (Pin 49) */
#define PIN_UART_RX     44   /* UART0 RX (Pin 50) */

/* RAMbo hardware enable: external 10K pull-up = RAMbo present, pull-down = no RAMbo */
#define PIN_RAMBO_EN     3   /* GPIO 3 (Pin 8) — read once at boot */

/* ===========================================================================
 * Helper Macros & Inline Functions
 * =========================================================================== */

/*
 * Decode full 16-bit address from GPIO input snapshots.
 * A0-A9  : Bank 0 bits 12-21.
 * A10-A13: Bank 1 bits 3, 4, 1, 2  (GPIO 35, 36, 33, 34 → offsets from 32).
 * A14-A15: Bank 1 bits 15, 16       (GPIO 47, 48).
 * All bits captured in the same g_lo / g_hi APB reads — no extra reads needed.
 */
static inline uint16_t IRAM_ATTR decode_addr(uint32_t lo, uint32_t hi)
{
    uint16_t a = (uint16_t)((lo >> 12) & 0x3FFu);              /* A0-A9  */
    a |= (uint16_t)(((hi >> (PIN_A10 - 32)) & 1u) << 10);      /* A10    */
    a |= (uint16_t)(((hi >> (PIN_A11 - 32)) & 1u) << 11);      /* A11    */
    a |= (uint16_t)(((hi >> (PIN_A12 - 32)) & 1u) << 12);      /* A12    */
    a |= (uint16_t)(((hi >> (PIN_A13 - 32)) & 1u) << 13);      /* A13    */
    a |= (uint16_t)(((hi >> (PIN_A14 - 32)) & 1u) << 14);      /* A14    */
    a |= (uint16_t)(((hi >> (PIN_A15 - 32)) & 1u) << 15);      /* A15    */
    return a;
}

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
#define DEDIC_OUT_DEVSELN    (1u << 0)
#define DEDIC_OUT_EXTSEL     (1u << 1)
#define DEDIC_OUT_MPD        (1u << 2)
#define DEDIC_OUT_CTRL_MASK  0x07u
#define DEDIC_OUT_CTRL_IDLE  0x07u   /* all HIGH = deasserted (active LOW) */

/* ===========================================================================
 * Compile-time Configuration
 * =========================================================================== */

#define PBI_DEV_ID        0x80u  /* PBI Device ID bit 7 */
#define VERA_BOARD_IS_PBI 0x01u  /* Default as PBI */
//#define VERA_BOARD_IS_PBI 0x00u  /* if CCTL */

/* ===========================================================================
 * Log Queue (Core 1 → Core 0)
 * =========================================================================== */
#define LOG_QUEUE_SIZE 64

#define EVT_LATCH 0   /* VCS/CCTL latch state change */
#define EVT_REG   1   /* VERA register ($D1xx) access */

/* flags: bit0=write(1)/read(0)  bit1=latch state (EVT_LATCH)  bit2=MPD asserted  bit3=EXTSEL asserted */
typedef struct
{
    uint64_t ts_us;   /* microseconds since boot (esp_timer_get_time()) */
    uint8_t  type;
    uint8_t  offset;  /* register offset: 0x00-0xFF within address range */
    uint8_t  data;
    uint8_t  flags;
} LogEvt;

/* ===========================================================================
 * Shared Data
 * =========================================================================== */

static IRAM_ATTR uint32_t lut_drive[256];   /* Lookup table for fast data bus drive */
static IRAM_ATTR uint8_t  ram_pbi[512];     /* 512B RAM at $D600-$D7FF */

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
static IRAM_ATTR uint8_t PORTB = 0xFFu;

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
static IRAM_ATTR uint8_t PBCTL = 0x00u;

static QueueHandle_t log_queue;

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

/* Enqueue a log event without blocking (drops if queue full). */
static inline void IRAM_ATTR log_send(uint8_t type, uint8_t offset,
                                      uint8_t data, uint8_t flags)
{
    LogEvt evt = {(uint64_t)esp_timer_get_time(), type, offset, data, flags};
    xQueueSend(log_queue, &evt, 0);
}

/* ===========================================================================
 * MonitorTask -- Core 1, IRAM-resident
 * =========================================================================== */
static void IRAM_ATTR MonitorTask(void *arg)
{
    /*
     * Dedicated GPIO bundles — MUST be created on Core 1 (the executing core).
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

    bool selected = false;
    bus_release();

    bool vera_board_is_pbi = VERA_BOARD_IS_PBI;

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

        /* 2. Decode address and R/W */
        bool     is_read = (g_lo & (1UL << PIN_RW)) != 0;
        uint16_t addr    = decode_addr(g_lo, g_hi);
        uint8_t  off8    = (uint8_t)(addr & 0xFFu);

        /* PBI address ranges */
        bool is_d1xx = (addr >= 0xD100u && addr <= 0xD1FFu);
        bool is_d6xx = (addr >= 0xD600u && addr <= 0xD7FFu);
        bool is_d8xx = (addr >= 0xD800u && addr <= 0xDFFFu);

        bool is_vera_range = is_d1xx && (off8 <= 0x1Fu);
        bool is_vcs_latch  = is_d1xx && (off8 == 0xFFu);

        /* CCTL address range ($D500-$D5FF, latch at $D5FF) */
        bool is_d5xx       = (addr >= 0xD500u && addr <= 0xD5FFu);
        bool is_cctl_range = is_d5xx && (off8 != 0xFFu);
        bool is_cctl_latch = is_d5xx && (off8 == 0xFFu);

        /* RAMbo: 256 KB bank-switched at $4000-$7FFF, independent of PBI/CCTL mode.
         * Active when PIN_RAMBO_EN=1 (hardware present) and bit 4 of PORTB ($D301) = 0.
         * Bank (0-15) from PORTB bits 6,5,3,2: bank = ((portb>>2)&0x03)|((portb>>3)&0x0C) */
        bool is_rambo_window = (addr >= 0x4000u && addr <= 0x7FFFu);
        bool rambo_active    = rambo_hw_enabled && ((PORTB & 0x10u) == 0u);

        /* 3. Assert control signals atomically in one ee.wr_mask_gpio_out instruction.
         *    Previously: up to 3 separate APB writes (~18 cycles total).
         *    Now: 1 TIE instruction (~1 cycle). */
        uint8_t ctrl = DEDIC_OUT_CTRL_IDLE;
        {
            if (vera_board_is_pbi)
            {
                if (selected)
                {
                    /* Disable MMU for D1xx (except VCS latch) and D6xx */
                    if ((is_d1xx && !is_vcs_latch) || is_d6xx)
                        ctrl &= ~DEDIC_OUT_EXTSEL;

                    /* Disable Math Pack for ROM area */
                    if (is_d8xx)
                        ctrl &= ~DEDIC_OUT_MPD;

                    /* Chip-select VERA for register accesses */
                    if (is_vera_range)
                        ctrl &= ~DEDIC_OUT_DEVSELN;
                }
            }
            else
            {
                /* CCTL: assert DEV_SEL_N only when selected and in non-latch range */
                if (selected && is_cctl_range)
                    ctrl &= ~DEDIC_OUT_DEVSELN;
            }
            /* RAMbo: suppress Atari internal RAM in the bank window */
            if (rambo_active && is_rambo_window)
                ctrl &= ~DEDIC_OUT_EXTSEL;

            cpu_ll_write_dedic_gpio_mask(DEDIC_OUT_CTRL_MASK, ctrl);
        }

        /* 4. Cycle Handling */
        if (is_read)
        {
            if (vera_board_is_pbi && selected)
            {
                if (is_d8xx)
                {
                    bus_drive(pbi_driver[addr & 0x7FFu]);
                }
                else if (is_d6xx)
                {
                    bus_drive(ram_pbi[addr & 0x1FFu]);
                }
                else if (is_vera_range)
                {
                    /* VERA drives the bus; re-read to capture what it put on the line. */
                    uint8_t reg_data = decode_data(GPIO.in);
                    uint8_t fl = 0x00;  /* read */
                    if (!(ctrl & DEDIC_OUT_MPD))    fl |= 0x04;
                    if (!(ctrl & DEDIC_OUT_EXTSEL)) fl |= 0x08;
                    log_send(EVT_REG, off8, reg_data, fl);
                }
            }
            if (rambo_active && is_rambo_window)
            {
                uint8_t bank = ((PORTB >> 2) & 0x03u) | ((PORTB >> 3) & 0x0Cu);
                bus_drive(extended_rambo_256k[((uint32_t)bank << 14) | (addr & 0x3FFFu)]);
            }
            /* CCTL mode: no data bus driving */
        }
        else
        {
            /* Write cycle: re-read GPIO.in to ensure data has settled well after
             * the PHI2 rising edge (6502 data is stable during all of PHI2 high). */
            uint8_t data = decode_data(GPIO.in);

            if (vera_board_is_pbi)
            {
                if (is_vcs_latch)
                {
                    bool new_sel = (data == PBI_DEV_ID);
                    if (new_sel != selected)
                    {
                        selected = new_sel;
                        uint8_t fl = 0x01u | (selected ? 0x02u : 0x00u);
                        if (!(ctrl & DEDIC_OUT_MPD))    fl |= 0x04u;
                        if (!(ctrl & DEDIC_OUT_EXTSEL)) fl |= 0x08u;
                        log_send(EVT_LATCH, off8, data, fl);
                    }
                }
                else if (is_vera_range)
                {
                    uint8_t fl = 0x01u;  /* write */
                    if (!(ctrl & DEDIC_OUT_MPD))    fl |= 0x04u;
                    if (!(ctrl & DEDIC_OUT_EXTSEL)) fl |= 0x08u;
                    log_send(EVT_REG, off8, data, fl);
                }
            }
            else
            {
                if (is_cctl_latch)
                {
                    bool new_sel = (data == PBI_DEV_ID);
                    if (new_sel != selected)
                    {
                        selected = new_sel;
                        uint8_t fl = 0x01u | (selected ? 0x02u : 0x00u);
                        if (!(ctrl & DEDIC_OUT_MPD))    fl |= 0x04u;
                        if (!(ctrl & DEDIC_OUT_EXTSEL)) fl |= 0x08u;
                        log_send(EVT_LATCH, off8, data, fl);
                    }
                }
            }
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
            if (addr == 0xD303u)
                PBCTL = data;
            /* Update bank-selection register only when PBCTL bit 2 (CRB-2) = 1. */
            if (addr == 0xD301u && (PBCTL & 0x04u))
                PORTB = data;
            /* RAMbo write: store data into the active bank */
            if (rambo_active && is_rambo_window)
            {
                uint8_t bank = ((PORTB >> 2) & 0x03u) | ((PORTB >> 3) & 0x0Cu);
                extended_rambo_256k[((uint32_t)bank << 14) | (addr & 0x3FFFu)] = data;
            }
        }

        /* 5. Wait for PHI2 falling edge (1 cycle/iteration via TIE instruction). */
        while (cpu_ll_read_dedic_gpio_in() & DEDIC_IN_PHI2);

        /* 6. End of cycle: release data bus, deassert all controls in 1 instruction. */
        bus_release();
        cpu_ll_write_dedic_gpio_mask(DEDIC_OUT_CTRL_MASK, DEDIC_OUT_CTRL_IDLE);
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

    log_queue = xQueueCreate(LOG_QUEUE_SIZE, sizeof(LogEvt));

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

    /* Start High-Priority Monitor Task on Core 1 (dedicated bus management) */
    xTaskCreatePinnedToCore(
        MonitorTask, "BusMonitor",
        4096, NULL,
        configMAX_PRIORITIES - 1,
        NULL, 1
    );

    Serial.println("[VeraX16] Bus Monitor active on Core 1");
}

/* ---------------------------------------------------------------------------
 * VERA register name lookup — Core 0 only, not IRAM-resident.
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
 * Main Loop -- Core 0: drain log queue and print
 * =========================================================================== */
void loop(void)
{
    /* Shadow of VERA_CTRL DCSEL bits [2:1]; updated on every write to $D105. */
    static uint8_t dcsel = 0;

    LogEvt evt;
    while (xQueueReceive(log_queue, &evt, pdMS_TO_TICKS(10)) == pdTRUE)
    {
        uint32_t sec = (uint32_t)(evt.ts_us / 1000000ULL);
        uint32_t us  = (uint32_t)(evt.ts_us % 1000000ULL);
        if (evt.type == EVT_LATCH)
        {
            bool enabled = (evt.flags & 0x02) != 0;
            Serial.printf("[%5lu.%06lu] [VCS ] Latch %s ($%02X written to %s) MPD=%s EXTSEL=%s\n",
                          sec, us,
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

            Serial.printf("[%5lu.%06lu] [D1%02X - %-20s] %c $%02X  MPD=%s EXTSEL=%s\n",
                          sec, us,
                          evt.offset,
                          vera_reg_name(evt.offset, dcsel),
                          (evt.flags & 0x01) ? 'W' : 'R',
                          evt.data,
                          (evt.flags & 0x04) ? "LO" : "HI",
                          (evt.flags & 0x08) ? "LO" : "HI");
        }
    }
}
