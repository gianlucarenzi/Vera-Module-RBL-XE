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

#define TEST
#undef TEST

#ifdef TEST
// Definizione delle coppie di pin da testare: { Pin_Output, Pin_Input }
// Assicurati di collegare fisicamente questi pin tra loro con un cavo jumper.
// I pin sul lato destro (Input) verranno testati leggendo il valore
// scritto sul pin di sinistra (Output).

// Attenzione: i pin 34, 35, 36, 39 sono solo INPUT. Il loro partner sul
// lato sinistro DEVE essere un pin configurabile come OUTPUT.

const int testPairs[][2] = {
	{2, 4},   // D2, D4
	{5, 12},  // D5, D12
	{13, 14}, // D13, D14
	{15, 18}, // D15, D18
	{19, 21}, // D19, D21
	{22, 23}, // D22, D23
	{25, 26}, // D25, D26
	{27, 32}, // D27, D32
	{33, 34}, // D33, D34 (D34 è solo INPUT)
	{17, 35}, // TX2, D35 (D35 è solo INPUT)
	{16, 39}  // RX2, VN (VN/GPIO39 è solo INPUT)
};

const int numPairs = sizeof(testPairs) / sizeof(testPairs[0]);

// Funzione per verificare se un pin è solo input
bool isOnlyInputPin(int pin)
{
	return (pin == 34 || pin == 35 || pin == 36 || pin == 39);
}

void setup(void)
{
	Serial.begin(115200); // Inizializza la comunicazione seriale
	while (!Serial);      // Attendi che la porta seriale sia disponibile

	Serial.println(ANSI_BLUE "--- Avvio Test Automatico GPIO ESP32 ---" ANSI_RESET);
	Serial.println(ANSI_YELLOW "Assicurati che i pin siano fisicamente collegati a coppie come definito nel codice.");
	Serial.println("-----------------------------------------------------------------------------------" ANSI_RESET);
	Serial.println("Configurazione delle coppie:");
	for (int i = 0; i < numPairs; i++)
	{
		Serial.print(ANSI_YELLOW "  Pair "); Serial.print(i + 1); Serial.print(": Output GPIO"); Serial.print(testPairs[i][0]);
		Serial.print(" <-> Input GPIO" ANSI_RESET); Serial.println(testPairs[i][1]);
	}
	Serial.println("---------------------------------------");
	delay(2000); // Breve pausa prima di iniziare i test
}

void loop(void)
{
	static unsigned long lastTestTime = 0;
	const unsigned long testInterval = 5000; // Intervallo tra cicli di test completi (5 secondi)

	if (millis() - lastTestTime >= testInterval)
	{
		lastTestTime = millis();
		Serial.println(ANSI_BLUE "\n--- Inizio un nuovo ciclo di test ---" ANSI_RESET);
		int failedTests = 0;

		for (int i = 0; i < numPairs; i++)
		{
			int outputPin = testPairs[i][0];
			int inputPin = testPairs[i][1];

			Serial.print(ANSI_YELLOW "Testing Pair: Output GPIO");
			Serial.print(outputPin);
			Serial.print(" <-> Input GPIO");
			Serial.print(inputPin);
			Serial.print(" -- " ANSI_RESET);

			// Salta il test di output se il pin di output è un pin solo input (errore di configurazione)
			if (isOnlyInputPin(outputPin))
			{
				Serial.println(ANSI_RED "ERRORE: Pin " + String(outputPin) + " (Output) è un pin SOLO INPUT. Questa coppia non può funzionare come previsto." ANSI_RESET);
				failedTests++;
				continue; // Passa alla prossima coppia
			}

			// Configura i pin
			pinMode(outputPin, OUTPUT);
			pinMode(inputPin, INPUT); // Non usiamo PULLUP qui perché il pin è pilotato attivamente

			bool pairFailed = false;

			// --- Test HIGH ---
			digitalWrite(outputPin, HIGH);
			delay(10); // Breve ritardo per stabilizzare il segnale
			int readStateHigh = digitalRead(inputPin);

			if (readStateHigh == HIGH)
			{
				//Serial.println(ANSI_GREEN "HIGH Test: PASS" ANSI_RESET); // Troppo verboso, solo in caso di fallimento
			}
			else
			{
				Serial.println(ANSI_RED "FAIL (HIGH Test): Expected HIGH on GPIO" + String(inputPin) + ", but got LOW." ANSI_RESET);
				pairFailed = true;
			}

			// --- Test LOW ---
			digitalWrite(outputPin, LOW);
			delay(10); // Breve ritardo per stabilizzare il segnale
			int readStateLow = digitalRead(inputPin);

			if (readStateLow == LOW)
			{
				//Serial.println(ANSI_GREEN "LOW Test: PASS" ANSI_RESET); // Troppo verboso, solo in caso di fallimento
			}
			else
			{
				Serial.println(ANSI_RED "FAIL (LOW Test): Expected LOW on GPIO" + String(inputPin) + ", but got HIGH." ANSI_RESET);
				pairFailed = true;
			}

			if (!pairFailed)
			{
				Serial.println(ANSI_GREEN "PASS." ANSI_RESET);
			}
			else
			{
				failedTests++;
			}

			// Disabilita i pin per la prossima iterazione, o mettili in uno stato neutro
			// pinMode(outputPin, INPUT); // Rimettere come input previene consumi o conflitti futuri
			// pinMode(inputPin, INPUT);
		}

		Serial.println(ANSI_BLUE "--- Ciclo di test completato ---" ANSI_RESET);
		if (failedTests == 0)
		{
			Serial.println(ANSI_GREEN "Tutti i test delle coppie sono PASSATI!" ANSI_RESET);
		}
		else
		{
			Serial.print(ANSI_RED "Numero di test FALLITI: ");
			Serial.println(failedTests);
			Serial.println("Verifica i collegamenti o i pin indicati come falliti." ANSI_RESET);
		}

		Serial.println(ANSI_YELLOW "Prossimo test tra " + String(testInterval / 1000) + " secondi." ANSI_RESET);
	}
}

#else // NON TEST

#define SERIAL_QUEUE_LENGTH 32 // Length of the serial queue
#define SERIAL_MSG_SIZE 128 // Size of the serial message buffer

// Pin Configuration 

// Pin Definitions for 6502 Bus Monitor
// Address Bus A0-A15

#define PROTOTYPE 1 // Define PROTOTYPE (some lines are not available)
//#define PROTOTYPE 0 // Final Revision Board

#if PROTOTYPE == 0

// A0-A15 are only on the final revision board

// LSB Address Pins

constexpr gpio_num_t PIN_A0 = GPIO_NUM_6;	// D6
constexpr gpio_num_t PIN_A1 = GPIO_NUM_8;	// D8
constexpr gpio_num_t PIN_A6 = GPIO_NUM_38;	// D38
constexpr gpio_num_t PIN_A7 = GPIO_NUM_37;	// D37

// Decoded selection signals are only on the final revision board
constexpr gpio_num_t PIN_EXSEL = GPIO_NUM_11;	// When low external memory is selected, when high internal memory is selected
constexpr gpio_num_t PIN_D1XX = GPIO_NUM_10;	// When accessing PBI I/O memory space
constexpr gpio_num_t PIN_CCTL = GPIO_NUM_20;	// When accessing Cartridge Control $D500 CCTL
constexpr gpio_num_t PIN_MPD = GPIO_NUM_7;		// Math Pack ROM Disable

#endif // PROTOTYPE == 0

// Common LSB Address Pins
constexpr gpio_num_t PIN_A2 = GPIO_NUM_21;	// D21
constexpr gpio_num_t PIN_A3 = GPIO_NUM_27;	// D27
constexpr gpio_num_t PIN_A4 = GPIO_NUM_33;	// D33
constexpr gpio_num_t PIN_A5 = GPIO_NUM_32;	// D32

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
// These pins are used for data transfer on the 6502 bus
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

// Shadow RAM 256 Bytes
static char ram_d600[256] = {0};

static uint8_t d500[256] = {0}; // CCTL memory space
static uint8_t cardselected = 0; // Flag to indicate if a card is selected
static uint8_t DEVICE_ID = 0x01; // Example device ID for PBI I/O
extern const uint8_t pbi_driver[]; // PBI driver memory space

#define DBG_ERROR   0
#define DBG_INFO    1
#define DBG_VERBOSE 2
#define DBG_NOISY   3

static int debuglevel = DBG_INFO;


#if PROTOTYPE == 1
	#define CARTRIDGE_CONTROL   (address >= 0xD500 && address <= 0xD5FF)
	#define PBI_IO              (address >= 0xD100 && address <= 0xD1FF)
#else
	#define CARTRIDGE_CONTROL   (!read_gpio_level(PIN_CCTL))
	#define PBI_IO              (!read_gpio_level(PIN_D1XX))
#endif


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

inline bool read_gpio_level(gpio_num_t pin)
{
	if(pin < 32) {
		return (read_gpio_low() >> pin) & 1;
	} else {
		return (read_gpio_high() >> (pin - 32)) & 1;
	}
}

static inline uint16_t read_address_bus(void)
{
    uint32_t gpio_low = read_gpio_low();
    uint32_t gpio_high = read_gpio_high();

#if PROTOTYPE == 0
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
#else
    return (((gpio_low >> PIN_A2) & 1) << 2) |
           (((gpio_low >> PIN_A3) & 1) << 3) |
           (((gpio_high >> (PIN_A4 - 32)) & 1) << 4) |
           (((gpio_high >> (PIN_A5 - 32)) & 1) << 5) |
           (((gpio_low >> PIN_A8) & 1) << 8) |
           (((gpio_low >> PIN_A9) & 1) << 9) |
           (((gpio_low >> PIN_A10) & 1) << 10) |
           (((gpio_low >> PIN_A11) & 1) << 11) |
           (((gpio_high >> (PIN_A12 - 32)) & 1) << 12) |
           (((gpio_high >> (PIN_A13 - 32)) & 1) << 13) |
           (((gpio_high >> (PIN_A14 - 32)) & 1) << 14) |
           (((gpio_high >> (PIN_A15 - 32)) & 1) << 15);
#endif
}

static inline uint8_t read_data_bus(void)
{
    uint32_t gpio_low = read_gpio_low();
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

void setup(void)
{
	Serial.begin(115200);

	serialQueue = xQueueCreate(SERIAL_QUEUE_LENGTH, SERIAL_MSG_SIZE);
	xTaskCreatePinnedToCore(SerialTask, "SerialTask", 2048, NULL, 1, NULL, 0); // Create the serial task on core 0
	xTaskCreatePinnedToCore(MonitorTask, "MonitorTask", 2048, NULL, 1, NULL, 1); // Create the monitor task on core 1

	// Configurazione pin
	gpio_config_t io_conf = {};
	// ADDRESS BUS A0-A15

#if PROTOTYPE == 1
		// ADDRESS BUS LSB on prototype board (only A2-A5)
		// A0 and A1 are not available on the prototype board
		// A6 and A7 are not available on the prototype board
	io_conf.pin_bit_mask =
		(1ULL << PIN_A2) | (1ULL << PIN_A3) | (1ULL << PIN_A4) | (1ULL << PIN_A5);
#else
	// Read as much Address Lines as possible
	io_conf.pin_bit_mask =
		(1ULL << PIN_A0) | (1ULL << PIN_A1) | (1ULL << PIN_A2) | (1ULL << PIN_A3) |
		(1ULL << PIN_A4) | (1ULL << PIN_A5) | (1ULL << PIN_A6) | (1ULL << PIN_A7);
#endif
	// Common Address pins (PROTOTYPE & BOARD)
	io_conf.pin_bit_mask |=
		// ADDRESS BUS MSB A8-A15
		(1ULL << PIN_A8) | (1ULL << PIN_A9) | (1ULL << PIN_A10) | 
		(1ULL << PIN_A11) | (1ULL << PIN_A12) | (1ULL << PIN_A13) |
		(1ULL << PIN_A14) | (1ULL << PIN_A15);

	// PHI2 & RW
	io_conf.pin_bit_mask |= (1ULL << PIN_PHI2) | (1ULL << PIN_RW);
	io_conf.mode = GPIO_MODE_INPUT;
	gpio_config(&io_conf);

	// Configurazione pin dati come input/output (per ora come input)
	io_conf.pin_bit_mask = (1ULL << PIN_D0) | (1ULL << PIN_D1) | (1ULL << PIN_D2) |
						   (1ULL << PIN_D3) | (1ULL << PIN_D4) | (1ULL << PIN_D5) |
						   (1ULL << PIN_D6) | (1ULL << PIN_D7);
	gpio_config(&io_conf);

#if PROTOTYPE == 0
	// Configurazione pin segnali aggiuntivi come output oppure input
	io_conf.pin_bit_mask = (1ULL << PIN_EXSEL) | (1ULL << PIN_MPD);
	io_conf.mode = GPIO_MODE_OUTPUT;
	gpio_config(&io_conf);

	io_conf.pin_bit_mask = 0; // Reset pin_bit_mask
	io_conf.pin_bit_mask |= (1ULL << PIN_CCTL);
	io_conf.pin_bit_mask |= (1ULL << PIN_D1XX);
	io_conf.mode = GPIO_MODE_INPUT;
	gpio_config(&io_conf);
#endif

	// Configurazione CS come output
	gpio_set_direction(PIN_CS, GPIO_MODE_OUTPUT);
	gpio_set_level(PIN_CS, 1);

}

void MonitorTask(void *pvParameters)
{
	serialPrintQueue(ANSI_BLUE "6502 Bus Monitor Ready on Core 1\n" ANSI_RESET);

	for (;;)
	{
		// Rising edge of PHI2
		while (! read_gpio_level(PIN_PHI2))
			;;

		uint16_t address = read_address_bus();
		bool rw = read_gpio_level(PIN_RW);

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
				while ( read_gpio_level(PIN_PHI2) )
					;;
				set_data_bus_direction(GPIO_MODE_INPUT);
				serialPrintQueue(ANSI_YELLOW "CCTL: Send $\%02X from $\%04X to CPU\n" ANSI_RESET, data, address);
			}
			else
			{
				// 6502 CPU is asking to write to memory @ $D5xx ST[X/Y/A]
				set_data_bus_direction(GPIO_MODE_INPUT);
				uint8_t data = read_data_bus();
				d500[address & 0x00FF] = data;
				serialPrintQueue(ANSI_YELLOW "CCTL: Received $\%02X to $\%04X from CPU\n" ANSI_RESET, data, address);
			}
		}

		// Gestione PBI I/O
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
					uint8_t device = read_data_bus();
					if (device == DEVICE_ID)
					{
						serialPrintQueue(ANSI_BLUE "PBI I/O: Device $\%02X selected\n" ANSI_RESET, device);
						cardselected = 1;
						gpio_set_level(PIN_CS, 0); // Set CS low for PBI I/O
					}
					else
					{
						serialPrintQueue(ANSI_RED "PBI I/O: Invalid device $\%02X selected.\n" ANSI_RESET, device);
						cardselected = 0;
						gpio_set_level(PIN_CS, 1); // Set CS high for no device selected
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
					while ( read_gpio_level(PIN_PHI2) )
						;;
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

		// Gestione EXSEL e MPD
		if(address >= 0xD800 && address <= 0xDFFF)
		{
			if (cardselected)
			{
			#if PROTOTYPE == 0 
				gpio_set_level(PIN_MPD, 0); // Set MPD low to indicate Math Pack ROM Disable
			#endif
				serialPrintQueue(ANSI_YELLOW "EXSEL: Set to external memory, MPD: Disabled" ANSI_RESET);
				// ROM (READ ONLY)
				if (rw)
				{
					// D800-DFFF: CPU read from PBI Driver and lower MPD
					uint8_t data = pbi_driver[ address - 0xD800 ];
					set_data_bus_direction(GPIO_MODE_OUTPUT);
					write_data_bus(data);
					// wait for PHI2 low
					while ( read_gpio_level(PIN_PHI2) )
						;;
					set_data_bus_direction(GPIO_MODE_INPUT);
					serialPrintQueue(ANSI_YELLOW "PBI ROM Driver: Sent $\%02X from $\%04X to CPU\n" ANSI_RESET, data, address);
				}
				else
				{
					// D800-DFFF: CPU try to writes to PBI Driver?
				#if PROTOTYPE == 0 
					gpio_set_level(PIN_EXSEL, 0); // Set EXSEL low for external memory
				#endif
					serialPrintQueue(ANSI_RED "PBI OSROM Driver: Why Write to ROM? $\%04X\n" ANSI_RESET, address);
					uint8_t data = read_data_bus();
					// Store the data in the shadow RAM of PBI OSROM Driver!
					ram_d800[address - 0xD800] = data;
					serialPrintQueue(ANSI_YELLOW "PBI OSROM Driver: Received $\%02X to $\%04X from CPU. Shadow RAM?\n" ANSI_RESET, data, address);
				}

				// Restore all MPD and EXSEL pins...
			#if PROTOTYPE == 0
				gpio_set_level(PIN_MPD, 1); // Set MPD high for normal operation
				gpio_set_level(PIN_EXSEL, 1); // Set EXSEL high for internal memory
			#endif
			}
		}

		// D600-D7FF Shadow RAM
		// This is a special area for shadow RAM, used by the 6502 CPU
		// only if there is a PBI card connected and initialized 
		if (address >= 0xD600 && address <= 0xD7FF)
		{
			// Accessing the Shadow RAM Area D6xx-D7xx only if a device card is
			// selected by PBI bus protocol
			if (cardselected)
			{
			#if PROTOTYPE == 0
				// the real hardware can drive the EXSEL pin to select external memory
				// only if a PBI card is connected and selected
				// 6502 CPU writes to Shadow RAM D600-D7FF
				gpio_set_level(PIN_EXSEL, 0); // Set EXSEL low for accessing external PBI memory
			#endif

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
						while ( read_gpio_level(PIN_PHI2) )
							;;
						set_data_bus_direction(GPIO_MODE_INPUT);
						serialPrintQueue(ANSI_YELLOW "PBI Shadow RAM: Sent $\%02X from $\%04X to CPU\n" ANSI_RESET, data, address);
					}
					else
					{
						set_data_bus_direction(GPIO_MODE_OUTPUT);
						write_data_bus(0xEA); // 'NOP'
						// wait for PHI2 low
						while ( read_gpio_level(PIN_PHI2) )
							;;
						set_data_bus_direction(GPIO_MODE_INPUT);
						serialPrintQueue(ANSI_YELLOW "PBI Shadow RAM $D7xx: Sent $EA (NOP) from $\%04X to CPU????\n" ANSI_RESET, address);
					}
				}
				else
				{
					// CPU writes to shadow RAM
					set_data_bus_direction(GPIO_MODE_INPUT);
					uint8_t data = read_data_bus();
					ram_d600[address - 0xD600] = data;
					serialPrintQueue(ANSI_YELLOW "Shadow RAM: Received $\%02X to $\%04X from CPU\n" ANSI_RESET, data, address);
				}

				// Restore EXSEL pin
			#if PROTOTYPE == 0
				gpio_set_level(PIN_EXSEL, 1); // Set EXSEL high for internal memory
			#endif
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
#endif
