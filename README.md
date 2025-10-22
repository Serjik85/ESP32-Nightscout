# GlucoTrack ESP32-C3  
**Real-time glucose display for LibreLinkUp & Nightscout**

![OLED Example](docs/screen-example.jpg)

GlucoTrack is a small ESP32-C3 based device with an OLED screen that displays live glucose levels retrieved either from **LibreLinkUp** or **Nightscout**.  
It’s designed to be a **simple, always-visible companion** for parents and people with diabetes who need to monitor glucose data in real time — without constantly checking their phone.

---

## ✨ Features

- 🔄 **Dual data source** – switch between:
  - `LibreLinkUp` (via Abbott API)
  - `Nightscout` (via REST API)
- ⚙️ **WiFiManager configuration portal**
  - Enter Libre/Nightscout credentials directly from your phone or laptop
  - Hidden advanced fields (`src`, `TZ offset`, etc.)
- 🖥️ **OLED display (SSD1306 128x64)**
  - Clear glucose values and trend arrows
  - Multi-screen interface (Glucose, Dose, Log)
- 💡 **Animated startup and status**
  - Splash screen: “GlucoTrack”
  - Smooth “connecting…” animation while waiting for Wi-Fi or data
- 🔘 **Physical button control**
  - `NEXT` (GPIO4): short press - add +0.5U, long press - reset dose  
  - `ACT` (GPIO5): short press - switch screen  
  - Hold **both buttons for 2+ seconds** to enter configuration mode
- 🧠 **Non-volatile storage**
  - Saves Wi-Fi and API settings using ESP32 `Preferences`
- 🔔 **Bolus logging**
  - Send manual bolus records to Nightscout

---

## 🧩 Hardware

| Component | Description |
|------------|-------------|
| MCU | ESP32-C3 (DevKitM-1 or compatible) |
| Display | SSD1306 OLED 128x64 (I2C) |
| SDA | GPIO6 |
| SCL | GPIO7 |
| Button NEXT | GPIO4 |
| Button ACT | GPIO5 |
| Power | 5V / USB-C |

---

## ⚙️ Setup (Arduino IDE)

### 1. Install Dependencies
Use **Tools → Manage Libraries…** and install:
- `WiFiManager` by tzapu  
- `ArduinoJson` by Benoit Blanchon  
- `Adafruit GFX Library`  
- `Adafruit SSD1306`

### 2. Board Settings
Tools → Board → ESP32 Arduino → ESP32C3 Dev Module
Upload Speed: 460800 or 921600
Partition Scheme: Default or Huge App
### 3. Flash the Firmware
1. Connect your ESP32-C3 via USB.  
2. Open `ESP32-Nightscout.ino`.  
3. Click **Upload**.

### 4. Configure the Device
1. On first boot, the device creates a Wi-Fi hotspot: **GlucoTrack_Setup**
2. Connect to it (no password).  
3. If the portal doesn’t open automatically, go to **http://192.168.4.1**.  
4. Enter:
   - Libre email / password (if using Libre)
   - Nightscout URL and API secret (if using Nightscout)
   - Timezone offset (e.g., `120`)
   - Choose data source (`libre` or `night`)
5. Save and reboot — device starts fetching data.

---

## 🔋 Operation

| Action | Description |
|--------|--------------|
| Boot | Splash screen “GlucoTrack” + loading animation |
| No Wi-Fi | “connecting Wi-Fi…” animation |
| No data | “waiting data…” animation |
| Short press `ACT` | Switch between screens |
| Short press `NEXT` | Add +0.5U dose |
| Long press `NEXT` | Reset dose |
| Hold both buttons (2s) | Open setup portal |

---

## 💾 Configuration Storage

Stored using `Preferences` under namespace `"gluco"`:

| Key | Description |
|-----|--------------|
| `lle` | Libre email |
| `llp` | Libre password |
| `api` | API base |
| `nsu` | Nightscout URL |
| `nss` | Nightscout API secret |
| `src` | Data source (`libre` or `night`) |
| `tzo` | Timezone offset (minutes) |

---

## 📸 Screenshots

| Mode | Example |
|------|----------|
| Glucose | ![Glucose Screen](docs/screen-glu.jpg) |
| Dose | ![Dose Screen](docs/screen-dose.jpg) |
| Log | ![Log Screen](docs/screen-log.jpg) |
| Setup Hint | ![Setup Hint](docs/screen-setup.jpg) |

---

## 🧠 Technical Notes

- LibreLinkUp communication uses official Abbott API endpoints:
  - `/llu/auth/login`
  - `/llu/connections`
- Nightscout integration uses `/api/v1/entries.json?count=1`
- Device auto-reconnects to Wi-Fi and restores saved configuration
- Built with ESP32 `Preferences` and `WiFiManager`
- Not a medical device — for informational and hobby use only

---

## 🧰 Development

To build from source:
```bash
git clone https://github.com/Serjik85/ESP32-Nightscout.git
cd ESP32-Nightscout
Then open in Arduino IDE or PlatformIO.

Required Libraries:
WiFiManager
ArduinoJson
Adafruit GFX Library
Adafruit SSD1306
