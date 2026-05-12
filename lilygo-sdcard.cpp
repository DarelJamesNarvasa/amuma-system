#include <Arduino.h>
#include <ArduinoJson.h>
#include <SPI.h>
#include <SD.h>
#include <WiFi.h>
#include <time.h>

// ================= WIFI =================
const char* WIFI_SSID = "NARVASA_Wifi";
const char* WIFI_PASSWORD = "@Narvasa96";

// ================= NTP =================
const char* ntpServer = "pool.ntp.org";
const long gmtOffset_sec = 8 * 3600; // Philippines GMT+8
const int daylightOffset_sec = 0;

// ================= SD CARD =================
#define SD_MISO 2
#define SD_MOSI 15
#define SD_SCLK 14
#define SD_CS   13

// ================= BUILT-IN LED =================
#define LED_PIN 12

// ================= UART =================
#define RX_PIN 3
#define TX_PIN 1

HardwareSerial ArduinoSerial(0);

// ================= FILE =================
String fileName = "/arduino_data.csv";

String jsonBuffer = "";
bool receivingJson = false;
int braceCount = 0;

// ================= FUNCTIONS =================
void connectWiFi();
String getTimeStamp();
void createCSVHeader();
void saveJsonToSD(String jsonData);
void blinkLED();

void setup() {
  Serial.begin(115200);
  delay(1000);

  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW);

  Serial.println();
  Serial.println("LilyGO JSON Receiver + SD Logger");

  // ================= UART =================
  ArduinoSerial.begin(38400, SERIAL_8N1, RX_PIN, TX_PIN);

  Serial.println("UART Started on RX=3 TX=1");

  // ================= WIFI =================
  connectWiFi();

  // ================= NTP =================
  configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);

  Serial.println("Waiting for NTP time...");

  struct tm timeinfo;

  while (!getLocalTime(&timeinfo)) {
    Serial.print(".");
    delay(500);
  }

  Serial.println();
  Serial.println("NTP Time Synced!");

  // ================= SD CARD =================
  SPI.begin(SD_SCLK, SD_MISO, SD_MOSI, SD_CS);

  if (!SD.begin(SD_CS)) {
    Serial.println("SD Card Mount Failed!");
    return;
  }

  Serial.println("SD Card Ready!");

  createCSVHeader();
}

void loop() {

  while (ArduinoSerial.available()) {

    char c = ArduinoSerial.read();

    // Detect JSON start
    if (c == '{') {

      if (!receivingJson) {
        jsonBuffer = "";
        receivingJson = true;
        braceCount = 0;
      }

      braceCount++;
      jsonBuffer += c;
    }

    // Continue receiving JSON
    else if (receivingJson) {

      jsonBuffer += c;

      if (c == '}') {

        braceCount--;

        if (braceCount == 0) {

          receivingJson = false;

          jsonBuffer.trim();

          Serial.println();
          Serial.println("Received JSON:");
          Serial.println(jsonBuffer);

          saveJsonToSD(jsonBuffer);

          jsonBuffer = "";
        }
      }

      // Safety reset
      if (jsonBuffer.length() > 700) {
        Serial.println("JSON too long. Buffer cleared.");

        jsonBuffer = "";
        receivingJson = false;
        braceCount = 0;
      }
    }
  }
}

// ================= WIFI =================
void connectWiFi() {

  Serial.print("Connecting WiFi");

  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  Serial.println();
  Serial.println("WiFi Connected!");
  Serial.println(WiFi.localIP());
}

// ================= GET REAL TIME =================
String getTimeStamp() {

  struct tm timeinfo;

  if (!getLocalTime(&timeinfo)) {
    return "NO_TIME";
  }

  char timeStringBuff[30];

  strftime(
    timeStringBuff,
    sizeof(timeStringBuff),
    "%Y-%m-%d %H:%M:%S",
    &timeinfo
  );

  return String(timeStringBuff);
}

// ================= CSV HEADER =================
void createCSVHeader() {

  if (!SD.exists(fileName)) {

    File file = SD.open(fileName, FILE_WRITE);

    if (file) {

      file.println(
        "datetime,ecg,heartRate,heartRateStatus,respiratoryRate,lungSound,micP2P,micZCR,micSpikeRate,respSignal"
      );

      file.close();

      Serial.println("CSV header created.");
    }
  }
  else {
    Serial.println("CSV file already exists.");
  }
}

// ================= SAVE JSON =================
void saveJsonToSD(String jsonData) {

  StaticJsonDocument<768> doc;

  DeserializationError error = deserializeJson(doc, jsonData);

  if (error) {

    Serial.print("JSON Parse Failed: ");
    Serial.println(error.c_str());

    return;
  }

  String realTime = getTimeStamp();

  int ecg = doc["ecg"] | 0;
  int heartRate = doc["heartRate"] | 0;

  const char* heartRateStatus =
    doc["heartRateStatus"] | "UNKNOWN";

  float respiratoryRate =
    doc["respiratoryRate"] | 0.0;

  const char* lungSound =
    doc["lungSound"] | "UNKNOWN";

  float micP2P =
    doc["raw"]["micP2P"] | 0.0;

  float micZCR =
    doc["raw"]["micZCR"] | 0.0;

  float micSpikeRate =
    doc["raw"]["micSpikeRate"] | 0.0;

  float respSignal =
    doc["raw"]["respSignal"] | 0.0;

  File file = SD.open(fileName, FILE_APPEND);

  if (!file) {
    Serial.println("Failed to open CSV file.");
    return;
  }

  file.print(realTime);
  file.print(",");

  file.print(ecg);
  file.print(",");

  file.print(heartRate);
  file.print(",");

  file.print(heartRateStatus);
  file.print(",");

  file.print(respiratoryRate, 2);
  file.print(",");

  file.print(lungSound);
  file.print(",");

  file.print(micP2P, 2);
  file.print(",");

  file.print(micZCR, 4);
  file.print(",");

  file.print(micSpikeRate, 4);
  file.print(",");

  file.println(respSignal, 2);

  file.close();

  Serial.println("Saved to SD card!");

  blinkLED();
}

// ================= LED BLINK =================
void blinkLED() {

  digitalWrite(LED_PIN, HIGH);
  delay(80);

  digitalWrite(LED_PIN, LOW);
}
