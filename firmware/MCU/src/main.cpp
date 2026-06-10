/**
 * esp32-main.cpp  --  Atari PBI / CCTL  VeraX16 Bus Controller
 * 
 * Target MCU: ESP32-S3FN8 (QFN56)
 *
 * This firmware allows an ESP32-S3 to interface with the 6502 bus of an
 * Atari XL/XE computer via the Parallel Bus Interface (PBI) or 
 * Cartridge Control Line (CCTL).
 * 
 * This version uses a definitive 30-GPIO mapping (plus Serial on 43/44)
 * with software-based 12-bit address decoding.
 * 
 * Style: Allman
 * Language: English
 */

#ifndef BUS_MODE
#define BUS_MODE 0   /* 0 = PBI mode, 1 = CCTL mode */
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

/* ===========================================================================
 * Hardware Pin Mapping (ESP32-S3FN8 QFN56)
 * =========================================================================== */

/* --- Bank 0 Signals (GPIO 0-31) --- */
#define PIN_PHI2        1    /* CPU Clock Phase 2 (Pin 16) */
#define PIN_RW          2    /* Read/Write, High = Read (Pin 17) */

/* D0-D7: GPIO 4 to 11 (Pins 19-26, contiguous in Bank 0) */
#define DBUS_MASK       0x00000FF0UL 
static const uint8_t DBUS_PINS[8] = { 4, 5, 6, 7, 8, 9, 10, 11 };

/* A0-A9: GPIO 12 to 21 (Pins 27-36, contiguous in Bank 0) */
#define ABUS_LO_MASK    0x003FF000UL

/* --- Bank 1 Signals (GPIO 32-48) --- */
#define PIN_A10         35   /* Address Bit 10 (Pin 37) */
#define PIN_A11         36   /* Address Bit 11 (Pin 38) */
#define PIN_ARESET      37   /* Atari System Reset Output (Pin 39) */
#define PIN_CRESET      38   /* Vera Chip Reset Output (Pin 40) */
#define PIN_CDONE       39   /* Vera Programmed Status Input (Pin 41) */
#define PIN_DEV_SEL_N   40   /* Vera Chip Select Output (Pin 42, active LOW) */
#define PIN_EXTSEL_N    41   /* Atari MMU Disable Output (Pin 43, active LOW) */
#define PIN_MPD         42   /* Math Pack Disable Output (Pin 44, active LOW) */

/* Serial Debug (UART0 default pins on ESP32-S3 QFN56) */
#define PIN_UART_TX     43   /* UART0 TX (Pin 49) */
#define PIN_UART_RX     44   /* UART0 RX (Pin 50) */

/* Spare Pins */
#define PIN_SPARE1      47   /* GPIO 47 (Pin 53) */
#define PIN_SPARE2      48   /* GPIO 48 (Pin 54) */

/* ===========================================================================
 * Helper Macros & Inline Functions
 * =========================================================================== */

/**
 * Decode A0-A11 from GPIO input snapshots.
 * A0-A9 are in Bank 0 bits 12-21.
 * A10-A11 are in Bank 1 bits 3-4 (since 35-32=3, 36-32=4).
 */
static inline uint16_t IRAM_ATTR decode_addr(uint32_t lo, uint32_t hi)
{
    uint16_t a = (uint16_t)((lo >> 12) & 0x3FFu);      /* A0-A9   */
    a |= (uint16_t)(((hi >> (PIN_A10 - 32)) & 1u) << 10); /* A10     */
    a |= (uint16_t)(((hi >> (PIN_A11 - 32)) & 1u) << 11); /* A11     */
    return a;
}

/**
 * Decode D0-D7 from Bank 0 snapshot.
 */
static inline uint8_t IRAM_ATTR decode_data(uint32_t lo)
{
    return (uint8_t)((lo >> 4) & 0xFFu);
}

/* Control Signal Assert/Deassert Macros (Bank 1) */
#define MPD_ASSERT()       GPIO.out1_w1tc.val = (1UL << (PIN_MPD - 32))
#define MPD_DEASSERT()     GPIO.out1_w1ts.val = (1UL << (PIN_MPD - 32))

#define EXTSEL_ASSERT()    GPIO.out1_w1tc.val = (1UL << (PIN_EXTSEL_N - 32))
#define EXTSEL_DEASSERT()  GPIO.out1_w1ts.val = (1UL << (PIN_EXTSEL_N - 32))

#define VERA_CS_ASSERT()   GPIO.out1_w1tc.val = (1UL << (PIN_DEV_SEL_N - 32))
#define VERA_CS_DEASSERT() GPIO.out1_w1ts.val = (1UL << (PIN_DEV_SEL_N - 32))

/* ===========================================================================
 * Shared Data
 * =========================================================================== */

#if BUS_MODE == 0
static IRAM_ATTR uint32_t lut_drive[256];   /* Lookup table for fast data bus drive */
static IRAM_ATTR uint8_t  int_regs[256];    /* ESP32 internal registers */
static IRAM_ATTR uint8_t  ram_pbi[512];     /* 512B RAM at $D600-$D7FF */
#endif

static QueueHandle_t eventQueue;

/* ===========================================================================
 * Bus Drive/Release Logic
 * =========================================================================== */

#if BUS_MODE == 0
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
 * Uses atomic register writes for maximum performance.
 */
static inline void IRAM_ATTR bus_drive(uint8_t val)
{
    uint32_t mask = lut_drive[val];
    GPIO.out_w1tc = DBUS_MASK & ~mask;
    GPIO.out_w1ts = mask;
    GPIO.enable_w1ts = DBUS_MASK;
}
#endif

/**
 * Release the data bus (set to High-Z).
 */
static inline void IRAM_ATTR bus_release(void)
{
    GPIO.enable_w1tc = DBUS_MASK;
}

/* ===========================================================================
 * MonitorTask -- Core 1, IRAM-resident
 * =========================================================================== */
static void IRAM_ATTR MonitorTask(void *arg)
{
#if BUS_MODE == 0
    /* PBI Mode Implementation */
    bool selected = false;

    /* Initial state: Deassert all outputs */
    EXTSEL_DEASSERT();
    VERA_CS_DEASSERT();
    MPD_DEASSERT();
    bus_release();

    for (;;)
    {
        /* 1. Wait for PHI2 rising edge and capture snapshots */
        uint32_t g_lo;
        while (!((g_lo = GPIO.in) & (1UL << PIN_PHI2)));
        uint32_t g_hi = GPIO.in1.val;

        /* 2. Decode Address and Signals */
        bool is_read = (g_lo & (1UL << PIN_RW)) != 0;
        uint16_t addr = decode_addr(g_lo, g_hi);
        uint8_t  off8 = (uint8_t)(addr & 0xFFu);

        /* Address range decoding within $Dxxx page ($D000 is base 0x000 in decode) */
        bool is_d1xx  = (addr >= 0x100u && addr <= 0x1FFu);
        bool is_d6xx  = (addr >= 0x600u && addr <= 0x7FFu);
        bool is_d8xx  = (addr >= 0x800u && addr <= 0xFFFu);

        bool is_vera_range = is_d1xx && (off8 <= 0x1Fu);
        bool is_int_range  = is_d1xx && (off8 >= 0x20u && off8 <= 0xFEu);
        bool is_vcs_latch  = is_d1xx && (off8 == 0xFFu);

        /* 3. EXTSEL Logic: Asserted for D1xx (except latch) and D6xx (RAM) */
        if (selected && ((is_d1xx && !is_vcs_latch) || is_d6xx))
        {
            EXTSEL_ASSERT();
        }

        /* 4. MPD Logic: Asserted for D8xx (ROM) */
        if (selected && is_d8xx)
        {
            MPD_ASSERT();
        }

        /* 5. Vera CS Logic: Asserted for VERA register range */
        if (selected && is_vera_range)
        {
            VERA_CS_ASSERT();
        }

        /* 6. Cycle Handling */
        if (is_read)
        {
            if (selected)
            {
                if (is_d8xx)
                {
                    bus_drive(pbi_driver[addr & 0x7FFu]);
                }
                else if (is_d6xx)
                {
                    bus_drive(ram_pbi[addr & 0x1FFu]);
                }
                else if (is_int_range)
                {
                    bus_drive(int_regs[off8]);
                }
            }
        }
        else
        {
            /* Write cycle: Sample data bus */
            uint8_t data = decode_data(GPIO.in);

            if (is_vcs_latch)
            {
                bool new_sel = (data == 0x80u);
                if (new_sel != selected)
                {
                    selected = new_sel;
                    xQueueSend(eventQueue, &selected, 0);
                }
            }
            else if (selected && is_int_range)
            {
                int_regs[off8] = data;
            }
        }

        /* 7. Cycle Completion: Wait for PHI2 falling edge and release bus */
        while (GPIO.in & (1UL << PIN_PHI2));
        bus_release();
        MPD_DEASSERT();
        EXTSEL_DEASSERT();
        VERA_CS_DEASSERT();
    }

#else
    /* CCTL Mode Implementation */
    VERA_CS_DEASSERT();
    bus_release();

    for (;;)
    {
        uint32_t g_lo;
        while (!((g_lo = GPIO.in) & (1UL << PIN_PHI2)));
        uint32_t g_hi = GPIO.in1.val;

        uint16_t addr = decode_addr(g_lo, g_hi);
        
        /* CCTL usually triggers on the whole $D5xx range (0x500 offset) */
        if (addr >= 0x500u && addr <= 0x5FFu)
        {
            uint8_t off = (uint8_t)(addr & 0xFFu);
            if (off <= 0x1Fu)
            {
                VERA_CS_ASSERT();
            }
        }

        while (GPIO.in & (1UL << PIN_PHI2));
        VERA_CS_DEASSERT();
    }
#endif
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

    /* Initialize Serial for Debug (UART0 default pins 43/44) */
    Serial.begin(115200, SERIAL_8N1, PIN_UART_RX, PIN_UART_TX);

#if BUS_MODE == 0
    Serial.println("[VeraX16] Initializing PBI Mode (Parallel Bus Interface)");
    memset(int_regs, 0x00, sizeof(int_regs));
    memset(ram_pbi,  0x00, sizeof(ram_pbi));
    build_drive_lut();
    eventQueue = xQueueCreate(8, sizeof(bool));
#else
    Serial.println("[VeraX16] Initializing CCTL Mode (Cartridge Control)");
    eventQueue = xQueueCreate(1, sizeof(bool));
#endif

    /* Configure GPIO Modes */
    gpio_config_t cfg = {};
    cfg.intr_type    = GPIO_INTR_DISABLE;
    cfg.pull_up_en   = GPIO_PULLUP_DISABLE;
    cfg.pull_down_en = GPIO_PULLDOWN_DISABLE;
    cfg.mode         = GPIO_MODE_INPUT;

    /* Bank 0 inputs: PHI2, RW, D0-D7, A0-A9 */
    cfg.pin_bit_mask = (1ULL << PIN_PHI2) | (1ULL << PIN_RW) | DBUS_MASK | ABUS_LO_MASK;
    gpio_config(&cfg);

    /* Bank 1 inputs: A10, A11, CDONE */
    cfg.pin_bit_mask = (1ULL << PIN_A10) | (1ULL << PIN_A11) | (1ULL << PIN_CDONE);
    gpio_config(&cfg);

    /* Output Signal Initialization */
    pinMode(PIN_ARESET, OUTPUT);
    digitalWrite(PIN_ARESET, HIGH);
    
    pinMode(PIN_CRESET, OUTPUT);
    digitalWrite(PIN_CRESET, HIGH);

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

/* ===========================================================================
 * Main Loop -- Core 0, Debug & Event Monitoring
 * =========================================================================== */
void loop(void)
{
#if BUS_MODE == 0
    bool state;
    if (xQueueReceive(eventQueue, &state, portMAX_DELAY))
    {
        Serial.printf("[Event] VCS Selection: %s\n", state ? "ENABLED" : "DISABLED");
    }
#else
    vTaskDelay(pdMS_TO_TICKS(1000));
#endif
}
