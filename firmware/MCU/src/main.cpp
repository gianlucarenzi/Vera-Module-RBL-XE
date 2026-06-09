/**
 * esp32-main.cpp  --  Atari PBI / CCTL  VeraX16 Bus Controller
 *
 * Compile-time selection via BUS_MODE:
 *
 *   BUS_MODE 0  (default) -- PBI mode
 *     Connector : XL/XE ECI
 *     Signals   : D1XX_N, ROM_SEL_N, RAM_SEL_N, EXTSEL_N, MPD
 *     Selection : D1FF one-hot write -- 0x80 selects this device (VCS latch)
 *     ROM       : 2 KB image served from IRAM at $D800-$DFFF
 *                 MPD (Math Pack Disable) asserted ONLY during ROM cycles
 *     RAM       : 512 B buffer at $D600-$D7FF
 *                 EXTSEL_N asserted during RAM cycles (disables MMU/Freddie)
 *     VERA regs : $D100-$D11F (A0-A4, 32 registers) -- DEV_SEL_N asserted
 *                 EXTSEL_N asserted during D1xx cycles (excl. D1FF latch reg)
 *
 *   BUS_MODE 1             -- CCTL mode
 *     Connector : 400/800 or XL/XE cartridge port
 *     Signals   : CCTL_N (same pin as D1XX_N)
 *     Selection : always active when CCTL_N asserted ($D500-$D5FF)
 *     ROM       : none
 *     VERA regs : $D500-$D51F -- DEV_SEL_N asserted on A0-A4 range
 *
 * Supported MCUs:
 *
 *   MCU_ESP32_S3  --  ESP32-S3FN8, QFN56
 *   -------------------------------------------------------
 *   D0-D7         : GPIO  4,  5,  6,  7,  8,  9, 10, 11  (bank-0 bits 4-11)
 *   A0-A5         : GPIO 33, 34, 35, 36, 37, 38           (bank-1 bits 1-6)
 *   A6-A10        : GPIO 12, 13, 14, 15, 16               (bank-0, PBI only)
 *                              GPIO15/16 = pad XTAL_32K_P/N: usati come GPIO normali;
 *                              nessun quarzo 32 kHz; MCU su RC interno a 240 MHz
 *   PHI2          : GPIO  2   (bank-0)
 *   R/W_          : GPIO 17   (bank-0)
 *   D1XX_N/CCTL_N : GPIO 18   (bank-0, direct da 74LVC 3.3V)
 *   ROM_SEL_N     : GPIO 21   (bank-0, direct da 74LVC 3.3V, PBI only)
 *   RAM_SEL_N     : GPIO 40   (bank-1, MTDO, QFN56 pin 45, direct da 74LVC 3.3V, PBI only)
 *   EXTSEL_N      : GPIO  0   (output) -- disabilita MMU/Freddie per D1xx e D6xx
 *                              add 10 kohm pull-up to 3.3 V (strapping pin)
 *   DEV_SEL_N     : GPIO  1   (output) -- VERA chip select
 *   MPD           : GPIO 42   (bank-1, MTMS, QFN56 pin 48, output, active LOW)
 *                              Math Pack Disable -- solo durante accessi ROM $D800-$DFFF
 *                              via U3 canale A6/B6 (liberato da D1XX_N → 74LVC)
 *   UART TX       : GPIO 43
 *   WARNING GPIO0 : strapping (download mode if LOW at reset) -- 10 kohm pull-up
 *   WARNING GPIO3 : strapping (JTAG source), floats at reset -- tie GND via 10 kohm
 *   WARNING GPIO1 : 60 us LOW glitch at power-up -- hold VERA in reset during boot
 *   WARNING GPIO26-32: in-package Quad SPI flash -- never connect externally
 *
 * Architecture:
 *   Core 1: MonitorTask -- IRAM, no blocking calls, ~54-cycle decode path.
 *   Core 0: loop()      -- serial debug via FreeRTOS queue.
 *
 * Timing (NTSC 6502 @ 1.7897 MHz):
 *   PHI2 high period ~= 279 ns -- every decode+drive path must stay in IRAM.
 *   MPD must be asserted BEFORE bus_drive() to tristate Atari bus buffer first.
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

// ===========================================================================
// MCU-specific pin definitions, bitmasks and decode helpers
// ===========================================================================

#if defined(MCU_ESP32_S3)
/* ---- ESP32-S3FN8 -------------------------------------------------------- */
#define PIN_PHI2        2
#define PIN_RW         17
#define PIN_BUS_SEL_N  18
#define PIN_DEV_SEL_N   1
#define PIN_ROM_SEL_N  21
#define PIN_RAM_SEL_N  40   /* MTDO, QFN56 pin 45 -- /RAM_SEL ($D600-$D7FF) */
#define PIN_EXTSEL_N    0
#define PIN_MPD        42   /* MTMS, QFN56 pin 48 -- Memory Port Data, active LOW */
                             /* Level shifting 3.3 V -> 5 V required on PCB      */
#define PIN_UART_TX    43

/* D0-D7: GPIO4-11, bank-0 bits 4-11, DBUS_MASK = 0x00000FF0 */
#define DBUS_MASK \
    ((1UL<<4)|(1UL<<5)|(1UL<<6)|(1UL<<7)|(1UL<<8)|(1UL<<9)|(1UL<<10)|(1UL<<11))
static const uint8_t DBUS_PINS[8] = {4, 5, 6, 7, 8, 9, 10, 11};

/* A0-A5: GPIO33-38 (bank-1 bits 1-6);  A6-A10: GPIO12-16 (bank-0) */
static inline uint16_t IRAM_ATTR decode_addr(uint32_t lo, uint32_t hi)
{
    uint16_t a = (uint16_t)((hi >> 1) & 0x3Fu);       /* A0-A5  GPIO33-38 */
    a |= (uint16_t)(((lo >> 12) & 1u) <<  6);          /* A6     GPIO12    */
    a |= (uint16_t)(((lo >> 13) & 1u) <<  7);          /* A7     GPIO13    */
    a |= (uint16_t)(((lo >> 14) & 1u) <<  8);          /* A8     GPIO14    */
    a |= (uint16_t)(((lo >> 15) & 1u) <<  9);          /* A9     GPIO15 (XTAL_32K_P) */
    a |= (uint16_t)(((lo >> 16) & 1u) << 10);          /* A10    GPIO16 (XTAL_32K_N) */
    return a;
}
static inline uint8_t IRAM_ATTR decode_offset_cctl(uint32_t hi)
{
    return (uint8_t)((hi >> 1) & 0x1Fu);  /* GPIO33-37 (in1 bits 1-5) = A0-A4 */
}
static inline uint8_t IRAM_ATTR decode_data(uint32_t lo)
{
    return (uint8_t)((lo >> 4) & 0xFFu);  /* GPIO4-11 compact read */
}

/* MPD is GPIO42 (bank-1, bit 10). Must use out1_w1tc/w1ts for GPIO32+. */
#define MPD_ASSERT()   GPIO.out1_w1tc.val = (1UL << (PIN_MPD - 32))
#define MPD_DEASSERT() GPIO.out1_w1ts.val = (1UL << (PIN_MPD - 32))

/* RAM_SEL_N is GPIO40 (bank-1, bit 8) -- read from g_hi snapshot. */
#define DECODE_IS_RAM(g_hi) (((g_hi) & (1UL << (PIN_RAM_SEL_N - 32))) == 0)

/* gpio_config bitmasks */
#define CFG_INPUTS_SHARED \
    ((uint64_t)DBUS_MASK | (1ULL<<PIN_PHI2) | (1ULL<<PIN_RW) | \
     (1ULL<<PIN_BUS_SEL_N) | \
     (1ULL<<33)|(1ULL<<34)|(1ULL<<35)|(1ULL<<36)|(1ULL<<37)|(1ULL<<38))
/* ROM_SEL_N + A6-A10 + RAM_SEL_N (GPIO40) -- inputs, PBI only */
#define CFG_INPUTS_PBI_ONLY \
    ((1ULL<<PIN_ROM_SEL_N) | \
     (1ULL<<12)|(1ULL<<13)|(1ULL<<14)|(1ULL<<15)|(1ULL<<16) | \
     (1ULL<<PIN_RAM_SEL_N))

#elif defined(MCU_ESP32_CLASSIC)
#  error "MCU_ESP32_CLASSIC (NodeMCU/PICO-D4) is no longer supported. Use MCU_ESP32_S3."
#else
#  error "Set -D MCU_ESP32_S3 in build_flags"
#endif

/* PBI device configuration */
#define DEVICE_MASK    0x80u   /* one-hot: PBI slot 7 */
#define VERA_REG_MAX   0x1Fu   /* VERA registers at $D100-$D11F (A0-A4, 32 regs) */
#define INT_REG_LO     0x20u   /* ESP32 internal regs $D120-$D1FE (PBI only) */
#define INT_REG_HI     0xFEu

// ---------------------------------------------------------------------------
// IRAM-resident data (PBI mode only -- conserve IRAM in CCTL mode)
// ---------------------------------------------------------------------------
#if BUS_MODE == 0
static IRAM_ATTR uint32_t lut_drive[256];   /* byte value -> GPIO drive bitmask */
static IRAM_ATTR uint8_t  int_regs[256];    /* ESP32 internal register file      */
static IRAM_ATTR uint8_t  ram_pbi[512];     /* 512 B RAM visible at $D600-$D7FF  */
#endif

static QueueHandle_t eventQueue;

// ---------------------------------------------------------------------------
// build_drive_lut()  (PBI mode only)
// ---------------------------------------------------------------------------
#if BUS_MODE == 0
static void build_drive_lut(void)
{
    for (int v = 0; v < 256; v++) {
        uint32_t m = 0;
        for (int b = 0; b < 8; b++)
            if (v & (1 << b)) m |= (1UL << DBUS_PINS[b]);
        lut_drive[v] = m;
    }
}
#endif

// ---------------------------------------------------------------------------
// bus_drive() / bus_release()
//
// IMPORTANT: assert MPD BEFORE calling bus_drive() so the Atari MMU
// tristates its bus buffer before the ESP32 drives the data lines.
// Failure to assert MPD causes bus contention and may corrupt data or
// damage hardware.
// ---------------------------------------------------------------------------
#if BUS_MODE == 0
static inline void IRAM_ATTR bus_drive(uint8_t val)
{
    uint32_t mask = lut_drive[val];
    GPIO.out_w1tc = DBUS_MASK & ~mask;   /* clear outputs that must be 0 */
    GPIO.out_w1ts = mask;                /* set   outputs that must be 1 */
    GPIO.enable_w1ts = DBUS_MASK;        /* tristate -> driven            */
}
#endif

static inline void IRAM_ATTR bus_release(void)
{
    GPIO.enable_w1tc = DBUS_MASK;        /* driven -> tristate */
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
    bool selected = false;   /* VCS latch: true = device selected */

    /* Deassert all outputs before starting */
    GPIO.out_w1ts = (1UL << PIN_EXTSEL_N) | (1UL << PIN_DEV_SEL_N);
    MPD_DEASSERT();
    bus_release();

    for (;;)
    {
        /* 1. Spin-wait for PHI2 rising edge; capture bank-0 snapshot */
        uint32_t g_lo;
        while (!((g_lo = GPIO.in) & (1UL << PIN_PHI2)));

        /* 2. Capture bank-1 snapshot (address and RAM_SEL_N stable before PHI2) */
        uint32_t g_hi = GPIO.in1.val;

        /* 3. Decode control signals */
        bool is_read  = (g_lo & (1UL << PIN_RW))         != 0;
        bool is_d1xx  = (g_lo & (1UL << PIN_BUS_SEL_N))  == 0;
        bool is_rom   = (g_lo & (1UL << PIN_ROM_SEL_N))   == 0;  /* $D800-$DFFF */
        bool is_ram   = DECODE_IS_RAM(g_hi);                      /* $D600-$D7FF */

        /* 4. Decode address (A0-A10, 11 bits covers 2 KB ROM / 512 B RAM) */
        uint16_t addr = decode_addr(g_lo, g_hi);
        uint8_t  off8 = (uint8_t)(addr & 0xFFu);

        bool is_d1ff       = is_d1xx && (off8 == 0xFFu);
        bool is_vera_range = is_d1xx && (off8 <= VERA_REG_MAX);
        bool is_int_range  = is_d1xx && (off8 >= INT_REG_LO) && (off8 <= INT_REG_HI);

        /* 5. EXTSEL_N -- per-cycle, not persistent.
              Disabilita MMU/Freddie per accesso D1xx (escluso D1FF latch)
              e per accesso RAM $D600-$D7FF. */
        if (selected && ((is_d1xx && !is_d1ff) || is_ram))
            GPIO.out_w1tc = (1UL << PIN_EXTSEL_N);

        /* 6. MPD (Math Pack Disable) -- solo durante accessi ROM $D800-$DFFF.
              Disabilita la ROM interna Atari per far vincere la ROM dell'ESP32. */
        if (selected && is_rom)
            MPD_ASSERT();

        /* 7. Assert VERA CS per range D1xx VERA ($D100-$D11F), R e W. */
        if (selected && is_vera_range)
            GPIO.out_w1tc = (1UL << PIN_DEV_SEL_N);

        /* 8. READ cycle */
        if (is_read)
        {
            if (selected)
            {
                if (is_rom)
                    bus_drive(pbi_driver[addr & 0x7FFu]);  /* 2 KB ROM */
                else if (is_ram)
                    bus_drive(ram_pbi[addr & 0x1FFu]);     /* 512 B RAM */
                else if (is_int_range)
                    bus_drive(int_regs[off8]);             /* registri interni */
                /* is_vera_range: VERA guida il bus autonomamente */
            }
        }
        /* 9. WRITE cycle -- re-read GPIO.in per dati stabili */
        else
        {
            uint8_t data = decode_data(GPIO.in);

            if (is_d1ff) {
                /* VCS latch: $80 = seleziona device, qualsiasi altro = deseleziona.
                   EXTSEL_N e MPD sono segnali per-ciclo, non si toccano qui. */
                bool new_sel = (data == DEVICE_MASK);
                if (new_sel != selected) {
                    selected = new_sel;
                    xQueueSend(eventQueue, &selected, 0);
                }
            }
            else if (selected && is_int_range) {
                int_regs[off8] = data;
            }
            /* is_vera_range write: DEV_SEL_N gia' asserito (step 7);
               VERA cattura i dati sul fronte discendente di PHI2. */
        }

        /* 10. Fine ciclo: rilascia bus, deasserta tutti gli output */
        while (GPIO.in & (1UL << PIN_PHI2));
        bus_release();
        MPD_DEASSERT();
        GPIO.out_w1ts = (1UL << PIN_EXTSEL_N) | (1UL << PIN_DEV_SEL_N);
    }

#else   /* BUS_MODE == 1 */
    /* ------------------------------------------------------------------ */
    /* CCTL mode                                                           */
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

        /* 3. Decode A0-A4 from bank-1 */
        uint8_t off = decode_offset_cctl(GPIO.in1.val);

        /* 4. Assert VERA CS if within 32-register range */
        if (off <= VERA_REG_MAX)
            GPIO.out_w1tc = (1UL << PIN_DEV_SEL_N);

        /* 5. Wait PHI2 low, deassert VERA CS */
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
    Serial.begin(115200, SERIAL_8N1, -1, PIN_UART_TX);

#if BUS_MODE == 0
    Serial.println("[VeraX16] PBI mode -- ECI connector");
    memset(int_regs, 0x00, sizeof(int_regs));
    memset(ram_pbi,  0x00, sizeof(ram_pbi));
    build_drive_lut();
    eventQueue = xQueueCreate(8, sizeof(bool));
#else
    Serial.println("[VeraX16] CCTL mode -- cartridge connector");
    eventQueue = xQueueCreate(1, sizeof(bool));
#endif

    gpio_config_t cfg = {};
    cfg.intr_type    = GPIO_INTR_DISABLE;
    cfg.pull_up_en   = GPIO_PULLUP_DISABLE;
    cfg.pull_down_en = GPIO_PULLDOWN_DISABLE;
    cfg.mode         = GPIO_MODE_INPUT;

    /* Inputs shared by both modes: data bus, PHI2, RW, BUS_SEL, A0-A5 */
    cfg.pin_bit_mask = CFG_INPUTS_SHARED;
    gpio_config(&cfg);

#if BUS_MODE == 0
    /* PBI-only inputs: ROM_SEL_N, A6-A10, RAM_SEL_N */
    cfg.pin_bit_mask = CFG_INPUTS_PBI_ONLY;
    gpio_config(&cfg);

    /* EXTSEL_N: output, deasserted HIGH at boot */
    pinMode(PIN_EXTSEL_N, OUTPUT);
    digitalWrite(PIN_EXTSEL_N, HIGH);

#ifdef PIN_MPD
    /* MPD: output, deasserted HIGH at boot (active LOW signal) */
    pinMode(PIN_MPD, OUTPUT);
    digitalWrite(PIN_MPD, HIGH);
#endif

#else  /* CCTL mode */
    /* PBI-only inputs not connected in CCTL mode -- pull up to avoid float */
    cfg.pull_up_en   = GPIO_PULLUP_ENABLE;
    cfg.pin_bit_mask = CFG_INPUTS_PBI_ONLY;
    gpio_config(&cfg);

    /* EXTSEL_N not driven in CCTL mode -- keep HIGH to avoid floating */
    pinMode(PIN_EXTSEL_N, OUTPUT);
    digitalWrite(PIN_EXTSEL_N, HIGH);
#endif

    /* DEV_SEL_N: output, deasserted (HIGH) */
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
        Serial.printf("[PBI] D1FF -> VCS %s (FP ROM %s)\n",
                      state ? "SELECTED ($80)" : "DESELECTED",
                      state ? "OFF" : "ON");
#else
    vTaskDelay(pdMS_TO_TICKS(1000));
#endif
}
