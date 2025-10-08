#include <Arduino.h>
#include <driver/gpio.h>
#include <soc/gpio_reg.h>
#include <soc/gpio_struct.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include <pbi-driver.h> // Include PBI driver memory space

/* ANSI Eye-Candy ;-)
 */
#define ANSI_RED    "\x1b[31m"
#define ANSI_GREEN  "\x1b[32m"
#define ANSI_YELLOW "\x1b[1;33m"
#define ANSI_BLUE   "\x1b[1;34m"
#define ANSI_RESET  "\x1b[0m"

#define SERIAL_QUEUE_LENGTH 32 // Length of the serial queue
#define SERIAL_MSG_SIZE 128 // Size of the serial message buffer

// Pin Configuration 

// Pin Definitions for 6502 Bus Monitor
// Address Bus A0-A15

// LSB Address Pins

// Decoded selection signals are only on the final revision board
constexpr gpio_num_t PIN_EXSEL = GPIO_NUM_11;	// When low, DEVICE memory is selected, when high ATARI memory is selected
constexpr gpio_num_t PIN_D1XX = GPIO_NUM_10;	// When accessing PBI I/O memory space
constexpr gpio_num_t PIN_CCTL = GPIO_NUM_20;	// When accessing Cartridge Control $D500 CCTL
constexpr gpio_num_t PIN_MPD = GPIO_NUM_7;		// Math Pack ROM Disable

// Common LSB Address Pins
constexpr gpio_num_t PIN_A0 = GPIO_NUM_6;	// D6
constexpr gpio_num_t PIN_A1 = GPIO_NUM_8;	// D8
constexpr gpio_num_t PIN_A2 = GPIO_NUM_21;	// D21
constexpr gpio_num_t PIN_A3 = GPIO_NUM_27;	// D27
constexpr gpio_num_t PIN_A4 = GPIO_NUM_33;	// D33
constexpr gpio_num_t PIN_A5 = GPIO_NUM_32;	// D32
constexpr gpio_num_t PIN_A6 = GPIO_NUM_38;	// D38
constexpr gpio_num_t PIN_A7 = GPIO_NUM_37;	// D37

// MSB Address Pins
constexpr gpio_num_t PIN_A8  = GPIO_NUM_2;	// D2
constexpr gpio_num_t PIN_A9  = GPIO_NUM_5;	// D5
constexpr gpio_num_t PIN_A10 = GPIO_NUM_12;	// D12
constexpr gpio_num_t PIN_A11 = GPIO_NUM_15;	// D15
constexpr gpio_num_t PIN_A12 = GPIO_NUM_34;	// D34
constexpr gpio_num_t PIN_A13 = GPIO_NUM_35;	// D35
constexpr gpio_num_t PIN_A14 = GPIO_NUM_36;	// D36 VP
constexpr gpio_num_t PIN_A15 = GPIO_NUM_39;	// D39 VN

// PHI2 and RW Pins
// PHI2 is used for clocking the 6502 bus
// RW is used to indicate read/write operations
constexpr gpio_num_t PIN_PHI2 = GPIO_NUM_23;	// D23
constexpr gpio_num_t PIN_RW   = GPIO_NUM_25;	// D25

// Data Bus D0-D7
// Those pins are used for data transfer on the 6502 bus
constexpr gpio_num_t PIN_D0 = GPIO_NUM_4;	// D4
constexpr gpio_num_t PIN_D1 = GPIO_NUM_13;	// D13
constexpr gpio_num_t PIN_D2 = GPIO_NUM_14;	// D14
constexpr gpio_num_t PIN_D3 = GPIO_NUM_16;	// D16 RX2
constexpr gpio_num_t PIN_D4 = GPIO_NUM_17;	// D17 TX2
constexpr gpio_num_t PIN_D5 = GPIO_NUM_18;	// D18
constexpr gpio_num_t PIN_D6 = GPIO_NUM_19;	// D19
constexpr gpio_num_t PIN_D7 = GPIO_NUM_22;	// D22

constexpr gpio_num_t PIN_CS = GPIO_NUM_26;	// D26 Chip Select Pin for FPGA VERA VIDEO CARD

// Shadow RAM PBI Driver Memory Space
static char ram_d800[2048] = {0};

// Shadow RAM 512 Bytes ($D600-$D7FF) ONLY $D600-$D6FF are used
static char ram_d600[512] = {0};

static uint8_t d500[256] = {0}; // CCTL memory space
static uint8_t cardselected = 0; // Flag to indicate if a card is selected
static uint8_t DEVICE_ID = 0x01; // Example device ID for PBI I/O
extern const uint8_t pbi_driver[]; // PBI driver memory space

#define DBG_ERROR   0
#define DBG_INFO    1
#define DBG_VERBOSE 2
#define DBG_NOISY   3

static int debuglevel = DBG_INFO;

#define CARTRIDGE_CONTROL (address >= 0xD500 && address <= 0xD5FF)
#define PBI_IO            (address >= 0xD100 && address <= 0xD1FF)
//#define CARTRIDGE_CONTROL !((gpio_low >> PIN_CCTL) & 1)
//#define PBI_IO            !((gpio_low >> PIN_D1XX) & 1)

// Checking functions for PHI2 Clock
#define PHI2_IS_LOW !((REG_READ(GPIO_IN1_REG) >> PIN_PHI2) & 1)
#define PHI2_IS_HIGH !(PHI2_IS_LOW)

QueueHandle_t serialQueue;

// Prototypes for functions
void SerialTask(void *pvParameters);
void MonitorTask(void *pvParameters);

// This task will run on the second (1) core of the ESP32
void SerialTask(void *pvParameters)
{
	char msg[SERIAL_MSG_SIZE];
	Serial.println(ANSI_BLUE "Serial Task Started on Core 0" ANSI_RESET);

	while (true)
	{
		if (xQueueReceive(serialQueue, msg, portMAX_DELAY) == pdTRUE)
		{
			Serial.println(msg);
		}
	}
}

void serialPrintQueue(const char* fmt, ...)
{
	char buf[SERIAL_MSG_SIZE];
	va_list args;
	va_start(args, fmt);
	vsnprintf(buf, SERIAL_MSG_SIZE, fmt, args);
	va_end(args);

	if (serialQueue != NULL)
	{
		xQueueSend(serialQueue, buf, portMAX_DELAY);
	}
	else
	{
		Serial.println("Serial queue not initialized!");
	}
}

// Funzioni ottimizzate per lettura GPIO
inline uint32_t read_gpio_low(void)
{
	return REG_READ(GPIO_IN1_REG);  // GPIO 0-31
}

inline uint32_t read_gpio_high(void)
{
	return REG_READ(GPIO_IN_REG);   // GPIO 32-39
}

static inline uint16_t read_address_bus(uint32_t gpio_low, uint32_t gpio_high)
{
	// Read all 16 Address Lines at once
	return (((gpio_low >> PIN_A0) & 1) << 0) |
		   (((gpio_low >> PIN_A1) & 1) << 1) |
		   (((gpio_low >> PIN_A2) & 1) << 2) |
		   (((gpio_low >> PIN_A3) & 1) << 3) |
		   (((gpio_high >> (PIN_A4 - 32)) & 1) << 4) |
		   (((gpio_high >> (PIN_A5 - 32)) & 1) << 5) |
		   (((gpio_high >> (PIN_A6 - 32)) & 1) << 6) |
		   (((gpio_high >> (PIN_A7 - 32)) & 1) << 7) |
		   (((gpio_low >> PIN_A8) & 1) << 8) |
		   (((gpio_low >> PIN_A9) & 1) << 9) |
		   (((gpio_low >> PIN_A10) & 1) << 10) |
		   (((gpio_low >> PIN_A11) & 1) << 11) |
		   (((gpio_high >> (PIN_A12 - 32)) & 1) << 12) |
		   (((gpio_high >> (PIN_A13 - 32)) & 1) << 13) |
		   (((gpio_high >> (PIN_A14 - 32)) & 1) << 14) |
		   (((gpio_high >> (PIN_A15 - 32)) & 1) << 15);
}

static inline uint8_t read_data_bus(uint32_t gpio_low)
{
	// Read all Data Bus Lines at once
	return (((gpio_low >> PIN_D0) & 1) << 0) |
		   (((gpio_low >> PIN_D1) & 1) << 1) |
		   (((gpio_low >> PIN_D2) & 1) << 2) |
		   (((gpio_low >> PIN_D3) & 1) << 3) |
		   (((gpio_low >> PIN_D4) & 1) << 4) |
		   (((gpio_low >> PIN_D5) & 1) << 5) |
		   (((gpio_low >> PIN_D6) & 1) << 6) |
		   (((gpio_low >> PIN_D7) & 1) << 7);
}

static inline void set_data_bus_direction(gpio_mode_t mode)
{
	const uint32_t pin_mask = (
		(1ULL << PIN_D0) | (1ULL << PIN_D1) | (1ULL << PIN_D2) | (1ULL << PIN_D3) |
		(1ULL << PIN_D4) | (1ULL << PIN_D5) | (1ULL << PIN_D6) | (1ULL << PIN_D7)
	);

	if (mode == GPIO_MODE_OUTPUT) {
		GPIO.enable_w1ts = pin_mask;
	} else {
		GPIO.enable_w1tc = pin_mask;
	}
}

static inline void write_data_bus(uint8_t value)
{
	const uint32_t mask = (1 << PIN_D0) | (1 << PIN_D1) | (1 << PIN_D2) | (1 << PIN_D3) |
						  (1 << PIN_D4) | (1 << PIN_D5) | (1 << PIN_D6) | (1 << PIN_D7);
	uint32_t set_mask = 0;
	if ((value >> 0) & 1) set_mask |= (1 << PIN_D0);
	if ((value >> 1) & 1) set_mask |= (1 << PIN_D1);
	if ((value >> 2) & 1) set_mask |= (1 << PIN_D2);
	if ((value >> 3) & 1) set_mask |= (1 << PIN_D3);
	if ((value >> 4) & 1) set_mask |= (1 << PIN_D4);
	if ((value >> 5) & 1) set_mask |= (1 << PIN_D5);
	if ((value >> 6) & 1) set_mask |= (1 << PIN_D6);
	if ((value >> 7) & 1) set_mask |= (1 << PIN_D7);
	GPIO.out_w1ts = set_mask;
	GPIO.out_w1tc = mask & ~set_mask;
}

static inline void fast_gpio_set_level(gpio_num_t pin, uint32_t level)
{
	if (level)
	{
		if (pin < 32)
		{
			GPIO.out_w1ts = (1U << pin);
		}
		else
		{
			GPIO.out1_w1ts.val = (1U << (pin - 32));
		}
	}
	else
	{
		if (pin < 32)
		{
			GPIO.out_w1tc = (1U << pin);
		}
		else
		{
			GPIO.out1_w1tc.val = (1U << (pin - 32));
		}
	}
}

void setup(void)
{
	Serial.begin(115200);

	serialQueue = xQueueCreate(SERIAL_QUEUE_LENGTH, SERIAL_MSG_SIZE);
	xTaskCreatePinnedToCore(SerialTask, "SerialTask", 2048, NULL, 1, NULL, 0); // Create the serial task on core 0
	xTaskCreatePinnedToCore(MonitorTask, "MonitorTask", 2048, NULL, 1, NULL, 1); // Create the monitor task on core 1

	// Configurazione pin
	gpio_config_t io_conf = {};

	// ADDRESS BUS A0-A15
	// Read Address Lines as possible LSB
	io_conf.pin_bit_mask =
		(1ULL << PIN_A0 ) | (1ULL << PIN_A1 ) | (1ULL << PIN_A2 ) | (1ULL << PIN_A3)  |
		(1ULL << PIN_A4 ) | (1ULL << PIN_A5 ) | (1ULL << PIN_A6 ) | (1ULL << PIN_A7)  |
		(1ULL << PIN_A8 ) | (1ULL << PIN_A9 ) | (1ULL << PIN_A10) | (1ULL << PIN_A11) |
		(1ULL << PIN_A12) | (1ULL << PIN_A13) | (1ULL << PIN_A14) | (1ULL << PIN_A15);

	// PHI2 & RW
	io_conf.pin_bit_mask |= (1ULL << PIN_PHI2) | (1ULL << PIN_RW);

	// DATA BUS input/output (default as input)
	io_conf.pin_bit_mask |= (1ULL << PIN_D0) | (1ULL << PIN_D1) | (1ULL << PIN_D2) |
						    (1ULL << PIN_D3) | (1ULL << PIN_D4) | (1ULL << PIN_D5) |
						    (1ULL << PIN_D6) | (1ULL << PIN_D7);
	io_conf.mode = GPIO_MODE_INPUT;
	gpio_config(&io_conf);

	// EXSEL, MPD and VERA_CS as output
	io_conf.pin_bit_mask = (1ULL << PIN_EXSEL) | (1ULL << PIN_MPD) | (1ULL << PIN_CS);
	io_conf.mode = GPIO_MODE_OUTPUT;
	gpio_config(&io_conf);

	// CCTL & D1XX as input (with a LED connected)
	io_conf.pin_bit_mask = (1ULL << PIN_CCTL) | (1ULL << PIN_D1XX);
	io_conf.mode = GPIO_MODE_INPUT;
	gpio_config(&io_conf);

	// VERA_CS not selected (high)
	gpio_set_level(PIN_CS, 1);

}

void MonitorTask(void *pvParameters)
{
	serialPrintQueue(ANSI_BLUE "6502 Bus Monitor Ready on Core 1\n" ANSI_RESET);

	for (;;)
	{
		// Wait the rising edge of PHI2
		while (PHI2_IS_LOW) ;;

		uint32_t gpio_low = read_gpio_low();
		uint32_t gpio_high = read_gpio_high();

		uint16_t address = read_address_bus(gpio_low, gpio_high);
		bool rw = (gpio_low >> PIN_RW) & 1;

		// Read all remaining signals here to minimize timing issues
		// CCTL
		if(CARTRIDGE_CONTROL)
		{
			if (rw)
			{
				// 6502 CPU is LD[X/Y/A] from memory @ $D5xx
				uint8_t data = d500[address & 0x00FF];
				set_data_bus_direction(GPIO_MODE_OUTPUT);
				write_data_bus(data);

				// wait for PHI2 low
				while (PHI2_IS_HIGH) ;;

				set_data_bus_direction(GPIO_MODE_INPUT);
				serialPrintQueue(ANSI_YELLOW "CCTL: Send $\%02X from $\%04X to CPU\n" ANSI_RESET, data, address);
			}
			else
			{
				// 6502 CPU is asking to write to memory @ $D5xx ST[X/Y/A]
				set_data_bus_direction(GPIO_MODE_INPUT);
				uint8_t data = read_data_bus(gpio_low);
				d500[address & 0x00FF] = data;
				serialPrintQueue(ANSI_YELLOW "CCTL: Received $\%02X to $\%04X from CPU\n" ANSI_RESET, data, address);
			}
		}

		// PBI I/O Management
		if(PBI_IO)
		{
			uint8_t addressLSB = address & 0x00FF; // LSB of the address

			if (addressLSB == 0xFF) // Device selection address $D1FF
			{
				// Accessing the Device Selection Register $D1FF
				serialPrintQueue(ANSI_BLUE "PBI I/O: Accessing Device Selection Register $D1FF\n" ANSI_RESET);
				// 6502 CPU writes to PBI I/O to select device
				if (!rw)
				{
					uint8_t device = read_data_bus(gpio_low);
					if (device == DEVICE_ID)
					{
						serialPrintQueue(ANSI_BLUE "PBI I/O: Device $\%02X selected\n" ANSI_RESET, device);
						cardselected = 1;
						fast_gpio_set_level(PIN_CS, 0); // Set CS low for PBI I/O
					}
					else
					{
						serialPrintQueue(ANSI_RED "PBI I/O: Invalid device $\%02X selected.\n" ANSI_RESET, device);
						cardselected = 0;
						fast_gpio_set_level(PIN_CS, 1); // Set CS high for no device selected
					}
				}
				else
				{
					// CPU reads from PBI Device Selection Register $D1FF
					// Return DEVICE_ID if device is selected, otherwise return 0

					// Is it a valid read operation?
					uint8_t data = cardselected ? DEVICE_ID : 0; // Return DEVICE_ID if device is selected, otherwise 0
					set_data_bus_direction(GPIO_MODE_OUTPUT);
					write_data_bus(data);

					// wait for PHI2 low
					while (PHI2_IS_HIGH) ;;

					set_data_bus_direction(GPIO_MODE_INPUT);
					serialPrintQueue(ANSI_GREEN "PBI I/O: Sent $\%02X to CPU from $D1FF\n" ANSI_RESET, data);
				}
			}
			else
			{
				// ADDRESS REGISTER RANGE from $00 to $FE (LSB of Address BUS)
				// should be directly connected to the real registers address lines of the PBI device
				// and the Chip Select for that device should be already activated
				if (cardselected)
				{
					// Accessing PBI I/O registers only if a device is selected
					// Valid PBI I/O registers are $D100 to $D1F0
					// $D1F1 to $D1FE are reserved/unknown
					if (addressLSB >= 00 && addressLSB <= 0xF0)
					{
						// Sniffing I/O Space registers
						serialPrintQueue(ANSI_YELLOW "PBI Device Registers: Read or Write @ $\%04X address\n" ANSI_RESET, address);
					}
					else
					{
						// Someone is trying to access to $D1F1 up to $D1FE
						serialPrintQueue(ANSI_RED "PBI Unknown Device Registers: Read or Write @ $\%04X address\n" ANSI_RESET, address);
					}
				}
				else
				{
					// Someone is trying to access to $D1XX without selecting device first! Maybe
					// another PBI device is connected and selected?
					serialPrintQueue(ANSI_RED "PBI Read or Write @ $\%04X address. This Device must be selected first!\n" ANSI_RESET, address);
				}
			}
		}

		// EXSEL e MPD Management
		if(address >= 0xD800 && address <= 0xDFFF)
		{
			if (cardselected)
			{
				fast_gpio_set_level(PIN_MPD, 0); // Set MPD low to indicate Math Pack ROM Disable
				serialPrintQueue(ANSI_YELLOW "MPD: Disabled" ANSI_RESET);
				// ROM (READ ONLY) from $D800-$D8FF (256 bytes for device driver)
				if (address >= 0xD800 && address <= 0xD8FF)
				{
					serialPrintQueue(ANSI_YELLOW "PBI ROM Driver: Area $\%04X Accessing\n" ANSI_RESET, address);
					if (rw)
					{
						// $D800-$DFFF: CPU read from PBI Driver and lower MPD
						uint8_t data = pbi_driver[ address - 0xD800 ];
						set_data_bus_direction(GPIO_MODE_OUTPUT);
						write_data_bus(data);

						// wait for PHI2 low
						while (PHI2_IS_HIGH) ;;

						set_data_bus_direction(GPIO_MODE_INPUT);
						serialPrintQueue(ANSI_YELLOW "PBI ROM Driver: Sent $\%02X from $\%04X to CPU\n" ANSI_RESET, data, address);
					} // Read-Only
					else
					{
						uint8_t data = read_data_bus(gpio_low);
						// If some code try to write at the rom space from $D800-$D8FF
						serialPrintQueue(ANSI_RED "PBI ROM Driver: Write $\%02X to ROM $\%04X IS DISABLED\n" ANSI_RESET, data, address);
					}
				}
				else
				{
					// Accessing Math Pack Area but outside the pbi_driver (256 over)
					// $D900-$DFFF: CPU try to writes/reads to shadow ROM MPD RAM space 
					fast_gpio_set_level(PIN_EXSEL, 0); // Set EXSEL low for external memory
					serialPrintQueue(ANSI_RED "MPD/EXSEL: Accessing DEVICE RAM\n" ANSI_RESET);
					if (!rw)
					{
						// We store data into buffer space from $D900 to $DFFF
						uint8_t data = read_data_bus(gpio_low);
						ram_d800[address - 0xD800] = data;
						serialPrintQueue(ANSI_YELLOW "PBI MPD/EXSEL Driver: Received $\%02X to $\%04X from CPU. Shadow RAM\n" ANSI_RESET, data, address);
					}
					else
					{
						// Read from shadow RAM $D900-$DFFF (2K - 256bytes)
						uint8_t data = ram_d800[ address - 0xD800 ];
						set_data_bus_direction(GPIO_MODE_OUTPUT);
						write_data_bus(data);
						
						// wait for PHI2 low
						while (PHI2_IS_HIGH) ;;
						
						set_data_bus_direction(GPIO_MODE_INPUT);
						serialPrintQueue(ANSI_YELLOW "PBI MPD/EXSEL Driver SHADOW RAM: Sent $\%02X to $\%04X to CPU\n" ANSI_RESET, data, address);
					}

					// Restore all MPD and EXSEL pins...
					fast_gpio_set_level(PIN_MPD, 1); // Set MPD high for normal operation
					fast_gpio_set_level(PIN_EXSEL, 1); // Set EXSEL high for internal memory
				}
			}
		}

		// D600-D7FF Shadow RAM -- Official Area
		// This is a special area for shadow RAM, used by the 6502 CPU
		// only if there is a PBI card connected and initialized 
		if (address >= 0xD600 && address <= 0xD7FF)
		{
			// Accessing the Shadow RAM Area D6xx-D7xx only if a device card is
			// selected by PBI bus protocol
			if (cardselected)
			{
				// the real hardware can drive the EXSEL pin to select external memory
				// only if a PBI card is connected and selected
				// 6502 CPU writes to Shadow RAM D600-D7FF
				fast_gpio_set_level(PIN_EXSEL, 0); // Set EXSEL low for accessing external PBI memory
				serialPrintQueue(ANSI_RED "EXSEL $D600-$D7FF PBI Device Shadow RAM\n" ANSI_RESET);

				// Shadow RAM D600-D6FF 256 bytes
				if (rw)
				{
					// CPU reads from shadow RAM
					if ((address - 0xD600) <= 0xFF) // 256 bytes window
					{
						uint8_t data = ram_d600[address - 0xD600];
						set_data_bus_direction(GPIO_MODE_OUTPUT);
						write_data_bus(data);

						// wait for PHI2 low
						while (PHI2_IS_HIGH) ;;

						set_data_bus_direction(GPIO_MODE_INPUT);
						serialPrintQueue(ANSI_YELLOW "EXSEL $D600-$D6FF PBI Device Shadow RAM: Sent $\%02X from $\%04X to CPU\n" ANSI_RESET, data, address);
					}
					else
					{
						// from $D700-$D7FF NOP AREA
						set_data_bus_direction(GPIO_MODE_OUTPUT);
						write_data_bus(0xEA); // 'NOP'

						// wait for PHI2 low
						while (PHI2_IS_HIGH) ;;

						set_data_bus_direction(GPIO_MODE_INPUT);
						serialPrintQueue(ANSI_YELLOW "EXSEL $D700-$D7FF PBI Shadow RAM $D7xx: Sent $EA (NOP) from $\%04X to CPU????\n" ANSI_RESET, address);
					}
				}
				else
				{
					// CPU writes to shadow RAM
					set_data_bus_direction(GPIO_MODE_INPUT);
					uint8_t data = read_data_bus(gpio_low);
					ram_d600[address - 0xD600] = data;
					serialPrintQueue(ANSI_YELLOW "EXSEL Write to Shadow RAM: Received $\%02X to $\%04X from CPU\n" ANSI_RESET, data, address);
				}

				// Restore EXSEL pin
				fast_gpio_set_level(PIN_EXSEL, 1); // Set EXSEL high for internal memory
			}
		}
	} // for(;;)
}

void loop(void)
{
	// Main loop does nothing, all work is done in MonitorTask & SerialTask
	// This is to keep the main loop responsive and free for other tasks
	vTaskDelay(pdMS_TO_TICKS(1)); // Yield to other tasks every 1 ms
}
