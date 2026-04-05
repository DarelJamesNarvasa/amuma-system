#include <Arduino.h>

int sensorPin = A0;
int ledPin = 13;
int sensorValue = 0;

void setup() {
    Serial.begin(9600);
    pinMode(ledPin, OUTPUT);
}

void loop() {
    sensorValue = analogRead(sensorPin);
    if (sensorValue >70){
    digitalWrite(ledPin, HIGH);
    }

    else{
    digitalWrite(ledPin, LOW);
    }
}