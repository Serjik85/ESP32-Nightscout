# ESP32 Nightscout Display

A compact ESP32-based device that displays real-time glucose data from your Nightscout server on a small OLED display.

## Features

- Real-time glucose level display with trend arrows
- IOB (Insulin on Board) and COB (Carbs on Board) monitoring
- WiFi configuration via WiFiManager
- Persistent settings storage in EEPROM
- Two-button interface for mode switching and configuration
- Compact OLED display with high contrast

## Hardware Requirements

- ESP32 development board
- SSD1306 OLED display (128x64)
- 2 push buttons
- Breadboard and jumper wires

## Pin Configuration

- OLED SDA: GPIO 6
- OLED SCL: GPIO 7
- Config Button: GPIO 5
- Mode Button: GPIO 4

## Installation

1. Clone this repository
2. Open the project in Arduino IDE
3. Install required libraries:
   - WiFiManager
   - ArduinoJson
   - Adafruit_GFX
   - Adafruit_SSD1306
4. Upload the sketch to your ESP32

## First Time Setup

1. Power on the device
2. Connect to the "ESP32 Nightscout" WiFi network
3. Open 192.168.4.1 in your browser
4. Enter your WiFi credentials and Nightscout URL
5. The device will restart and connect to your Nightscout server

## Usage

- Short press Mode button to switch between Glucose and IOB/COB display
- Long press Config button (3 seconds) to enter configuration mode
- Device automatically updates data every 10 seconds

## Contributing

This repository has branch protection rules to maintain code quality and stability:

1. The `main` branch is protected and requires pull request reviews
2. Direct pushes to `main` are not allowed
3. To contribute:
   - Fork the repository
   - Create a new branch for your feature: `git checkout -b feature/your-feature-name`
   - Make your changes
   - Push to your fork: `git push origin feature/your-feature-name`
   - Create a Pull Request from your fork to our `main` branch
4. All pull requests must be reviewed and approved before merging
5. For major changes, please open an issue first to discuss what you would like to change

## License

[MIT](https://choosealicense.com/licenses/mit/)
