#include <Keypad.h>
#include <ESP32Servo.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <RTClib.h>
#include "TOTP.h"
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <Preferences.h>
#include "time.h"

// --- SETTINGS ---
const char* ssid = "Networkk";
const char* password = "Passworddd";
const char* syncUrl = "http://10.111.111.68:5000/api/sync_users";


#define SERVO_PIN 13

// --- KEYPAD ---
const byte ROWS = 4; 
const byte COLS = 4; 
char keys[ROWS][COLS] = {
  {'1','2','3','A'}, 
  {'4','5','6','3'},
  {'7','8','9','0'}, 
  {'*','0','#','6'}
};
byte rowPins[ROWS] = {19, 18, 5, 17}; 
byte colPins[COLS] = {16, 4, 2, 15}; 
Keypad keypad = Keypad(makeKeymap(keys), rowPins, colPins, ROWS, COLS);

Preferences preferences;
Adafruit_SSD1306 display(128, 64, &Wire, -1);
RTC_DS3231 rtc;
Servo lockServo;

struct UserData {
  String name;
  String secretBase32;
};
UserData currentUsers[5];
int userCount = 0;
String inputCode = "";


void displayMsg(String line1, String line2) {
  display.clearDisplay();
  display.setTextSize(2);
  display.setTextColor(WHITE);
  display.setCursor(0, 10);
  display.println(line1);
  display.setTextSize(1);
  display.println(line2);
  display.display();
}

void updateDisplay(String line1, String line2) {
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(WHITE);
  DateTime now = rtc.now();
  display.setCursor(85, 0);
  display.printf("%02d:%02d", now.hour(), now.minute());
  display.setCursor(0, 20);
  display.setTextSize(2);
  display.println(line1);
  display.setTextSize(1);
  display.println(line2);
  display.display();
}

String formatCode(char* code) {
  String s = String(code);
  while (s.length() < 6) s = "0" + s;
  return s;
}

int base32_decode(const char* base32, uint8_t* out) {
  int bits = 0, val = 0, len = 0;
  for (int i = 0; base32[i]; i++) {
    int c = toupper(base32[i]);
    if (c == '=') break;
    if (c >= 'A' && c <= 'Z') val = (val << 5) | (c - 'A');
    else if (c >= '2' && c <= '7') val = (val << 5) | (c - '2' + 26);
    else continue;
    bits += 5;
    if (bits >= 8) {
      out[len++] = (val >> (bits - 8)) & 0xFF;
      bits -= 8;
    }
  }
  return len;
}

// --- CORE LOGIC ---

void syncWithServer() {
  displayMsg("WiFi ON", "Syncing...");
  WiFi.begin(ssid, password);
  
  int counter = 0;
  while (WiFi.status() != WL_CONNECTED && counter < 20) { 
    delay(500); 
    counter++; 
  }

  if (WiFi.status() == WL_CONNECTED) {
      // SET TO UTC (Offset 0)
      configTime(0, 0, "pool.ntp.org"); 
      
      struct tm timeinfo;
      if (getLocalTime(&timeinfo)) {
          rtc.adjust(DateTime(timeinfo.tm_year + 1900, timeinfo.tm_mon + 1, 
                              timeinfo.tm_mday, timeinfo.tm_hour, 
                              timeinfo.tm_min, timeinfo.tm_sec));
          Serial.println("RTC Clock Updated to UTC!");
      }

      HTTPClient http;
      http.begin(syncUrl);
      int httpCode = http.GET();
      
      if (httpCode == 200) {
        String payload = http.getString();
        DynamicJsonDocument doc(2048);
        deserializeJson(doc, payload);
        JsonArray arr = doc.as<JsonArray>();
        
        preferences.begin("lock-data", false);
        preferences.clear();
        userCount = 0;
        for (JsonObject u : arr) {
          if (userCount < 5) {
            currentUsers[userCount].name = u["name"].as<String>();
            currentUsers[userCount].secretBase32 = u["secret"].as<String>();
            preferences.putString(("n"+String(userCount)).c_str(), currentUsers[userCount].name);
            preferences.putString(("s"+String(userCount)).c_str(), currentUsers[userCount].secretBase32);
            userCount++;
          }
        }
        preferences.putInt("count", userCount);
        preferences.end();
        displayMsg("SUCCESS", "Time & Users Synced!");
      } else {
        displayMsg("ERR Server", "Code: " + String(httpCode));
      }
      http.end();
  } else {
    displayMsg("ERR WiFi", "Check Connection");
  }
  
  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);
  delay(2000);
  updateDisplay("Ready", "Enter Code:");
}

void verifyCode(String entered) {
  displayMsg("Checking", "");
  while (entered.length() < 6) entered = "0" + entered;

  long nowTs = rtc.now().unixtime();
  bool success = false;
  String userName = "";

  for (int i = 0; i < userCount; i++) {
    uint8_t secretBytes[32];
    int secretLen = base32_decode(currentUsers[i].secretBase32.c_str(), secretBytes);
    TOTP totp(secretBytes, secretLen);

    String currentCode = formatCode(totp.getCode(nowTs));
    String previousCode = formatCode(totp.getCode(nowTs - 30));

    Serial.print("User: "); Serial.println(currentUsers[i].name);
    Serial.print("ESP32 (UTC): "); Serial.println(currentCode);
    Serial.print("Entered: "); Serial.println(entered);

    if (entered == currentCode || entered == previousCode) {
      success = true;
      userName = currentUsers[i].name;
      break;
    }
  }
  
  if (success) {
    lockServo.write(90);
    displayMsg("WELCOME", userName);
    delay(3000);
    lockServo.write(0);
  } 
  else {
    displayMsg("DENIED", "Invalid Code");
    delay(2000);
  }
  updateDisplay("Ready", "Enter Code:");
}

void setup() {
  Serial.begin(115200);
  lockServo.attach(SERVO_PIN);
  lockServo.write(0);

  if(!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) for(;;);
  if (!rtc.begin()) {
     displayMsg("RTC ERR", "Check Wiring");
     while(1);
  }

  preferences.begin("lock-data", true);
  userCount = preferences.getInt("count", 0);
  for (int i = 0; i < userCount; i++) {
    currentUsers[i].name = preferences.getString(("n" + String(i)).c_str(), "");
    currentUsers[i].secretBase32 = preferences.getString(("s" + String(i)).c_str(), "");
  }
  preferences.end();

  WiFi.mode(WIFI_OFF);
  updateDisplay("Ready", "Enter Code:");
}

void loop() {
  char key = keypad.getKey();
  if (key) {
    if (key == 'A') {
      syncWithServer();
    } else if (key == '*' || key == '#') {
      inputCode = "";
      updateDisplay("Cleared", "Enter Code:");
    } else if (key >= '0' && key <= '9') {
      inputCode += key;
      updateDisplay("Input:", inputCode);
      if (inputCode.length() == 6) {
        verifyCode(inputCode);
        inputCode = "";
      }
    }
  }
}