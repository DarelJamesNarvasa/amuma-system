#include <Arduino.h>

#define MIC_PIN A0

void setup() {
  Serial.begin(9600);
}

void loop() {
  int signalMax = 0;
  int signalMin = 1023;

  unsigned long startTime = millis();

  // Sample for 50 ms
  while (millis() - startTime < 50) {
    int sample = analogRead(MIC_PIN);

    if (sample < 1023) {
      if (sample > signalMax) {
        signalMax = sample;
      }
      if (sample < signalMin) {
        signalMin = sample;
      }
    }
  }

  int peakToPeak = signalMax - signalMin;

  Serial.print("Sound Level: ");
  Serial.println(peakToPeak);

  delay(50);
}
