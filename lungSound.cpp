/*
  MAX9814 + Stethoscope Tube Lung Sound Classifier
  Fixed 3-State Version

  Output:
    - WEAK LUNG SOUND
    - NORMAL LUNG SOUND
    - ABNORMAL LUNG SOUND
*/

#include <Arduino.h>

#define MIC_PIN A0

// ----------------------------
// Timing
// ----------------------------
const unsigned long CALIBRATION_MS = 8000;
const unsigned long WINDOW_MS = 60;
const unsigned long REPORT_MS = 250;

// ----------------------------
// History sizes
// ----------------------------
const int FEATURE_HISTORY = 20;

// ----------------------------
// Feature buffers
// ----------------------------
float p2pHist[FEATURE_HISTORY];
float energyHist[FEATURE_HISTORY];
float zcrHist[FEATURE_HISTORY];
float envHist[FEATURE_HISTORY];
int histIndex = 0;
bool histFilled = false;

// ----------------------------
// Calibration values
// ----------------------------
float baseP2P = 0.0;
float baseEnergy = 0.0;
float baseZCR = 0.0;
float baseEnv = 0.0;

// ----------------------------
// Thresholds
// ----------------------------
float minSignalP2P = 10.0;
float minSignalEnergy = 2.5;

float weakEnvHigh = 60.0;
float weakEnergyHigh = 55.0;

float normalEnvLow = 70.0;
float normalEnvHigh = 220.0;

float normalEnergyLow = 70.0;
float normalEnergyHigh = 180.0;

float highNoiseZCR = 0.22;
float lowNoiseZCR = 0.01;
float unstableRatioLimit = 1.90;
float veryUnstableLimit = 2.80;

// ----------------------------
float smoothedEnvelope = 0.0;
const float ENV_ALPHA = 0.08;

float dcEstimate = 512.0;
const float DC_ALPHA = 0.01;

unsigned long lastReport = 0;

// ==================================================
float avgArr(const float *arr, int size) {
  float s = 0;
  for (int i = 0; i < size; i++) s += arr[i];
  return s / size;
}

float minArr(const float *arr, int size) {
  float m = arr[0];
  for (int i = 1; i < size; i++) {
    if (arr[i] < m) m = arr[i];
  }
  return m;
}

float maxArr(const float *arr, int size) {
  float m = arr[0];
  for (int i = 1; i < size; i++) {
    if (arr[i] > m) m = arr[i];
  }
  return m;
}

int histCount() {
  return histFilled ? FEATURE_HISTORY : histIndex;
}

void pushFeatures(float p2p, float energy, float zcr, float env) {
  p2pHist[histIndex] = p2p;
  energyHist[histIndex] = energy;
  zcrHist[histIndex] = zcr;
  envHist[histIndex] = env;

  histIndex++;
  if (histIndex >= FEATURE_HISTORY) {
    histIndex = 0;
    histFilled = true;
  }
}

// ==================================================
const char* stateText(int s) {
  switch (s) {
    case 0:
      return "WEAK LUNG SOUND";
    case 1:
      return "NORMAL LUNG SOUND";
    case 2:
      return "ABNORMAL LUNG SOUND";
    default:
      return "UNKNOWN";
  }
}

// ==================================================
float readFilteredSample() {
  int raw = analogRead(MIC_PIN);

  dcEstimate = (1.0 - DC_ALPHA) * dcEstimate + (DC_ALPHA * raw);
  float centered = raw - dcEstimate;

  if (centered > -2.0 && centered < 2.0) centered = 0.0;

  return centered;
}

// ==================================================
void analyzeWindow(float &p2pOut, float &energyOut, float &zcrOut, float &envOut) {
  unsigned long startTime = millis();

  float sMin = 99999.0;
  float sMax = -99999.0;
  float energySum = 0.0;
  long zeroCross = 0;
  long count = 0;
  float prev = 0.0;
  bool first = true;

  while (millis() - startTime < WINDOW_MS) {
    float s = readFilteredSample();

    if (s < sMin) sMin = s;
    if (s > sMax) sMax = s;

    float mag = abs(s);
    energySum += mag;

    smoothedEnvelope = (ENV_ALPHA * mag) + ((1.0 - ENV_ALPHA) * smoothedEnvelope);

    if (!first) {
      if ((prev > 0 && s < 0) || (prev < 0 && s > 0)) zeroCross++;
    } else {
      first = false;
    }

    prev = s;
    count++;
  }

  if (count <= 0) count = 1;

  p2pOut = sMax - sMin;
  energyOut = energySum / count;
  zcrOut = (float)zeroCross / count;
  envOut = smoothedEnvelope;
}

// ==================================================
int classifyInstant(float avgP2P, float avgEnergy, float avgZCR, float avgEnv, float instability) {
  // 0 = WEAK, 1 = NORMAL, 2 = ABNORMAL

  // ABNORMAL only when clearly noisy / unstable
  bool abnormalCondition =
    (avgZCR > 0.20) ||
    (avgZCR < 0.005) ||
    (instability > 2.10);

  if (abnormalCondition) {
    return 2;
  }

  // WEAK only when signal is clearly faint
  bool weakCondition =
    (avgEnergy < 60.0) &&
    (avgEnv < 70.0);

  if (weakCondition) {
    return 0;
  }

  // NORMAL includes moderate and deep but stable breathing
  bool normalCondition =
    (avgEnergy >= 60.0) &&
    (avgEnv >= 70.0) &&
    (avgZCR >= 0.005 && avgZCR <= 0.20) &&
    (instability <= 2.10);

  if (normalCondition) {
    return 1;
  }

  // fallback
  return 2;
}

// ==================================================
void setup() {
  Serial.begin(115200);
  delay(500);

  Serial.println("Lung Sound Classifier (3-State Fixed Version)");
}

// ==================================================
void loop() {
  float p2p, energy, zcr, env;
  analyzeWindow(p2p, energy, zcr, env);
  pushFeatures(p2p, energy, zcr, env);

  int n = histCount();
  if (n < 6) return;

  if (millis() - lastReport >= REPORT_MS) {
    lastReport = millis();

    float avgP2P = avgArr(p2pHist, n);
    float avgEnergy = avgArr(energyHist, n);
    float avgZCR = avgArr(zcrHist, n);
    float avgEnv = avgArr(envHist, n);

    float instability = (avgEnv > 0.01f)
      ? (maxArr(envHist, n) - minArr(envHist, n)) / avgEnv
      : 0.0f;

    int state = classifyInstant(avgP2P, avgEnergy, avgZCR, avgEnv, instability);

    Serial.print("P2P=");
    Serial.print(avgP2P, 1);
    Serial.print(" | Energy=");
    Serial.print(avgEnergy, 2);
    Serial.print(" | ZCR=");
    Serial.print(avgZCR, 4);
    Serial.print(" | Env=");
    Serial.print(avgEnv, 2);
    Serial.print(" | Instability=");
    Serial.print(instability, 3);
    Serial.print(" | State=");
    Serial.println(stateText(state));
  }
}