#include <Arduino.h>
#include "HeartSpeed.h"

// ===== PIN DEFINITIONS =====
#define MIC_PIN A0           // Sound sensor
#define AD8232_PIN A0        // AD8232 ECG sensor (conflicts with MIC_PIN)
#define HEARTRATE_PIN A1     // Heart rate sensor
#define LDT0_SENSOR_PIN A0   // LDT0-028K piezo sensor (conflicts with MIC_PIN)
#define AD8232_LO_PLUS 10    // AD8232 lead-off detection +
#define AD8232_LO_MINUS 11   // AD8232 lead-off detection -
#define LDT0_LED_PIN 13      // LDT0-028K LED indicator

// ===== SENSOR OBJECTS & VARIABLES =====
HeartSpeed heartspeed(HEARTRATE_PIN);      // Heart rate sensor
unsigned long lastSend = 0;
const unsigned long interval = 100;        // Send every 100 ms

int ad8232Value = 0;
int heartRate = 0;
int ldtSensorValue = 0;
int soundPeakToPeak = 0;

// ===== HEARTRATE CALLBACK =====
void heartRateCallback(uint8_t rawData, int value) {
  if (rawData) {
    heartRate = value;
  } else {
    heartRate = value;
  }
}

void setup() {
  Serial.begin(115200);
  
  // AD8232 setup
  pinMode(AD8232_LO_PLUS, INPUT);
  pinMode(AD8232_LO_MINUS, INPUT);
  
  // LDT0-028K setup
  pinMode(LDT0_LED_PIN, OUTPUT);
  
  // Heart rate sensor setup
  heartspeed.setCB(heartRateCallback);
  heartspeed.begin();
  
  Serial.println("All sensors initialized");
}

void loop() {
  if (millis() - lastSend >= interval) {
    lastSend = millis();

    // ===== AD8232 ECG SENSOR =====
    ad8232Value = 0;
    if ((digitalRead(AD8232_LO_PLUS) == 1) || (digitalRead(AD8232_LO_MINUS) == 1)) {
      ad8232Value = -1; // Lead-off detected
    } else {
      ad8232Value = analogRead(AD8232_PIN);
    }

    // ===== LDT0-028K PIEZO SENSOR =====
    ldtSensorValue = analogRead(LDT0_SENSOR_PIN);
    if (ldtSensorValue > 70) {
      digitalWrite(LDT0_LED_PIN, HIGH);
    } else {
      digitalWrite(LDT0_LED_PIN, LOW);
    }

    // ===== SOUND SENSOR =====
    int signalMax = 0;
    int signalMin = 1023;
    unsigned long startTime = millis();

    // Sample for 50 ms to get sound peak
    while (millis() - startTime < 50) {
      int sample = analogRead(MIC_PIN);
      if (sample > signalMax) signalMax = sample;
      if (sample < signalMin) signalMin = sample;
    }
    soundPeakToPeak = signalMax - signalMin;

    // ===== SEND ALL SENSOR DATA AS JSON =====
    Serial.print("{\"ad8232\":");
    Serial.print(ad8232Value);
    Serial.print(",\"heartrate\":");
    Serial.print(heartRate);
    Serial.print(",\"ldt0\":");
    Serial.print(ldtSensorValue);
    Serial.print(",\"sound\":");
    Serial.print(soundPeakToPeak);
    Serial.print(",\"time\":");
    Serial.print(millis());
    Serial.println("}");
  }
}
