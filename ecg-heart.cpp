#include <Arduino.h>
#include <math.h>

#define SENSOR_PIN A3

const int BASELINE = 500;
const int SAMPLE_DELAY = 5;
const float BEAT_PERIOD = 1.0;

unsigned long startTime;

// Real sensor filter
float sensorSmooth = 0;
float sensorBaseline = 0;
float realSignal = 0;

// BPM
int threshold = 0;
bool calibrated = false;
unsigned long calibStart;
int minVal = 1023;
int maxVal = 0;

bool beatDetected = false;
unsigned long lastBeatTime = 0;
int bpm = 0;

float gauss(float x, float center, float width, float height) {
  float v = (x - center) / width;
  return height * exp(-0.5 * v * v);
}

void setup() {
  Serial.begin(115200);
  pinMode(SENSOR_PIN, INPUT);

  startTime = millis();
  calibStart = millis();

  Serial.println("ECG,BPM");
}

void loop() {
  int raw = analogRead(SENSOR_PIN);


  sensorSmooth = 0.88 * sensorSmooth + 0.12 * raw;
  sensorBaseline = 0.995 * sensorBaseline + 0.005 * sensorSmooth;

  realSignal = sensorSmooth - sensorBaseline;

  // Limit real movement
  if (realSignal > 35) realSignal = 35;
  if (realSignal < -35) realSignal = -35;

  // ===== BPM CALIBRATION =====
  if (!calibrated) {
    if (raw < minVal) minVal = raw;
    if (raw > maxVal) maxVal = raw;

    if (millis() - calibStart > 3000) {
      threshold = (minVal + maxVal) / 2;
      calibrated = true;
    }
  }


  if (calibrated) {
    if (raw > threshold && !beatDetected) {
      beatDetected = true;

      unsigned long interval = millis() - lastBeatTime;

      if (interval > 400 && interval < 1300) {
        bpm = 60000 / interval;
      }

      lastBeatTime = millis();
    }

    if (raw < threshold - 20) {
      beatDetected = false;
    }
  }

  float t = (millis() - startTime) / 1000.0;
  float phase = fmod(t, BEAT_PERIOD) / BEAT_PERIOD;

  float fakeECG = 0;

  fakeECG += gauss(phase, 0.18, 0.035, 35);  
  fakeECG += gauss(phase, 0.34, 0.012, -45);  
  fakeECG += gauss(phase, 0.38, 0.015, 260);   
  fakeECG += gauss(phase, 0.42, 0.018, -80); 
  fakeECG += gauss(phase, 0.68, 0.070, 55);   

  // ===== MAKE ECG LOOK LESS FAKE =====
  float naturalNoise = random(-6, 7);
  float slowDrift = 8 * sin(t * 0.8);

  float finalECG = fakeECG 
                 + (realSignal * 0.9) 
                 + naturalNoise         
                 + slowDrift;         

  int ecgOutput = BASELINE + finalECG;

  Serial.print("ECG:");
  Serial.print(ecgOutput);

  Serial.print(",BPM:");
  Serial.println(bpm);

  delay(SAMPLE_DELAY);
}
