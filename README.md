# ESP32 LED Matrix Display System (16×32)

An advanced LED matrix display controller for ESP32, designed for dynamic text display with support for Real-Time Clock (RTC), temperature sensing, and RS485 communication.

## 📋 Overview

This project is an ESP32-based controller for a 16×32 pixel LED matrix display (5 modules wide × 2 modules high). It implements the **M3 Serial Protocol** by Rousis Systems for receiving and displaying text messages with various effects and configurations.

**Developed by:** Rousis LTD  
**Device:** Matrix 32  
**Version:** 3.0  

## ✨ Features

- **Multi-Line Display**: Supports up to 4 lines of text (8 pixels per line)
  - Standard font: 5×7 or 7×7 pixels
  - Double-height font: 16 pixels (Big_font)
  - Mixed line types on the same screen

- **Display Effects**:
  - Static text (hold)
  - Scrolling text (slide)
  - Flashing animation
  - Automatic mode switching

- **Real-Time Information**:
  - DS1302 RTC integration for time/date display
  - DS18B20 temperature sensor support
  - Live updates for clock and sensor data

- **Communication**:
  - RS485 serial communication (250000 baud)
  - M3 Serial Protocol support
  - Addressable device system

- **Smart Features**:
  - Automatic brightness control via photoresistor
  - Message persistence (NVS storage)
  - Optional message timeout (5 minutes)
  - Test pattern mode
  - Dual-core task processing (ESP32)

## 🔧 Hardware Requirements

### Components

- **Microcontroller**: ESP32 Development Board
- **Display**: LED Matrix 16×16 modules (5×2 configuration = 80×32 pixels)
- **Real-Time Clock**: DS1302 RTC module
- **Temperature Sensor**: DS18B20 (1-Wire)
- **Communication**: RS485 transceiver module
- **Brightness Sensor**: Photoresistor (connected to GPIO 36)

### Pin Configuration

| Component | Pin | GPIO |
|-----------|-----|------|
| **RS485** | | |
| RX | RXD2 | GPIO 16 |
| TX | TXD2 | GPIO 17 |
| Direction | DIR | GPIO 5 |
| **DS1302 RTC** | | |
| Enable | ENA | GPIO 4 |
| Clock | CLK | GPIO 32 |
| Data | DAT | GPIO 2 |
| **DS18B20** | Data | GPIO 18 |
| **Photo Sensor** | Analog | GPIO 36 |
| **Matrix Control** | Enable | GPIO 26 |
| **Test Mode** | Input | GPIO 22 |
| **Status LED** | Output | GPIO 15 |

## 📦 Dependencies

The following libraries are required:

- Arduino.h
- Ds1302.h (DS1302 RTC library)
- RousisMatrix16_Static.h (Rousis LED Matrix driver)
- fonts/Big_font.h
- fonts/Big_font_2.h
- fonts/SystemFont5x7_greek.h
- fonts/greek_big_7x7.h
- Preferences.h (ESP32 NVS storage)
- OneWire.h (1-Wire protocol)
- DallasTemperature.h (DS18B20 sensor)

## 🚀 Installation

1. Clone the repository: `git clone https://github.com/yourusername/Matrix_normal_16X32.git`
2. Install required libraries via Arduino Library Manager or PlatformIO (custom libraries like RousisMatrix16_Static may need manual installation)
3. Configure your board: ESP32 Dev Module, Upload Speed: 115200, Flash Frequency: 80MHz, ensure Bluetooth is enabled
4. Open `Matrix_normal_16X32.ino` in Arduino IDE or Visual Studio with Visual Micro
5. Select your ESP32 board and COM port
6. Upload the sketch

## ⚙️ Configuration

### Display Settings
- MODULE_X: 5 (Number of horizontal modules)
- MODULE_Y: 2 (Number of vertical modules)
- PIXELS_X: 80 (5 × 16 pixels)
- PIXELS_Y: 32 (2 × 16 pixels)

### Communication
- BAUD_RATE: 250000 (RS485 baud rate)
- Address: 1 (Device address, 0 = broadcast)

### Timeouts & Sampling
- MESSAGE_TIMEOUT_S: 5 minutes (300000 ms)
- TEMP_SAMPLE_DELAY: 5000 ms (Temperature reading every 5s)
- PHOTO_SAMPLE_DELAY: 1000 ms (Brightness sampling every 1s)

## 📡 M3 Serial Protocol

### Packet Structure
Header: 0x55 0xAA [Address] 0xA1 0x02  
Page: 0x01 [Delay] [Function_Byte] [Text Data] 0x00  
Footer: 0x04

### Function Byte (per line)
- Bit 7: Bold (ignored)
- Bit 6: Flash effect
- Bit 5: Double-height line
- Bits 4-0: Mode (00=hold, 01=slide, 03=auto)

### Special Commands
- 0x05: Line separator (next line follows)
- 0xD6: Font select ('0' or '1')
- 0xDA: Insert clock (HH:MM)
- 0xDB: Insert date (DD/MM/YY)
- 0xDC: Insert temperature (XX.X C)
- 0xE0: Set brightness (next byte)

### RTC Time Setting
Send packet with command 0xA2 followed by 7 BCD bytes: [Seconds] [Minutes] [Hours] [Day_of_Week] [Day] [Month] [Year]

## 💡 Usage Examples

### Displaying Static Text
Send via RS485: `55 AA 01 A1 02 01 05 00 "Hello World" 00 04`

### Clock Display
`55 AA 01 A1 02 01 03 20 DA 00 04` (Shows time in double-height font)

### Temperature Display
`55 AA 01 A1 02 01 03 00 DC 00 04`

### Multi-Line Mixed Display
`55 AA 01 A1 02 01 05 20 "Title" 05 00 "Subtitle" 00 04` (First line double-height, second line normal)

## 🧪 Test Mode

Hold GPIO 22 LOW during boot to enter test pattern mode. The display will cycle through various test patterns (full screen, vertical lines, horizontal lines, checkerboards).

## 🔍 Troubleshooting

### Display not working
- Check power supply (LED matrices require significant current)
- Verify GPIO pin connections
- Enable Bluetooth support in ESP32 configuration

### RTC shows wrong time
- RTC battery may be depleted
- Send time-setting command via RS485
- Check if RTC is halted (serial monitor will indicate)

### Temperature not displaying
- Verify DS18B20 connection and 4.7kΩ pull-up resistor
- Check serial monitor for sensor count (should be ≥1)

### Brightness issues
- Check photoresistor connection to GPIO 36
- Adjust brightness sampling if needed
- Manual brightness can be sent via protocol (0xE0 command)

## 📄 License

This project is proprietary software developed by Rousis LTD. Please contact the manufacturer for licensing information.

## 🤝 Contributing

For bug reports or feature requests, please contact Rousis Systems or submit issues through the repository.

## 📞 Contact

**Rousis LTD**  
Manufacturer of LED Display Systems

---

*Last Updated: Based on firmware version 3.0 (2026)*