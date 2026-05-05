# Pin Mapping: VERA Module RBL-XE (ESP32-PICO-D4)

Questo documento definisce la mappatura dei segnali tra l'Atari XE (Bus PBI/ECI/CART) e il microcontroller ESP32-PICO-D4. 

## ⚠️ AVVERTENZE CRITICHE (Leggere prima del Design)

1.  **Flash Interna (NON COLLEGARE):** I pin GPIO 6, 7, 8, 11, 16, 17 (**Pin fisici 31, 32, 33, 30, 25, 27**) sono collegati internamente alla memoria flash. **Devono rimanere scollegati** nel PCB, altrimenti l'ESP32 non si avvierà.
2.  **Strapping Pins (Boot):** I pin GPIO 0, 2, 12 (**Pin fisici 23, 22, 18**) sono stati lasciati liberi per garantire che l'ESP32 si avvii sempre correttamente a 3.3V e in modalità normale.
3.  **Input-Only Pins:** I GPIO 34-39 sono solo ingressi. Sono stati usati per il bus degli indirizzi (A0-A5).

---

## 1. Bus Indirizzi (ATARI -> ESP32)
*Configurato per lettura veloce tramite registro `GPIO_IN1_REG` (GPIO 32-39).*

| Segnale ATARI | Pin Fisico ESP32 | GPIO | Tipo | Nota |
| :--- | :--- | :--- | :--- | :--- |
| **A0** | 5 | GPIO36 | Input | |
| **A1** | 6 | GPIO37 | Input | |
| **A2** | 7 | GPIO38 | Input | |
| **A3** | 8 | GPIO39 | Input | |
| **A4** | 10 | GPIO34 | Input | |
| **A5** | 11 | GPIO35 | Input | |
| **A6** | 12 | GPIO32 | Input | |
| **A7** | 13 | GPIO33 | Input | |

---

## 2. Bus Dati (Bidirezionale)
*Configurato per lettura/scrittura su registro `GPIO_IN_REG` / `GPIO_OUT_REG`.*

| Segnale ATARI | Pin Fisico ESP32 | GPIO | Tipo | Nota |
| :--- | :--- | :--- | :--- | :--- |
| **D0** | 24 | GPIO4 | I/O | |
| **D1** | 20 | GPIO13 | I/O | |
| **D2** | 17 | GPIO14 | I/O | |
| **D3** | 35 | GPIO18 | I/O | |
| **D4** | 38 | GPIO19 | I/O | |
| **D5** | 42 | GPIO21 | I/O | |
| **D6** | 39 | GPIO22 | I/O | |
| **D7** | 14 | GPIO25 | I/O | |

---

## 3. Segnali di Controllo (ECI / PBI / CART)

| Segnale ATARI | Pin Fisico ESP32 | GPIO | Tipo | Descrizione |
| :--- | :--- | :--- | :--- | :--- |
| **PHI2** | 36 | GPIO23 | Input | Clock di sistema (Sincronizzazione) |
| **R/_W** | 16 | GPIO27 | Input | Read (1) / Write (0) |
| **D1XX** | 29 | GPIO10 | Input | Selezione Area PBI $D1xx (Attivo Basso) |
| **CCTL** | 28 | GPIO9 | Input | Selezione Area CART $D5xx (Attivo Basso) |
| **VERA_CS** | 15 | GPIO26 | Output | Chip Select verso FPGA VERA |
| **EXSEL** | 21 | GPIO15 | Output | Selezione memoria esterna (ECI) |
| **MPD** | 34 | GPIO5 | Output | Math Pack Disable (PBI) |

---

## 4. Pin di Sistema (Debug / Programmazione)

| Funzione | Pin Fisico ESP32 | GPIO | Nota |
| :--- | :--- | :--- | :--- |
| **UART_TX** | 41 | GPIO1 | Seriale di Debug |
| **UART_RX** | 40 | GPIO3 | Seriale di Debug |
| **RESET** | 9 | CHIP_PU | Pulsante di Reset |

---

## 💡 PCB Design Hints & Best Practices

### 1. Level Shifting (Fondamentale)
Il bus ATARI lavora a **5V TTL**, mentre l'ESP32 lavora a **3.3V**.
- **Input (Indirizzi/Controllo):** Usa dei buffer/transceiver (es. 74LVC245 o 74LVC8T245) alimentati a 3.3V sul lato ESP32. Molti buffer LVC sono *5V tolerant* sugli ingressi se alimentati a 3.3V.
- **Bidirezionale (Dati):** È obbligatorio l'uso di transceiver bidirezionali (es. SN74LVC8T245) con doppia alimentazione (VCCA=3.3V, VCCB=5V). Il segnale **R/_W** dell'Atari deve pilotare il pin di direzione (DIR) dei transceiver del bus dati.

### 2. Integrità di Segnale
Il bus ATARI XE non è terminato e può soffrire di riflessioni, specialmente con cavi flat lunghi.
- **Resistenze di terminazione:** Inserisci delle resistenze serie da **22Ω - 47Ω** sulle linee PHI2, Data Bus e Address Bus vicino all'ESP32 per smorzare i ringing.
- **PHI2 (Clock):** Questo segnale è il cuore del sistema. Assicurati che la traccia sia il più corta possibile e lontana da segnali rumorosi.

### 3. Power Supply & Decoupling
L'ESP32-PICO-D4 può avere picchi di assorbimento di 500mA durante l'uso del Wi-Fi.
- **LDO:** Usa un regolatore da almeno 600mA-1A (es. AP2112K o AMS1117-3.3).
- **Condensatori:** Posiziona un condensatore da **10µF** (tantalio o ceramico) e uno da **100nF** (ceramico 0402/0603) il più vicino possibile ai pin di alimentazione del PICO-D4.
- **Ground Pad:** Il pad termico centrale dell'ESP32 (Pin 49) deve essere saldato a un ampio piano di massa con diversi *via* per la dissipazione del calore.

### 4. Gestione Flash Interna
- **Isolamento:** Le tracce dei pin 25, 27, 30, 31, 32, 33 non devono solo essere "non connesse" logicamente, ma fisicamente isolate. Non far passare segnali ad alta frequenza (come PHI2) sotto o vicino a questi pin per evitare cross-talk con la flash interna.

### 5. Layout Meccanico (ECI/CART)
- Se il modulo deve essere inserito direttamente negli slot Atari, ricorda che lo slot **ECI** e lo slot **Cartridge** sono affiancati ma con un offset meccanico preciso. Verifica le quote meccaniche dai blueprint originali Atari.
- Considera l'uso di un PCB a 4 layer per avere piani di massa e alimentazione dedicati, migliorando drasticamente la stabilità del bus decoder.
