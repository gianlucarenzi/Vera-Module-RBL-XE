#include <Arduino.h>
#include <driver/gpio.h>
#include <soc/gpio_reg.h>
#include <soc/gpio_struct.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include <pbi-driver.h>

/* ANSI Eye-Candy ;-) */
#define ANSI_RED     "\x1b[31m"
#define ANSI_GREEN   "\x1b[32m"
#define ANSI_YELLOW  "\x1b[1;33m"
#define ANSI_BLUE    "\x1b[1;34m"
#define ANSI_MAGENTA "\x1b[1;35m"
#define ANSI_CYAN    "\x1b[1;36m"
#define ANSI_WHITE   "\x1b[1;37m"
#define ANSI_RESET   "\x1b[0m"

/* --- CONFIGURAZIONE PIN (Safe Mapping per PICO-D4) --- */

// Registro 1 (GPIO 32-39) - Indirizzi A0-A7
constexpr gpio_num_t PIN_A0 = GPIO_NUM_36; // bit 4
constexpr gpio_num_t PIN_A1 = GPIO_NUM_37; // bit 5
constexpr gpio_num_t PIN_A2 = GPIO_NUM_38; // bit 6
constexpr gpio_num_t PIN_A3 = GPIO_NUM_39; // bit 7
constexpr gpio_num_t PIN_A4 = GPIO_NUM_34; // bit 2
constexpr gpio_num_t PIN_A5 = GPIO_NUM_35; // bit 3
constexpr gpio_num_t PIN_A6 = GPIO_NUM_32; // bit 0
constexpr gpio_num_t PIN_A7 = GPIO_NUM_33; // bit 1

// Registro 0 (GPIO 0-31) - Dati D0-D7
constexpr gpio_num_t PIN_D0 = GPIO_NUM_4;
constexpr gpio_num_t PIN_D1 = GPIO_NUM_13;
constexpr gpio_num_t PIN_D2 = GPIO_NUM_14;
constexpr gpio_num_t PIN_D3 = GPIO_NUM_18;
constexpr gpio_num_t PIN_D4 = GPIO_NUM_19;
constexpr gpio_num_t PIN_D5 = GPIO_NUM_21;
constexpr gpio_num_t PIN_D6 = GPIO_NUM_22;
constexpr gpio_num_t PIN_D7 = GPIO_NUM_25;

#define MASK_DATA_BUS ((1ULL << PIN_D0) | (1ULL << PIN_D1) | (1ULL << PIN_D2) | (1ULL << PIN_D3) | \
                       (1ULL << PIN_D4) | (1ULL << PIN_D5) | (1ULL << PIN_D6) | (1ULL << PIN_D7))

// Segnali di Controllo
constexpr gpio_num_t PIN_PHI2   = GPIO_NUM_23;
constexpr gpio_num_t PIN_RW     = GPIO_NUM_27;
constexpr gpio_num_t PIN_D1XX   = GPIO_NUM_10;
constexpr gpio_num_t PIN_CCTL   = GPIO_NUM_9;
constexpr gpio_num_t PIN_VERA_CS= GPIO_NUM_26;
constexpr gpio_num_t PIN_EXSEL  = GPIO_NUM_15;
constexpr gpio_num_t PIN_MPD    = GPIO_NUM_5;

#define MASK_PHI2 (1UL << PIN_PHI2)
#define MASK_RW   (1UL << PIN_RW)
#define MASK_D1XX (1UL << PIN_D1XX)
#define MASK_CCTL (1UL << PIN_CCTL)

/* --- LOGGING NON-BLOCKING --- */
#define LOG_QUEUE_SIZE 64
typedef struct {
    uint32_t timestamp;
    uint16_t addr;
    uint8_t data;
    char type; // 'R'ead, 'W'rite, 'C'CTL, 'P'BI
} bus_log_t;

QueueHandle_t logQueue;
volatile uint32_t droppedLogs = 0;

void IRAM_ATTR log_bus(uint16_t addr, uint8_t data, char type) {
    bus_log_t log = { xTaskGetTickCountFromISR(), addr, data, type };
    if (xQueueSendFromISR(logQueue, &log, NULL) != pdTRUE) {
        droppedLogs++;
    }
}

/* --- LOOK-UP TABLE PER SCRITTURA DATI --- */
static uint32_t data_set_lut[256];

static void precompute_data_lut() {
    for (int v = 0; v < 256; v++) {
        uint32_t m = 0;
        if (v & 0x01) m |= (1 << PIN_D0);
        if (v & 0x02) m |= (1 << PIN_D1);
        if (v & 0x04) m |= (1 << PIN_D2);
        if (v & 0x08) m |= (1 << PIN_D3);
        if (v & 0x10) m |= (1 << PIN_D4);
        if (v & 0x20) m |= (1 << PIN_D5);
        if (v & 0x40) m |= (1 << PIN_D6);
        if (v & 0x80) m |= (1 << PIN_D7);
        data_set_lut[v] = m;
    }
}

/* --- ACCESSO VELOCE AI BUS --- */

// Legge A0-A7 tramite bit-shuffling del registro 1
static inline uint8_t read_addr_low() {
    uint32_t in1 = GPIO.in1.val;
    // A0-A3 (bits 4-7) -> move to 0-3
    // A4-A5 (bits 2-3) -> move to 4-5
    // A6-A7 (bits 0-1) -> move to 6-7
    return ((in1 & 0xF0) >> 4) | ((in1 & 0x0C) << 2) | ((in1 & 0x03) << 6);
}

// Legge D0-D7 tramite campionamento sparse del registro 0
static inline uint8_t read_data_bus() {
    uint32_t in = GPIO.in;
    uint8_t d = 0;
    if (in & (1 << PIN_D0)) d |= 0x01;
    if (in & (1 << PIN_D1)) d |= 0x02;
    if (in & (1 << PIN_D2)) d |= 0x04;
    if (in & (1 << PIN_D3)) d |= 0x08;
    if (in & (1 << PIN_D4)) d |= 0x10;
    if (in & (1 << PIN_D5)) d |= 0x20;
    if (in & (1 << PIN_D6)) d |= 0x40;
    if (in & (1 << PIN_D7)) d |= 0x80;
    return d;
}

// Scrive D0-D7 usando la LUT (Massima velocità)
static inline void write_data_bus(uint8_t val) {
    uint32_t set_mask = data_set_lut[val];
    GPIO.out_w1ts = set_mask;
    GPIO.out_w1tc = MASK_DATA_BUS & ~set_mask;
}

static inline void data_bus_mode(gpio_mode_t mode) {
    if (mode == GPIO_MODE_OUTPUT) GPIO.enable_w1ts = (uint32_t)MASK_DATA_BUS;
    else GPIO.enable_w1tc = (uint32_t)MASK_DATA_BUS;
}

/* --- MEMORIA --- */
static uint8_t d500_ram[256] = {0};
static bool device_selected = false;

/* --- TASKS --- */

void SerialTask(void *pvParameters) {
    bus_log_t log;
    uint32_t lastReport = 0;
    while(1) {
        if (xQueueReceive(logQueue, &log, pdMS_TO_TICKS(100))) {
            Serial.printf("[%u] %c %04X -> %02X\n", log.timestamp, log.type, log.addr, log.data);
        }

        uint32_t now = millis();
        if (now - lastReport > 1000) {
            if (droppedLogs > 0) {
                Serial.printf(ANSI_RED "!!! WARNING: Dropped %u logs due to slow serial !!!\n" ANSI_RESET, droppedLogs);
                droppedLogs = 0; // Reset after reporting
            }
            lastReport = now;
        }
    }
}

void IRAM_ATTR MonitorTask(void *pvParameters) {
    while(1) {
        // 1. Sincronizzazione PHI2 High
        while (!(GPIO.in & MASK_PHI2));

        uint32_t in_reg = GPIO.in;
        uint8_t addr = read_addr_low();
        bool is_read = (in_reg & MASK_RW);

        // 2. Logica CCTL ($D5xx)
        if (!(in_reg & MASK_CCTL)) {
            if (is_read) {
                uint8_t data = d500_ram[addr];
                write_data_bus(data);
                data_bus_mode(GPIO_MODE_OUTPUT);
                while (GPIO.in & MASK_PHI2); // Attendi fine ciclo
                data_bus_mode(GPIO_MODE_INPUT);
                log_bus(0xD500 | addr, data, 'C');
            } else {
                while (GPIO.in & MASK_PHI2); // Campiona a fine ciclo
                uint8_t data = read_data_bus();
                d500_ram[addr] = data;
                log_bus(0xD500 | addr, data, 'c');
            }
            continue;
        }

        // 3. Logica PBI I/O ($D1xx)
        if (!(in_reg & MASK_D1XX)) {
            if (addr == 0xFF && !is_read) {
                while (GPIO.in & MASK_PHI2);
                uint8_t dev = read_data_bus();
                device_selected = (dev == 0x01);
                GPIO.out_w1tc = (1 << PIN_VERA_CS); 
                log_bus(0xD1FF, dev, 'P');
            }
            while (GPIO.in & MASK_PHI2);
            continue;
        }

        // 4. Fine ciclo (Fallback)
        while (GPIO.in & MASK_PHI2);
    }
}

void setup() {
    Serial.begin(115200);
    precompute_data_lut();
    logQueue = xQueueCreate(LOG_QUEUE_SIZE, sizeof(bus_log_t));

    gpio_config_t io_conf = {};
    
    // Ingressi PHI2, RW, D1XX, CCTL
    io_conf.mode = GPIO_MODE_INPUT;
    io_conf.pin_bit_mask = MASK_PHI2 | MASK_RW | MASK_D1XX | MASK_CCTL;
    gpio_config(&io_conf);

    // Ingressi Indirizzi (Registro 1)
    io_conf.pin_bit_mask = (1ULL << PIN_A0) | (1ULL << PIN_A1) | (1ULL << PIN_A2) | 
                           (1ULL << PIN_A3) | (1ULL << PIN_A4) | (1ULL << PIN_A5) | 
                           (1ULL << PIN_A6) | (1ULL << PIN_A7);
    gpio_config(&io_conf);

    // Dati (Input default)
    io_conf.pin_bit_mask = MASK_DATA_BUS;
    gpio_config(&io_conf);

    // Uscite
    io_conf.mode = GPIO_MODE_OUTPUT;
    io_conf.pin_bit_mask = (1ULL << PIN_VERA_CS) | (1ULL << PIN_EXSEL) | (1ULL << PIN_MPD);
    gpio_config(&io_conf);

    gpio_set_level(PIN_VERA_CS, 1);
    gpio_set_level(PIN_EXSEL, 1);
    gpio_set_level(PIN_MPD, 1);

    xTaskCreatePinnedToCore(SerialTask, "SerialTask", 4096, NULL, 1, NULL, 0);
    xTaskCreatePinnedToCore(MonitorTask, "MonitorTask", 4096, NULL, 24, NULL, 1);
}

void loop() { vTaskDelay(pdMS_TO_TICKS(1000)); }
