#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <WiFiManager.h>
#include <EEPROM.h>
#include "time.h"

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET -1
#define I2C_SDA 6
#define I2C_SCL 7
#define EEPROM_SIZE 512
#define CONFIG_BUTTON_PIN 5
#define MODE_BUTTON_PIN 4

#define MODE_GLUCOSE 0
#define MODE_TIMER 1

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);
WiFiClientSecure client;

const char* ntpServer = "pool.ntp.org";
const long  gmtOffset_sec = 7200;
const int   daylightOffset_sec = 3600;

struct Settings {
  char nightscout_url[100];
  char api_secret[100];
  bool configured;
  time_t last_insulin_time;
  float last_insulin_amount;
  bool timer_active;
} settings;

const unsigned long UPDATE_INTERVAL = 10000;
unsigned long lastUpdate = 0;
unsigned long lastTimerUpdate = 0;
unsigned long buttonPressTime = 0;
int displayMode = MODE_GLUCOSE;

#define INPUT_STATE_NORMAL 0
#define INPUT_STATE_AMOUNT 1

int inputState = INPUT_STATE_NORMAL;
float insulin_input = 0.0;


String getTrendArrow(String direction) {
  if (direction == "DoubleUp" || direction == "DOUBLE_UP") return "++";
  if (direction == "SingleUp" || direction == "SINGLE_UP") return "+";
  if (direction == "FortyFiveUp" || direction == "FORTY_FIVE_UP") return "+";
  if (direction == "Flat" || direction == "FLAT") return "...";
  if (direction == "FortyFiveDown" || direction == "FORTY_FIVE_DOWN") return "-";
  if (direction == "SingleDown" || direction == "SINGLE_DOWN") return "-";
  if (direction == "DoubleDown" || direction == "DOUBLE_DOWN") return "--";
  return "-";
}

void configureWiFi() {
  WiFiManager wm;
  wm.setDebugOutput(true);

  
  WiFiManagerParameter custom_nightscout_url("nightscout", "Nightscout URL (без https://)", settings.nightscout_url, 100);
  WiFiManagerParameter custom_api_secret("api_secret", "API Secret", settings.api_secret, 100);

  wm.addParameter(&custom_nightscout_url);
  wm.addParameter(&custom_api_secret);

  display.clearDisplay();
  display.setTextSize(1);
  display.setCursor(0, 0);
  display.println("Connect to WiFi:");
  display.println("'ESP32_Setup'");
  display.println("Go: 192.168.4.1");
  display.display();

  
  if (wm.startConfigPortal("ESP32_Setup", "12345678")) {
    
    const char* new_url = custom_nightscout_url.getValue();
    const char* new_secret = custom_api_secret.getValue();

    Serial.println("[DEBUG] New settings:");
    Serial.println("[DEBUG] URL: " + String(new_url));
    Serial.println("[DEBUG] Secret: " + String(new_secret));

    
    if (strlen(new_url) == 0 || strlen(new_secret) == 0) {
      Serial.println("[ERROR] Empty values not allowed");
      display.clearDisplay();
      display.setCursor(0, 0);
      display.println("Error: Empty");
      display.println("values!");
      display.display();
      delay(2000);
      ESP.restart();
      return;
    }

    
    memset(settings.nightscout_url, 0, sizeof(settings.nightscout_url));
    memset(settings.api_secret, 0, sizeof(settings.api_secret));
    strncpy(settings.nightscout_url, new_url, sizeof(settings.nightscout_url) - 1);
    strncpy(settings.api_secret, new_secret, sizeof(settings.api_secret) - 1);
    settings.configured = true;

    EEPROM.put(0, settings);
    if (EEPROM.commit()) {
      Serial.println("[DEBUG] Settings saved to EEPROM");
      Serial.println("[DEBUG] Saved URL: " + String(settings.nightscout_url));
      Serial.println("[DEBUG] Saved Secret length: " + String(strlen(settings.api_secret)));
    } else {
      Serial.println("[ERROR] EEPROM commit failed");
    }

    display.clearDisplay();
    display.setCursor(0, 0);
    display.println("Settings saved!");
    display.display();
    delay(2000);
  } else {
    Serial.println("[ERROR] Config portal failed");
    display.clearDisplay();
    display.setCursor(0, 0);
    display.println("Config failed!");
    display.println("Try again...");
    display.display();
    delay(2000);
  }
}

void displayGlucose(float sgv, String direction) {
  display.clearDisplay();

  
  display.fillRect(0, 0, 128, 16, SSD1306_BLACK);
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(1);
  display.setCursor(16, 4);
  display.print("GLUCOSE LEVEL");

  
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(4);
  display.setCursor(0, 20);
  char glucoseStr[6];
  dtostrf(sgv, 3, 1, glucoseStr);
  display.print(glucoseStr);

  
  display.setTextSize(3);
  display.setCursor(0, 48);
  display.print(getTrendArrow(direction));
}

void displayIOBCOB(float iob, float cob) {
  display.clearDisplay();

  
  display.fillRect(0, 0, 128, 16, SSD1306_WHITE);
  display.setTextColor(SSD1306_BLACK);
  display.setTextSize(1);
  display.setCursor(16, 4);
  display.print("INSULIN & CARBS");
  
  
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(2);
  display.setCursor(0, 20);
  display.print("IOB:");
  display.setCursor(0, 36);
  char iobStr[8];
  dtostrf(iob, 1, 2, iobStr);  // 2 знака после запятой для инсулина
  display.print(iobStr);
  
  
  display.setCursor(64, 20);
  display.print("COB:");
  display.setCursor(64, 36);
  char cobStr[8];
  dtostrf(cob, 1, 1, cobStr);  // 1 знак после запятой для углеводов
  display.print(cobStr);
}

void displayInsulinInput() {
  display.clearDisplay();
  
  // Заголовок
  display.fillRect(0, 0, 128, 16, SSD1306_BLACK);
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(1);
  display.setCursor(16, 4);
  display.print("INSULIN AMOUNT");
  
  display.setTextSize(3);
  display.setCursor(20, 24);
  char amountStr[6];
  dtostrf(insulin_input, 3, 1, amountStr);
  display.print(amountStr);
  
  display.setTextSize(1);
  display.setCursor(0, 48);
  display.print("CONFIG: +0.5");
  display.setCursor(0, 56);
  display.print("MODE: Confirm");
}

void displayTimer() {
  display.clearDisplay();

  display.fillRect(0, 0, 128, 16, SSD1306_BLACK);
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(1);
  display.setCursor(16, 4);
  display.print("INSULIN TIMER");
  
  if (!settings.timer_active) {
    display.setTextSize(2);
    display.setCursor(8, 24);
    display.print("No data");
  } else {
    display.setTextSize(1);
    display.setCursor(0, 20);
    char amountStr[10];
    sprintf(amountStr, "%.1f U", settings.last_insulin_amount);
    display.print(amountStr);
    
    display.setTextSize(2);
    display.setCursor(8, 32);
    
    time_t now;
    time(&now);
    
    int seconds = difftime(now, settings.last_insulin_time);
    int hours = seconds / 3600;
    int minutes = (seconds % 3600) / 60;
    seconds = seconds % 60;
    
    char timeStr[9];
    sprintf(timeStr, "%02d:%02d:%02d", hours, minutes, seconds);
    display.print(timeStr);
  }
  
  display.setTextSize(1);
  display.setCursor(0, 56);
  display.print("Hold MODE: new dose");
}

String formatTime(unsigned long milliseconds) {
  unsigned long seconds = milliseconds / 1000;
  unsigned long hours = seconds / 3600;
  unsigned long minutes = (seconds % 3600) / 60;
  seconds = seconds % 60;
  
  char timeStr[9];
  sprintf(timeStr, "%02lu:%02lu:%02lu", hours, minutes, seconds);
  return String(timeStr);
}

void updateNightscoutData() {
  if (WiFi.status() == WL_CONNECTED) {
    HTTPClient https;
    
    if (strlen(settings.nightscout_url) == 0 || strlen(settings.api_secret) == 0) {
      display.clearDisplay();
      display.setCursor(0, 0);
      display.setTextSize(2);
      display.println("Error");
      display.setTextSize(1);
      display.println("\nNo config");
      display.println("Hold button");
      display.display();
      return;
    }
    
    String baseUrl = "https://";
    if (strncmp(settings.nightscout_url, "https://", 8) == 0) {
      baseUrl = settings.nightscout_url;
    } else {
      baseUrl += String(settings.nightscout_url);
    }
    
    if (baseUrl.endsWith("/")) {
      baseUrl = baseUrl.substring(0, baseUrl.length() - 1);
    }

    if (displayMode == MODE_GLUCOSE) {
      String url = baseUrl + "/api/v1/entries.json?count=1";
      
      Serial.println("\n[DEBUG] Mode: GLUCOSE");
      Serial.println("[DEBUG] URL: " + url);
      
      if (!https.begin(client, url)) {
        display.clearDisplay();
        display.setCursor(0, 0);
        display.setTextSize(2);
        display.println("Error");
        display.setTextSize(1);
        display.println("HTTPS init");
        display.display();
        return;
      }
      
      https.addHeader("api-secret", settings.api_secret);
      int httpResponseCode = https.GET();
      
      if (httpResponseCode > 0) {
        String payload = https.getString();
        Serial.println("[DEBUG] Response: " + payload);
        
        DynamicJsonDocument doc(2048);
        DeserializationError error = deserializeJson(doc, payload);
        
        if (!error && doc.size() > 0) {
          float sgv = doc[0]["sgv"].as<float>() / 18.0;
          String direction = doc[0]["direction"].as<String>();
          displayGlucose(sgv, direction);
          display.display();
        } else {
          Serial.println("[ERROR] Parse error: " + String(error.c_str()));
          display.clearDisplay();
          display.setCursor(0, 0);
          display.setTextSize(2);
          display.println("Error");
          display.setTextSize(1);
          display.println("Bad glucose");
          display.display();
        }
      }
      https.end();
    } else if (displayMode == MODE_TIMER) {
      displayTimer();
      display.display();
    } else {
      String url = baseUrl + "/api/v1/devicestatus.json?count=1";
      
      Serial.println("\n[DEBUG] Mode: IOB/COB");
      Serial.println("[DEBUG] URL: " + url);
      
      if (!https.begin(client, url)) {
        display.clearDisplay();
        display.setCursor(0, 0);
        display.setTextSize(2);
        display.println("Error");
        display.setTextSize(1);
        display.println("HTTPS init");
        display.display();
        return;
      }
      
      https.addHeader("api-secret", settings.api_secret);
      int httpResponseCode = https.GET();
      
      float iob = 0;
      float cob = 0;
      bool dataFound = false;
      
      if (httpResponseCode > 0) {
        String payload = https.getString();
        Serial.println("\n[DEBUG] Raw devicestatus response:");
        Serial.println(payload);
        
        DynamicJsonDocument doc(16384);
        DeserializationError error = deserializeJson(doc, payload);
        
        if (!error && doc.size() > 0) {
          JsonObject status = doc[0];
          
          Serial.println("\n[DEBUG] JSON parsing successful");
          Serial.println("[DEBUG] First status object keys:");
          for(JsonPair kv : status) {
            Serial.print(kv.key().c_str());
            Serial.print(" ");
          }
          Serial.println();
          
          if (status.containsKey("openaps")) {
            JsonObject openaps = status["openaps"];
            Serial.println("\n[DEBUG] Found openaps section");
            
            if (openaps.containsKey("iob")) {
              Serial.println("[DEBUG] Found iob section in openaps");
              JsonObject iobData = openaps["iob"];
              if (iobData.containsKey("iob")) {
                iob = iobData["iob"].as<float>();
                dataFound = true;
                Serial.println("[DEBUG] Found OpenAPS IOB: " + String(iob));
              }
            }
            
            if (openaps.containsKey("meal")) {
              Serial.println("[DEBUG] Found meal section in openaps");
              JsonObject meal = openaps["meal"];
              if (meal.containsKey("mealCOB")) {
                cob = meal["mealCOB"].as<float>();
                dataFound = true;
                Serial.println("[DEBUG] Found OpenAPS COB: " + String(cob));
              }
            }
          }
          
          if (!dataFound && status.containsKey("pump")) {
            JsonObject pump = status["pump"];
            Serial.println("\n[DEBUG] Found pump section");
            
            if (pump.containsKey("iob")) {
              iob = pump["iob"].as<float>();
              dataFound = true;
              Serial.println("[DEBUG] Found Pump IOB: " + String(iob));
            }
            
            if (pump.containsKey("cob")) {
              cob = pump["cob"].as<float>();
              dataFound = true;
              Serial.println("[DEBUG] Found Pump COB: " + String(cob));
            }
          }
          
          if (dataFound) {
            displayIOBCOB(iob, cob);
            display.display();
          } else {
            Serial.println("[ERROR] No IOB/COB data found in JSON");
            display.clearDisplay();
            display.setCursor(0, 0);
            display.setTextSize(2);
            display.println("Error");
            display.setTextSize(1);
            display.println("No IOB/COB");
            display.println("data found");
            display.display();
          }
        } else {
          Serial.println("[ERROR] JSON parse error: " + String(error.c_str()));
          display.clearDisplay();
          display.setCursor(0, 0);
          display.setTextSize(2);
          display.println("Error");
          display.setTextSize(1);
          display.println("Bad data");
          display.display();
        }
      } else {
        Serial.println("[ERROR] HTTP Error: " + String(httpResponseCode));
        display.clearDisplay();
        display.setCursor(0, 0);
        display.setTextSize(2);
        display.println("Error");
        display.setTextSize(1);
        display.println("HTTP " + String(httpResponseCode));
        display.display();
      }
      https.end();
    }
  } else {
    display.clearDisplay();
    display.setCursor(0, 0);
    display.setTextSize(2);
    display.println("Error");
    display.setTextSize(1);
    display.println("\nNo WiFi");
    display.display();
  }
}

void saveSettings() {
  EEPROM.begin(EEPROM_SIZE);
  EEPROM.put(0, settings);
  EEPROM.commit();
  EEPROM.end();
}

void startInsulinTimer(float amount) {
  settings.last_insulin_amount = amount;
  time(&settings.last_insulin_time);
  settings.timer_active = true;
  saveSettings();
  
  display.clearDisplay();
  display.setTextSize(2);
  display.setCursor(0, 16);
  display.print("Started!");
  display.setTextSize(1);
  display.setCursor(0, 40);
  char confirmStr[20];
  sprintf(confirmStr, "Amount: %.1f U", amount);
  display.print(confirmStr);
  display.display();
  delay(2000);
}

void setup() {
  Serial.begin(115200);
  Serial.println("\n[DEBUG] Starting setup...");
  
  pinMode(CONFIG_BUTTON_PIN, INPUT_PULLUP);
  pinMode(MODE_BUTTON_PIN, INPUT_PULLUP);
  Serial.println("[DEBUG] Buttons configured");

  EEPROM.begin(EEPROM_SIZE);

  Wire.begin(I2C_SDA, I2C_SCL);
  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println("SSD1306 allocation failed");
    return;
  }

  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);

  EEPROM.get(0, settings);

  Serial.println("[DEBUG] Checking config button state...");
  if (digitalRead(CONFIG_BUTTON_PIN) == LOW) {
    Serial.println("[DEBUG] Config button is pressed!");
    display.clearDisplay();
    display.setCursor(0, 0);
    display.println("Release for");
    display.println("config mode");
    display.display();

    while (digitalRead(CONFIG_BUTTON_PIN) == LOW) {
      delay(10);
    }
    Serial.println("[DEBUG] Config button released, entering config mode");
    configureWiFi();
    ESP.restart();
    return;
  }
  Serial.println("[DEBUG] Config button not pressed");

  if (!settings.configured) {
    configureWiFi();
    ESP.restart();
    return;
  }

  display.clearDisplay();
  display.setCursor(0, 0);
  display.println("Connecting WiFi...");
  display.display();

  WiFi.begin();

  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 20) {
    delay(500);
    attempts++;
    Serial.print(".");
  }

  if (WiFi.status() != WL_CONNECTED) {
    display.clearDisplay();
    display.setCursor(0, 0);
    display.println("WiFi failed!");
    display.println("Press button");
    display.println("to configure");
    display.display();
    return;
  }

  Serial.println("\n[DEBUG] Connected to WiFi");
  Serial.println("[DEBUG] SSID: " + WiFi.SSID());
  Serial.println("[DEBUG] IP: " + WiFi.localIP().toString());

  client.setInsecure();

  configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
  
  display.clearDisplay();
  display.setCursor(0, 0);
  display.println("Syncing time...");
  display.display();
  
  while (time(nullptr) < 1000000000) {
    delay(500);
    display.print(".");
    display.display();
  }
  
  display.clearDisplay();
  display.setTextSize(1);
  display.setCursor(0, 0);
  display.println("Time synced!");
  display.display();
  delay(1000);

  // Сразу делаем первое обновление данных
  updateNightscoutData();
}

void loop() {
  Serial.println("\n[DEBUG] ========= LOOP START =========");
  
  if (inputState == INPUT_STATE_AMOUNT) {
    bool configButtonState = digitalRead(CONFIG_BUTTON_PIN);
    bool modeButtonState = digitalRead(MODE_BUTTON_PIN);
    
    if (configButtonState == LOW) {
      delay(50);
      if (digitalRead(CONFIG_BUTTON_PIN) == LOW) {
        insulin_input += 0.5;
        if (insulin_input > 20.0) insulin_input = 0.0;
        displayInsulinInput();
        display.display();
        while(digitalRead(CONFIG_BUTTON_PIN) == LOW) delay(10);
      }
    }
    
    if (modeButtonState == LOW) {
      delay(50);
      if (digitalRead(MODE_BUTTON_PIN) == LOW) {
        startInsulinTimer(insulin_input);
        insulin_input = 0.0;
        inputState = INPUT_STATE_NORMAL;
        displayMode = MODE_TIMER;
      }
    }
    
  } else {
    static bool lastModeState = HIGH;
    bool modeButtonState = digitalRead(MODE_BUTTON_PIN);
    static unsigned long modeButtonPressTime = 0;
    
    if (modeButtonState == LOW) {
      if (modeButtonPressTime == 0) {
        modeButtonPressTime = millis();
      } else if (millis() - modeButtonPressTime > 2000 && displayMode == MODE_TIMER) {
        inputState = INPUT_STATE_AMOUNT;
        insulin_input = 0.0;
        displayInsulinInput();
        display.display();
        while(digitalRead(MODE_BUTTON_PIN) == LOW) delay(10);
        modeButtonPressTime = 0;
        return;
      }
    } else {
      if (modeButtonPressTime > 0 && millis() - modeButtonPressTime < 2000) {
        displayMode = !displayMode;
        if (displayMode == MODE_GLUCOSE) {
          updateNightscoutData();
        }
      }
      modeButtonPressTime = 0;
    }
    lastModeState = modeButtonState;

    bool configButtonState = digitalRead(CONFIG_BUTTON_PIN);
    static unsigned long configButtonPressTime = 0;
    
    if (configButtonState == LOW) {
      if (configButtonPressTime == 0) {
        configButtonPressTime = millis();
      } else if (millis() - configButtonPressTime > 3000) {
        display.clearDisplay();
        display.setCursor(0, 0);
        display.println("Release for");
        display.println("config mode");
        display.display();

        while (digitalRead(CONFIG_BUTTON_PIN) == LOW) delay(10);
        configureWiFi();
        ESP.restart();
        return;
      }
    } else {
      configButtonPressTime = 0;
    }

    if (WiFi.status() == WL_CONNECTED && displayMode == MODE_GLUCOSE) {
      if (millis() - lastUpdate >= UPDATE_INTERVAL) {
        updateNightscoutData();
        lastUpdate = millis();
      }
    } else if (displayMode == MODE_TIMER) {
      static unsigned long lastDisplayUpdate = 0;
      unsigned long currentMillis = millis();
      
      if (currentMillis - lastDisplayUpdate >= 1000) {
        displayTimer();
        display.display();
        lastDisplayUpdate = currentMillis;
        
        static unsigned long lastSaveTime = 0;
        if (currentMillis - lastSaveTime >= 60000) {
          saveSettings();
          lastSaveTime = currentMillis;
        }
      }
    }
  }

  delay(50);
}