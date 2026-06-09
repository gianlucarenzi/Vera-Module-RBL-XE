/**
 * esp32-main.cpp  --  Atari PBI / CCTL  VeraX16 Bus Controller
 *                     Target MCU: ESP32-S3FN8
 *
 * Compile-time selection via BUS_MODE:
 *
 *   BUS_MODE 0  (default) -- PBI mode
 *     Connector : XL/XE ECI
 *     Signals   : D1XX_N (GPIO 18), ROM_SEL_N (GPIO 21), EXTSEL_N (GPIO 0)
 *     Selection : D1FF one-hot write -- 0x80 selects this device
 *     ROM       : 2 KB image served from IRAM at $D800-$DFFF
 *     VERA regs : $D100-$D11F
 *
 *   BUS_MODE 1             -- CCTL mode
 *     Connector : 400/800 or XL/XE cartridge port (13-bit address: A0-A12)
 *     Signals   : CCTL_N on GPIO 18 (same pin as D1XX_N -- different wire)
 *     Selection : always active when CCTL_N asserted ($D500-$D5FF)
 *     ROM       : none -- Atari software (VERAX16.BIN) loads the driver
 *     VERA regs : $D500-$D51F
 *     Address   : only A0-A4 decoded (A13-A15 absent on cartridge port;
 *                 CCTL is fully pre-decoded by the Atari motherboard)
 *
 * GPIO 18 dual role:
 *   BUS_MODE 0 -> wire to D1XX_N (external address-decoder output)
 *   BUS_MODE 1 -> wire to Atari CCTL (cartridge connector pin)
 *
 * GPIO 0 (EXTSEL_N) and GPIO 21 (ROM_SEL_N) are unused in CCTL mode.
 * GPIO 14, 15, 16 (A8-A10) are unused in CCTL mode (not on cart port).
 *
 * Architecture:
 *   Core 1: MonitorTask -- IRAM, no blocking calls, ~54-cycle decode path.
 *   Core 0: loop()      -- serial debug via FreeRTOS queue.
 *
 * Timing (NTSC 6502 @ 1.7897 MHz):
 *   PHI2 high period  ~= 279 ns  (67 cycles @240 MHz LX7)
 *   ROM drive deadline ~= 179 ns  (43 cycles)
 *   Full decode+drive  ~= 54 cycles -- every path must stay in IRAM.
 *
 * ESP32-S3 PIN WARNINGS:
 *   GPIO  0 (EXTSEL_N)  : strapping -- LOW at power-on enters download mode.
 *                          Add 10 kohm pull-up to 3.3 V.
 *   GPIO  3             : strapping (JTAG source) -- NO internal pull resistor.
 *                          Must be externally pulled (tie to GND or 3.3V via
 *                          10 kohm). Do NOT leave floating.
 *   GPIO 26-32          : connected to in-package Quad SPI flash (FN8).
 *                          NEVER connect externally.
 *   GPIO 19, 20         : USB D-/D+ (USB CDC disabled via build flag).
 *   GPIO 45             : strapping (VDD_SPI select) -- weak pull-down to GND,
 *                          selects VDD_SPI = 3.3 V (correct for FN8).
 *   GPIO 46             : strapping (boot mode) -- weak pull-down, leave open.
 *
 * Pin mapping (same for both modes):
 *   D0-D7         : GPIO  4,  5,  6,  7,  8,  9, 10, 11  (bank-0, bits 4-11)
 *   A0-A5         : GPIO 33, 34, 35, 36, 37, 38           (bank-1, bits 1-6)
 *   A6-A10        : GPIO 12, 13, 14, 15, 16               (PBI mode only)
 *   PHI2          : GPIO  2
 *   R/W_          : GPIO 17
 *   D1XX_N/CCTL_N : GPIO 18
 *   ROM_SEL_N     : GPIO 21  (PBI mode only)
 *   EXTSEL_N      : GPIO  0  (PBI mode only)
 *   DEV_SEL_N     : GPIO  1  (VERA CS, both modes)
 *   UART TX       : GPIO 43  (S3 UART0 hardware pin)
 *   UART RX       : GPIO 44  (S3 UART0 hardware pin, disabled in firmware)
 */

#ifndef BUS_MODE
#define BUS_MODE 0   /* 0 = PBI,  1 = CCTL */
#endif

#include <Arduino.h>
#include <driver/gpio.h>
#include <soc/gpio_struct.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#if BUS_MODE == 0
#include "pbi-driver.h"
#endif

// ---------------------------------------------------------------------------
// Pin assignments  (ESP32-S3FN8)
// ---------------------------------------------------------------------------
#define PIN_PHI2        2
#define PIN_RW          17
#define PIN_BUS_SEL_N   18   /* D1XX_N (PBI) or CCTL_N (CCTL) -- same GPIO */
#define PIN_DEV_SEL_N   1    /* VERA chip CS, active LOW */

/* PBI-mode-only */
#define PIN_ROM_SEL_N   21   /* 74HC138 Y7 -> $D800-$DFFF, active LOW */
#define PIN_EXTSEL_N    0    /* Atari EXTSEL: disables FP ROM, active LOW */

/* Data bus: GPIO4-GPIO11, all in bank 0 — DBUS_MASK = 0x00000FF0 */
#define DBUS_MASK  ((1UL<<4)|(1UL<<5)|(1UL<<6)|(1UL<<7)|\
                    (1UL<<8)|(1UL<<9)|(1UL<<10)|(1UL<<11))

/* PBI device configuration */
#define DEVICE_MASK    0x80u   /* one-hot: PBI slot 7 */
#define VERA_REG_MAX   0x1Fu   /* VERA registers offset 0x00-0x1F */
#define INT_REG_LO     0x20u   /* ESP32 internal regs $D120-$D1FE (PBI only) */
#define INT_REG_HI     0xFEu

// ---------------------------------------------------------------------------
// IRAM-resident data (PBI mode only -- save IRAM in CCTL mode)
// ---------------------------------------------------------------------------
#if BUS_MODE == 0
static IRAM_ATTR uint32_t lut_drive[256];   /* byte value -> GPIO bitmask */
static IRAM_ATTR uint8_t  int_regs[256];    /* ESP32 internal register file */
#endif

static QueueHandle_t eventQueue;

// ---------------------------------------------------------------------------
// build_drive_lut()  (PBI mode only)
// ---------------------------------------------------------------------------
#if BUS_MODE == 0
static void build_drive_lut(void)
{
    static const uint8_t pin[8] = {4, 5, 6, 7, 8, 9, 10, 11};
    for (int v = 0; v < 256; v++) {
        uint32_t m = 0;
        for (int b = 0; b < 8; b++)
            if (v & (1 << b)) m |= (1UL << pin[b]);
        lut_drive[v] = m;
    }
}
#endif

// ---------------------------------------------------------------------------
// decode_addr()  -- PBI mode, A0-A10, ~12 Xtensa LX7 cycles
// ---------------------------------------------------------------------------
#if BUS_MODE == 0
static inline uint16_t IRAM_ATTR decode_addr(uint32_t lo, uint32_t hi)
{
    uint16_t a;
    a  = (uint16_t)((hi >> 1) & 0x3Fu);            /* A0-A5  GPIO33-38 (in1 bits 1-6) */
    a |= (uint16_t)(((lo >> 12) & 1u) << 6);       /* A6     GPIO 12                  */
    a |= (uint16_t)(((lo >> 13) & 1u) << 7);       /* A7     GPIO 13                  */
    a |= (uint16_t)(((lo >> 14) & 1u) << 8);       /* A8     GPIO 14                  */
    a |= (uint16_t)(((lo >> 15) & 1u) << 9);       /* A9     GPIO 15                  */
    a |= (uint16_t)(((lo >> 16) & 1u) << 10);      /* A10    GPIO 16                  */
    return a;
}
#endif

// ---------------------------------------------------------------------------
// decode_offset_cctl()  -- CCTL mode, A0-A4 only, ~2 Xtensa LX7 cycles
//
// The Atari CCTL signal is fully pre-decoded: when it is LOW the CPU is
// addressing $D500-$D5FF.  Only the low 5 bits (A0-A4) are needed to
// select one of the 32 VERA registers.  A13-A15 are absent on the 400/800
// cartridge port, but are not required here.
// ---------------------------------------------------------------------------
#if BUS_MODE == 1
static inline uint8_t IRAM_ATTR decode_offset_cctl(uint32_t hi)
{
    return (uint8_t)((hi >> 1) & 0x1Fu);  /* GPIO_IN1 bits 1-5 = GPIO33-37 = A0-A4 */
}
#endif

// ---------------------------------------------------------------------------
// decode_data()  (PBI mode only)
// ---------------------------------------------------------------------------
#if BUS_MODE == 0
static inline uint8_t IRAM_ATTR decode_data(uint32_t lo)
{
    return (uint8_t)((lo >> 4) & 0xFFu);  /* D0-D7  GPIO4-11 (bank-0 bits 4-11) */
}
#endif

// ---------------------------------------------------------------------------
// bus_drive() / bus_release()
// bus_drive() is PBI-only; bus_release() is needed in both modes.
// ---------------------------------------------------------------------------
#if BUS_MODE == 0
static inline void IRAM_ATTR bus_drive(uint8_t val)
{
    uint32_t mask = lut_drive[val];
    GPIO.out_w1tc = DBUS_MASK & ~mask;   /* clear bits that must be 0 */
    GPIO.out_w1ts = mask;                /* set   bits that must be 1 */
    GPIO.enable_w1ts = DBUS_MASK;        /* tristate -> driven         */
}
#endif

static inline void IRAM_ATTR bus_release(void)
{
    GPIO.enable_w1tc = DBUS_MASK;
}

// ============================================================================
// MonitorTask -- Core 1, IRAM
// ============================================================================
static void IRAM_ATTR MonitorTask(void * /*arg*/)
{
#if BUS_MODE == 0
    /* ------------------------------------------------------------------ */
    /* PBI mode                                                            */
    /* ------------------------------------------------------------------ */
    bool selected = false;

    GPIO.out_w1ts = (1UL << PIN_EXTSEL_N) | (1UL << PIN_DEV_SEL_N);
    bus_release();

    for (;;)
    {
        /* 1. Spin-wait for PHI2 rising edge; capture snapshot */
        uint32_t g_lo;
        while (!((g_lo = GPIO.in) & (1UL << PIN_PHI2)));

        /* 2. Decode control signals */
        bool is_read  = (g_lo & (1UL << PIN_RW))          != 0;
        bool is_d1xx  = (g_lo & (1UL << PIN_BUS_SEL_N))   == 0;
        bool is_rom   = (g_lo & (1UL << PIN_ROM_SEL_N))    == 0;

        uint32_t g_hi = GPIO.in1.val;
        uint16_t addr = decode_addr(g_lo, g_hi);
        uint8_t  off8 = (uint8_t)(addr & 0xFFu);

        bool is_d1ff       = is_d1xx && (off8 == 0xFFu);
        bool is_vera_range = is_d1xx && (off8 <= VERA_REG_MAX);
        bool is_int_range  = is_d1xx && (off8 >= INT_REG_LO) && (off8 <= INT_REG_HI);

        /* 3. Assert VERA CS when selected and in VERA register range */
        if (selected && is_vera_range)
            GPIO.out_w1tc = (1UL << PIN_DEV_SEL_N);

        /* 4. READ */
        if (is_read) {
            if (selected) {
                if (is_rom)
                    bus_drive(pbi_driver[addr & 0x7FFu]);
                else if (is_int_range)
                    bus_drive(int_regs[off8]);
                /* is_vera_range: VERA chip drives bus autonomously */
            }
        }
        /* 5. WRITE -- re-read GPIO after ~200 ns for stable write data */
        else {
            uint8_t data = decode_data(GPIO.in);

            if (is_d1ff) {
                bool new_sel = (data == DEVICE_MASK);
                if (new_sel != selected) {
                    selected = new_sel;
                    if (selected)
                        GPIO.out_w1tc = (1UL << PIN_EXTSEL_N);  /* FP ROM off */
                    else
                        GPIO.out_w1ts = (1UL << PIN_EXTSEL_N);  /* FP ROM on  */
                    xQueueSend(eventQueue, &selected, 0);
                }
            } else if (selected && is_int_range) {
                int_regs[off8] = data;
            }
            /* is_vera_range: VERA latches on PHI2 fall with CS already low */
        }

        /* 6. Wait PHI2 low, release bus, deassert VERA CS */
        while (GPIO.in & (1UL << PIN_PHI2));
        bus_release();
        GPIO.out_w1ts = (1UL << PIN_DEV_SEL_N);
    }

#else   /* BUS_MODE == 1 */
    /* ------------------------------------------------------------------ */
    /* CCTL mode                                                           */
    /*                                                                     */
    /* The device is always "active" when CCTL_N is asserted.             */
    /* No device-selection state, no EXSEL, no ROM serving.               */
    /* Atari software (VERAX16.BIN) is responsible for driver init.       */
    /* A8-A10 GPIOs (14, 12, 13) are not wired in this mode.             */
    /* ------------------------------------------------------------------ */

    GPIO.out_w1ts = (1UL << PIN_DEV_SEL_N);
    bus_release();

    for (;;)
    {
        /* 1. Spin-wait for PHI2 rising edge */
        uint32_t g_lo;
        while (!((g_lo = GPIO.in) & (1UL << PIN_PHI2)));

        /* 2. Check CCTL_N -- skip if not our address space */
        if (g_lo & (1UL << PIN_BUS_SEL_N)) {
            while (GPIO.in & (1UL << PIN_PHI2));
            continue;
        }

        /* 3. Decode register offset from A0-A4 (sufficient for VERA 0-31) */
        uint8_t off = decode_offset_cctl(GPIO.in1.val);

        /* 4. Assert VERA CS if within register range */
        if (off <= VERA_REG_MAX)
            GPIO.out_w1tc = (1UL << PIN_DEV_SEL_N);

        /* 5. Wait PHI2 low, release, deassert VERA CS */
        while (GPIO.in & (1UL << PIN_PHI2));
        bus_release();
        GPIO.out_w1ts = (1UL << PIN_DEV_SEL_N);
    }
#endif
}

// ============================================================================
// setup()
// ============================================================================
void setup(void)
{
    Serial.begin(115200, SERIAL_8N1, -1, 43);  /* TX=GPIO43 (S3 UART0), RX disabled */

#if BUS_MODE == 0
    Serial.println("[VeraX16] PBI mode -- ECI connector");
    memset(int_regs, 0x00, sizeof(int_regs));
    build_drive_lut();
    eventQueue = xQueueCreate(8, sizeof(bool));
#else
    Serial.println("[VeraX16] CCTL mode -- cartridge connector");
    eventQueue = xQueueCreate(1, sizeof(bool));  /* unused, minimal alloc */
#endif

    gpio_config_t cfg = {};
    cfg.intr_type    = GPIO_INTR_DISABLE;
    cfg.pull_up_en   = GPIO_PULLUP_DISABLE;
    cfg.pull_down_en = GPIO_PULLDOWN_DISABLE;
    cfg.mode         = GPIO_MODE_INPUT;

    /* Inputs shared by both modes: data bus, PHI2, RW, BUS_SEL, A0-A5 */
    cfg.pin_bit_mask = (uint64_t)DBUS_MASK       |
                       (1ULL << PIN_PHI2)         |
                       (1ULL << PIN_RW)           |
                       (1ULL << PIN_BUS_SEL_N)    |
                       (1ULL<<33)|(1ULL<<34)|(1ULL<<35)|
                       (1ULL<<36)|(1ULL<<37)|(1ULL<<38);
    gpio_config(&cfg);

#if BUS_MODE == 0
    /* PBI-only inputs: ROM_SEL_N, A6-A10 */
    cfg.pin_bit_mask = (1ULL << PIN_ROM_SEL_N)   |
                       (1ULL<<12)|(1ULL<<13)|(1ULL<<14)|
                       (1ULL<<15)|(1ULL<<16);
    gpio_config(&cfg);

    /* PBI-only output: EXTSEL_N (deasserted HIGH at boot) */
    pinMode(PIN_EXTSEL_N, OUTPUT);
    digitalWrite(PIN_EXTSEL_N, HIGH);
#else
    /* CCTL mode: ROM_SEL_N and A6-A10 not connected -- pull up to avoid float */
    cfg.pull_up_en   = GPIO_PULLUP_ENABLE;
    cfg.pin_bit_mask = (1ULL << PIN_ROM_SEL_N)   |
                       (1ULL<<12)|(1ULL<<13)|(1ULL<<14)|
                       (1ULL<<15)|(1ULL<<16);
    gpio_config(&cfg);
    /* EXTSEL_N unused in CCTL mode -- drive HIGH to avoid floating output */
    pinMode(PIN_EXTSEL_N, OUTPUT);
    digitalWrite(PIN_EXTSEL_N, HIGH);
#endif

    /* DEV_SEL_N: output, start deasserted (HIGH) */
    pinMode(PIN_DEV_SEL_N, OUTPUT);
    digitalWrite(PIN_DEV_SEL_N, HIGH);

    xTaskCreatePinnedToCore(
        MonitorTask, "PBI_Monitor",
        4096, NULL,
        configMAX_PRIORITIES - 1,
        NULL, 1
    );

    Serial.println("[VeraX16] Monitor active on Core 1");
}

// ============================================================================
// loop()  -- Core 0
// ============================================================================
void loop(void)
{
#if BUS_MODE == 0
    bool state;
    if (xQueueReceive(eventQueue, &state, portMAX_DELAY))
        Serial.printf("[PBI] D1FF -> device %s (mask=0x%02X)\n",
                      state ? "SELECTED (FP ROM off)" : "DESELECTED (FP ROM on)",
                      DEVICE_MASK);
#else
    /* CCTL mode: driver loaded by Atari software -- nothing to report here */
    vTaskDelay(pdMS_TO_TICKS(1000));
#endif
}
