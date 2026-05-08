/**
 * esp32-main.cpp  —  Atari PBI VeraX16 Bus Controller
 *
 * Architecture:
 *   Core 1: MonitorTask — high-speed 6502 bus monitor (IRAM, no blocking calls).
 *   Core 0: loop()      — serial debug, event logging via FreeRTOS queue.
 *
 * External hardware:
 *   74HC138 decodes $D800-$DFFF → PIN_ROM_SEL_N (Y7), also handles MPD in HW.
 *   External address decoder    → PIN_D1XX_N (D1xx page).
 *   External VERA chip on bus   → chip CS driven by PIN_DEV_SEL_N.
 *
 * Timing budget (Atari 800XL NTSC, 6502 @ 1.7897 MHz):
 *   PHI2 high period         ≈ 279 ns  →  ~67 ESP32 cycles @240 MHz
 *   6502 data setup (tMDS)   ≈ 100 ns before PHI2↓  →  24 cycles headroom
 *   Window from PHI2↑        ≈ 179 ns  →  ~43 cycles
 *   Full decode+drive path   ≈  54 cycles (IRAM, no branches) — borderline.
 *   CPU write data valid      ≈  80–100 ns after PHI2↑ → re-read GPIO after decode.
 *
 *   If VERA chip CS timing is too tight, route DEV_SEL_N through external
 *   combinatorial hardware (GAL/CPLD) instead of the ESP32.
 *
 * ⚠ PIN WARNINGS:
 *   GPIO  0 (PIN_EXTSEL_N) : ESP32 bootstrap pin.  If held LOW at power-on
 *                             the chip enters bootloader mode.  Ensure the
 *                             Atari cannot drive this line LOW during ESP32 boot.
 *   GPIO  3 (PIN_DEV_SEL_N): UART0 RX.  Serial RX must be disabled:
 *                             Serial.begin(baud, cfg, /*rx=*/-1, /*tx=*/1).
 *
 * Pin mapping:
 *   Data bus D0-D7  : GPIO 18, 19, 21, 22, 23, 25, 26, 27
 *   Addr A0-A5      : GPIO 32, 33, 34, 35, 36, 39   (GPIO_IN1 register)
 *   Addr A6-A10     : GPIO 16, 17, 14, 12, 13        (GPIO_IN register)
 *   PHI2            : GPIO  2
 *   R/W_            : GPIO 15  (HIGH=read, LOW=write)
 *   D1XX_N          : GPIO  5  (active LOW — D1xx address space selected)
 *   ROM_SEL_N       : GPIO  4  (active LOW — $D800-$DFFF, from 74HC138 Y7)
 *   EXTSEL_N        : GPIO  0  (active LOW → Atari PBI EXSEL, disables FP ROM)
 *   DEV_SEL_N       : GPIO  3  (active LOW → VERA chip CS)
 */

#include <Arduino.h>
#include <soc/gpio_struct.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "pbi-driver.h"
/* pbi-driver.h must declare:
 *   extern const uint8_t pbi_driver[] IRAM_ATTR;
 * The array must be exactly 2048 bytes (2 KB, $D800-$DFFF image). */

// ---------------------------------------------------------------------------
// Pin assignments
// ---------------------------------------------------------------------------
#define PIN_PHI2        2
#define PIN_RW          15
#define PIN_D1XX_N      5
#define PIN_ROM_SEL_N   4
#define PIN_EXTSEL_N    0   // → Atari EXSEL  (see boot-pin warning above)
#define PIN_DEV_SEL_N   3   // → VERA chip CS (see UART-RX  warning above)

// Data bus bitmask — all pins in GPIO bank 0 (GPIO_IN / GPIO_OUT registers)
#define DBUS_MASK  ((1UL<<18)|(1UL<<19)|(1UL<<21)|(1UL<<22)|\
                    (1UL<<23)|(1UL<<25)|(1UL<<26)|(1UL<<27))

// PBI device configuration
#define DEVICE_MASK    0x80u   // one-hot: this device occupies PBI slot 7
#define VERA_REG_MAX   0x1Fu   // VERA registers: offset 0x00–0x1F ($D100–$D11F)
#define INT_REG_LO     0x20u   // ESP32 internal regs start: $D120
#define INT_REG_HI     0xFEu   // ESP32 internal regs end:   $D1FE

// ---------------------------------------------------------------------------
// IRAM-resident data — mandatory for timing
// ---------------------------------------------------------------------------
static IRAM_ATTR uint32_t lut_drive[256];  // byte value → GPIO bitmask
static IRAM_ATTR uint8_t  int_regs[256];   // internal ESP32 register file

// FreeRTOS event queue: Core 1 → Core 0 (debug only)
static QueueHandle_t pbiEventQueue;

// ---------------------------------------------------------------------------
// build_drive_lut()
// Precompute: byte value → GPIO OUT bitmask for data bus.
// Called once from setup(); runs from DRAM (no timing constraint here).
// ---------------------------------------------------------------------------
static void build_drive_lut(void)
{
    static const uint8_t pin[8] = {18, 19, 21, 22, 23, 25, 26, 27};
    for (int v = 0; v < 256; v++)
    {
        uint32_t m = 0;
        for (int b = 0; b < 8; b++)
        {
            if (v & (1 << b))
                m |= (1UL << pin[b]);
        }
        lut_drive[v] = m;
    }
}

// ---------------------------------------------------------------------------
// decode_addr()  — branch-free, ~18 Xtensa cycles
//
// Reconstruct A0-A10 from two GPIO snapshot words.
//   A0-A4 : GPIO_IN1 bits  0-4   (GPIO 32-36, consecutive in IN1)
//   A5    : GPIO_IN1 bit   7     (GPIO 39)
//   A6    : GPIO_IN  bit   16    (GPIO 16)
//   A7    : GPIO_IN  bit   17    (GPIO 17)
//   A8    : GPIO_IN  bit   14    (GPIO 14)
//   A9    : GPIO_IN  bit   12    (GPIO 12)
//   A10   : GPIO_IN  bit   13    (GPIO 13)
// ---------------------------------------------------------------------------
static inline uint16_t IRAM_ATTR decode_addr(uint32_t lo, uint32_t hi)
{
    uint16_t a;
    a  = (uint16_t)( hi         & 0x1Fu);          // A0-A4
    a |= (uint16_t)(((hi >>  7) & 1u) << 5);       // A5  (GPIO 39)
    a |= (uint16_t)(((lo >> 16) & 1u) << 6);       // A6
    a |= (uint16_t)(((lo >> 17) & 1u) << 7);       // A7
    a |= (uint16_t)(((lo >> 14) & 1u) << 8);       // A8
    a |= (uint16_t)(((lo >> 12) & 1u) << 9);       // A9
    a |= (uint16_t)(((lo >> 13) & 1u) << 10);      // A10
    return a;
}

// ---------------------------------------------------------------------------
// decode_data()  — branch-free, ~5 Xtensa cycles
//
// Extract data bus byte from GPIO snapshot.
// Exploits three consecutive GPIO groups:
//   GPIO 18-19  (IN bits 18-19) → D0-D1
//   GPIO 21-23  (IN bits 21-23) → D2-D4
//   GPIO 25-27  (IN bits 25-27) → D5-D7
// ---------------------------------------------------------------------------
static inline uint8_t IRAM_ATTR decode_data(uint32_t lo)
{
    return (uint8_t)(
        ( (lo >> 18) & 0x03u)        |   // D0-D1
        (((lo >> 21) & 0x07u) << 2)  |   // D2-D4
        (((lo >> 25) & 0x07u) << 5)      // D5-D7
    );
}

// ---------------------------------------------------------------------------
// bus_drive()  — put one byte on the data bus (outputs enabled)
// Clears unwanted bits BEFORE setting new ones to prevent glitches.
// ---------------------------------------------------------------------------
static inline void IRAM_ATTR bus_drive(uint8_t val)
{
    uint32_t mask = lut_drive[val];
    GPIO.out_w1tc = DBUS_MASK & ~mask;  // 1. clear bits that must be 0
    GPIO.out_w1ts = mask;               // 2. set  bits that must be 1
    GPIO.enable_w1ts = DBUS_MASK;       // 3. enable outputs (tristate → driven)
}

// ---------------------------------------------------------------------------
// bus_release()  — tristate data bus (high-Z)
// ---------------------------------------------------------------------------
static inline void IRAM_ATTR bus_release(void)
{
    GPIO.enable_w1tc = DBUS_MASK;
}

// ============================================================================
// MonitorTask — Core 1
//
// Runs at maximum FreeRTOS priority on Core 1.
// Uses no shared variables: all state is task-local.
// Only FreeRTOS call: xQueueSend (non-blocking) on D1FF state change.
// ============================================================================
static void IRAM_ATTR MonitorTask(void * /*arg*/)
{
    // Local state — isolated to Core 1, no locking needed
    bool selected = false;   // true when D1FF was written with DEVICE_MASK

    // Ensure outputs start deasserted (active-LOW signals start HIGH)
    GPIO.out_w1ts = (1UL << PIN_EXTSEL_N) | (1UL << PIN_DEV_SEL_N);
    bus_release();

    for (;;)
    {
        // --------------------------------------------------------------------
        // 1. Spin-wait for PHI2 rising edge.
        //    Capture GPIO.in atomically: address bus, R/W_ and control signals
        //    are all valid at PHI2↑.
        //    NOTE: for 6502 WRITE cycles, CPU data appears ~80-100 ns AFTER
        //    PHI2↑.  Do NOT use g_lo to sample write data — re-read GPIO.in
        //    after the decode phase instead (see step 5).
        // --------------------------------------------------------------------
        uint32_t g_lo;
        while (!((g_lo = GPIO.in) & (1UL << PIN_PHI2)));

        // --------------------------------------------------------------------
        // 2. Decode control signals from the PHI2↑ snapshot
        // --------------------------------------------------------------------
        bool is_read  = (g_lo & (1UL << PIN_RW))       != 0;
        bool is_d1xx  = (g_lo & (1UL << PIN_D1XX_N))   == 0;   // active LOW
        bool is_rom   = (g_lo & (1UL << PIN_ROM_SEL_N)) == 0;   // active LOW

        uint32_t g_hi  = GPIO.in1.val;
        uint16_t addr  = decode_addr(g_lo, g_hi);   // A0-A10,  ~18 cycles
        uint8_t  off8  = (uint8_t)(addr & 0xFFu);   // offset within D1xx page

        // Sub-range decode for D1xx space
        bool is_d1ff       = is_d1xx && (off8 == 0xFFu);
        bool is_vera_range = is_d1xx && (off8 <= VERA_REG_MAX);
        bool is_int_range  = is_d1xx && (off8 >= INT_REG_LO) && (off8 <= INT_REG_HI);

        // --------------------------------------------------------------------
        // 3. Assert DEV_SEL_N (CS to external VERA chip).
        //    Only when device is selected AND address is in VERA register range.
        //    ⚠ Assertion is ~200 ns after PHI2↑ (full decode path in IRAM).
        //      If the VERA chip cannot tolerate this latency, move this decode
        //      to external combinatorial logic (GAL/CPLD).
        // --------------------------------------------------------------------
        if (selected && is_vera_range)
        {
            GPIO.out_w1tc = (1UL << PIN_DEV_SEL_N);
        }

        // --------------------------------------------------------------------
        // 4. Bus transaction — READ
        // --------------------------------------------------------------------
        if (is_read)
        {
            if (selected)
            {
                if (is_rom)
                {
                    // $D800-$DFFF: serve 2 KB ROM image from IRAM.
                    // addr is A0-A10 (0x000-0x7FF) — direct ROM index.
                    bus_drive(pbi_driver[addr & 0x7FFu]);
                }
                else if (is_int_range)
                {
                    // $D120-$D1FE: ESP32 internal register file
                    bus_drive(int_regs[off8]);
                }
                // is_vera_range: DEV_SEL_N already asserted; the external
                //   VERA chip drives the data bus autonomously.
                // is_d1ff read: no IRQ pending → do not drive the bus.
                //   (open-drain protocol: only assert if IRQ bit is set)
            }
        }

        // --------------------------------------------------------------------
        // 5. Bus transaction — WRITE
        //    Re-read GPIO.in here (~200 ns after PHI2↑): by now the 6502
        //    write data is guaranteed stable (tMDS satisfied).
        // --------------------------------------------------------------------
        else
        {
            uint8_t data = decode_data(GPIO.in);   // fresh read, data now valid

            if (is_d1ff)
            {
                // $D1FF: PBI device select — one-hot byte (0x00 = all off).
                // Only recognise our own mask; anything else deselects us.
                bool new_sel = (data == DEVICE_MASK);
                if (new_sel != selected)
                {
                    selected = new_sel;

                    // EXTSEL_N is a LEVEL signal: assert while selected,
                    // deassert when deselected.  Must be stable before the
                    // next PHI2↑ so the Atari sees the correct FP ROM state.
                    if (selected)
                        GPIO.out_w1tc = (1UL << PIN_EXTSEL_N);  // FP ROM off
                    else
                        GPIO.out_w1ts = (1UL << PIN_EXTSEL_N);  // FP ROM on

                    // Notify Core 0 (non-blocking; drop if queue full)
                    xQueueSend(pbiEventQueue, &selected, 0);
                }
            }
            else if (selected && is_int_range)
            {
                // $D120-$D1FE: write to ESP32 internal register file
                int_regs[off8] = data;
            }
            // is_vera_range: DEV_SEL_N is asserted (step 3); the VERA chip
            //   will latch the write on PHI2↓ without ESP32 intervention.
        }

        // --------------------------------------------------------------------
        // 6. End of PHI2 cycle: wait for PHI2↓, then release bus and CS.
        //    EXTSEL_N is intentionally NOT touched here — it is a persistent
        //    level signal that follows 'selected', not the cycle boundary.
        // --------------------------------------------------------------------
        while (GPIO.in & (1UL << PIN_PHI2));

        bus_release();
        GPIO.out_w1ts = (1UL << PIN_DEV_SEL_N);   // deassert VERA CS
    }
}

// ============================================================================
// setup()  — Core 0
// ============================================================================
void setup(void)
{
    // TX-only serial: disables UART RX on GPIO 3, freeing it for DEV_SEL_N
    Serial.begin(115200, SERIAL_8N1, /*rx=*/-1, /*tx=*/1);
    Serial.println("[VeraX16 PBI] Firmware starting");

    memset(int_regs, 0x00, sizeof(int_regs));
    build_drive_lut();
    pbiEventQueue = xQueueCreate(8, sizeof(bool));

    gpio_config_t cfg = {};
    cfg.intr_type    = GPIO_INTR_DISABLE;
    cfg.pull_up_en   = GPIO_PULLUP_DISABLE;
    cfg.pull_down_en = GPIO_PULLDOWN_DISABLE;
    cfg.mode         = GPIO_MODE_INPUT;

    // Bank 0 inputs: data bus, PHI2, RW, D1XX_N, ROM_SEL_N, A6-A10
    cfg.pin_bit_mask = (uint64_t)DBUS_MASK           |
                       (1ULL << PIN_PHI2)             |
                       (1ULL << PIN_RW)               |
                       (1ULL << PIN_D1XX_N)           |
                       (1ULL << PIN_ROM_SEL_N)        |
                       (1ULL<<16)|(1ULL<<17)|(1ULL<<14)|
                       (1ULL<<12)|(1ULL<<13);
    gpio_config(&cfg);

    // Bank 1 inputs: A0-A5 (GPIO 32-36, 39)
    cfg.pin_bit_mask = (1ULL<<32)|(1ULL<<33)|(1ULL<<34)|
                       (1ULL<<35)|(1ULL<<36)|(1ULL<<39);
    gpio_config(&cfg);

    // Outputs: start deasserted (HIGH = inactive for active-LOW signals)
    pinMode(PIN_EXTSEL_N,  OUTPUT); digitalWrite(PIN_EXTSEL_N,  HIGH);
    pinMode(PIN_DEV_SEL_N, OUTPUT); digitalWrite(PIN_DEV_SEL_N, HIGH);

    // Launch bus monitor on Core 1 at maximum FreeRTOS priority
    xTaskCreatePinnedToCore(
        MonitorTask, "PBI_Monitor",
        4096, NULL,
        configMAX_PRIORITIES - 1,
        NULL, /*core=*/1
    );

    Serial.println("[VeraX16 PBI] Monitor active on Core 1");
}

// ============================================================================
// loop()  — Core 0: serial event reporting
// ============================================================================
void loop(void)
{
    bool state;
    if (xQueueReceive(pbiEventQueue, &state, portMAX_DELAY))
    {
        Serial.printf("[PBI] D1FF → device %s (mask=0x%02X)\n",
                      state ? "SELECTED  (FP ROM off)" : "DESELECTED (FP ROM on)",
                      DEVICE_MASK);
    }
}
