#include <Arduino.h>

/*
  MAX9814 Lung Sound Classifier
  -----------------------------
  More sensitive + fixed calculations

  Classes:
    0 = NO SIGNAL / POOR CONTACT
    1 = VESICULAR-LIKE
    2 = WHEEZE-LIKE
    3 = FINE CRACKLES-LIKE
    4 = COARSE CRACKLES-LIKE
    5 = STRIDOR-LIKE
    6 = RHONCHI-LIKE
    7 = NOISY / UNCLASSIFIED
*/

#define MIC_PIN A0

const unsigned long CALIBRATION_MS   = 8000;
const unsigned long SAMPLE_WINDOW_MS = 80;
const unsigned long SCAN_DURATION_MS = 6000;
const unsigned long IGNORE_START_MS  = 800;

// ----------------------------
// Baseline
// ----------------------------
float baseEnv = 0;
float baseP2P = 0;
float baseZCR = 0;
float baseSmooth = 0;

// ----------------------------
// Helpers
// ----------------------------
float clampf(float x, float lo, float hi) {
  if (x < lo) return lo;
  if (x < lo) return lo;
  if (x > hi) return hi;
  return x;
}

float safeRatio(float value, float base, float minBase) {
  if (base < minBase) base = minBase;
  return value / base;
}

const char* getClassLabel(int classId) {
  switch (classId) {
    case 0: return "NO SIGNAL / POOR CONTACT";
    case 1: return "VESICULAR-LIKE";
    case 2: return "WHEEZE-LIKE";
    case 3: return "FINE CRACKLES-LIKE";
    case 4: return "COARSE CRACKLES-LIKE";
    case 5: return "STRIDOR-LIKE";
    case 6: return "RHONCHI-LIKE";
    default: return "NOISY / UNCLASSIFIED";
  }
}

// ----------------------------
// Read one audio window
// ----------------------------
void readAudioWindow(float &env, float &p2p, float &zcrNorm,
                     int &burstCount, int &shortBurstCount, int &longBurstCount,
                     float &smoothness, float &activity) {
  unsigned long t0 = millis();

  int signalMin = 1023;
  int signalMax = 0;

  long absSum = 0;
  long diffSum = 0;
  long sampleCount = 0;

  int prevCentered = 0;
  int zeroCrossings = 0;

  bool inBurst = false;
  int burstSamples = 0;
  burstCount = 0;
  shortBurstCount = 0;
  longBurstCount = 0;

  // More sensitive adaptive threshold
  int dynamicBurstThreshold = (int)(baseEnv * 1.8f);
  if (dynamicBurstThreshold < 18) dynamicBurstThreshold = 18;
  if (dynamicBurstThreshold > 70) dynamicBurstThreshold = 70;

  while (millis() - t0 < SAMPLE_WINDOW_MS) {
    int raw = analogRead(MIC_PIN);
    int centered = raw - 512;
    int amp = abs(centered);

    if (raw < signalMin) signalMin = raw;
    if (raw > signalMax) signalMax = raw;

    absSum += amp;
    if (sampleCount > 0) diffSum += abs(centered - prevCentered);
    sampleCount++;

    if ((centered > 0 && prevCentered < 0) || (centered < 0 && prevCentered > 0)) {
      zeroCrossings++;
    }

    if (amp > dynamicBurstThreshold) {
      if (!inBurst) {
        inBurst = true;
        burstSamples = 0;
      }
      burstSamples++;
    } else {
      if (inBurst) {
        if (burstSamples >= 2) {
          burstCount++;
          if (burstSamples <= 4) {
            shortBurstCount++;
          } else {
            longBurstCount++;
          }
        }
        inBurst = false;
      }
    }

    prevCentered = centered;
  }

  p2p = signalMax - signalMin;
  env = (sampleCount > 0) ? (float)absSum / sampleCount : 0.0f;
  smoothness = (sampleCount > 1) ? (float)diffSum / (sampleCount - 1) : 0.0f;
  zcrNorm = (sampleCount > 0) ? (float)zeroCrossings / sampleCount : 0.0f;

  activity = (env * 0.55f) + (p2p * 0.07f) + (smoothness * 0.38f);
}

// ----------------------------
// Calibration
// ----------------------------
void calibrateSensor() {
  Serial.println("Calibrating sensor for 8 seconds...");
  Serial.println("Keep the microphone still.");
  Serial.println();

  unsigned long t0 = millis();
  int count = 0;

  float envSum = 0;
  float p2pSum = 0;
  float zcrSum = 0;
  float smoothSum = 0;

  while (millis() - t0 < CALIBRATION_MS) {
    float env, p2p, zcrNorm, smoothness, activity;
    int burstCount, shortBurstCount, longBurstCount;

    readAudioWindow(env, p2p, zcrNorm, burstCount, shortBurstCount, longBurstCount, smoothness, activity);

    envSum += env;
    p2pSum += p2p;
    zcrSum += zcrNorm;
    smoothSum += smoothness;
    count++;
  }

  if (count > 0) {
    baseEnv = envSum / count;
    baseP2P = p2pSum / count;
    baseZCR = zcrSum / count;
    baseSmooth = smoothSum / count;
  }

  if (baseZCR < 0.0010f) baseZCR = 0.0010f;
  if (baseSmooth < 1.0f) baseSmooth = 1.0f;
  if (baseEnv < 1.0f) baseEnv = 1.0f;
  if (baseP2P < 1.0f) baseP2P = 1.0f;

  Serial.println("Calibration complete.");
  Serial.print("Base Env: "); Serial.println(baseEnv, 3);
  Serial.print("Base P2P: "); Serial.println(baseP2P, 3);
  Serial.print("Base ZCR: "); Serial.println(baseZCR, 5);
  Serial.print("Base Smooth: "); Serial.println(baseSmooth, 3);
  Serial.println();
}

// ----------------------------
// Window classifier
// ----------------------------
int classifyWindow(float env, float p2p, float zcrNorm,
                   int burstCount, int shortBurstCount, int longBurstCount,
                   float smoothness, float activity) {
  float envRatio    = safeRatio(env, baseEnv, 1.0f);
  float p2pRatio    = safeRatio(p2p, baseP2P, 1.0f);
  float zcrRatio    = safeRatio(zcrNorm, baseZCR, 0.0010f);
  float smoothRatio = safeRatio(smoothness, baseSmooth, 1.0f);

  if (envRatio < 0.60f && p2pRatio < 0.75f) {
    return 0;
  }

  int vesicularScore = 0;
  int wheezeScore = 0;
  int fineScore = 0;
  int coarseScore = 0;
  int stridorScore = 0;
  int rhonchiScore = 0;

  // VESICULAR
  if (envRatio > 0.75f && envRatio < 2.80f) vesicularScore += 2;
  if (smoothRatio < 1.55f) vesicularScore += 2;
  if (burstCount <= 1) vesicularScore += 2;
  if (shortBurstCount == 0 && longBurstCount == 0) vesicularScore += 2;
  if (zcrRatio > 0.30f && zcrRatio < 1.90f) vesicularScore += 1;

  // WHEEZE
  if (zcrRatio >= 1.6f) wheezeScore += 3;
  if (burstCount <= 2) wheezeScore += 2;
  if (smoothRatio >= 1.1f && smoothRatio < 2.6f) wheezeScore += 1;
  if (envRatio > 0.95f) wheezeScore += 1;

  // FINE CRACKLES
  if (shortBurstCount >= 2) fineScore += 3;
  if (shortBurstCount >= longBurstCount) fineScore += 1;
  if (smoothRatio >= 1.35f) fineScore += 2;
  if (zcrRatio >= 1.05f) fineScore += 1;
  if (burstCount >= 3) fineScore += 1;

  // COARSE CRACKLES
  if (longBurstCount >= 2) coarseScore += 3;
  if (longBurstCount > shortBurstCount) coarseScore += 1;
  if (smoothRatio >= 1.45f) coarseScore += 2;
  if (zcrRatio > 0.25f && zcrRatio < 1.55f) coarseScore += 1;
  if (burstCount >= 3) coarseScore += 2;
  if (envRatio > 1.0f) coarseScore += 1;

  // STRIDOR
  if (zcrRatio >= 2.2f) stridorScore += 3;
  if (burstCount <= 1) stridorScore += 2;
  if (smoothRatio >= 1.0f && smoothRatio < 2.3f) stridorScore += 1;
  if (envRatio > 0.95f) stridorScore += 1;

  // RHONCHI
  if (zcrRatio < 0.95f) rhonchiScore += 2;
  if (smoothRatio >= 1.45f) rhonchiScore += 2;
  if (burstCount <= 3) rhonchiScore += 1;
  if (envRatio > 1.0f) rhonchiScore += 1;

  int bestScore = vesicularScore;
  int classId = 1;

  if (wheezeScore > bestScore)  { bestScore = wheezeScore;  classId = 2; }
  if (fineScore > bestScore)    { bestScore = fineScore;    classId = 3; }
  if (coarseScore > bestScore)  { bestScore = coarseScore;  classId = 4; }
  if (stridorScore > bestScore) { bestScore = stridorScore; classId = 5; }
  if (rhonchiScore > bestScore) { bestScore = rhonchiScore; classId = 6; }

  if (bestScore < 4) classId = 7;

  // balanced guards
  if (classId == 4 && !(longBurstCount >= 2 && burstCount >= 3 && smoothRatio >= 1.45f)) classId = 7;
  if (classId == 3 && !(shortBurstCount >= 2 && burstCount >= 3 && smoothRatio >= 1.35f)) classId = 7;
  if (classId == 2 && !(zcrRatio >= 1.6f && burstCount <= 2)) classId = 7;
  if (classId == 5 && !(zcrRatio >= 2.2f && burstCount <= 1)) classId = 7;
  if (classId == 6 && !(zcrRatio < 0.95f && smoothRatio >= 1.45f)) classId = 7;

  // vesicular override
  if (burstCount == 0 &&
      shortBurstCount == 0 &&
      longBurstCount == 0 &&
      smoothRatio < 1.45f &&
      envRatio > 0.75f &&
      envRatio < 2.80f) {
    classId = 1;
  }

  return classId;
}

// ----------------------------
// Main
// ----------------------------
void setup() {
  Serial.begin(115200);
  delay(1200);
  calibrateSensor();
}

void loop() {
  Serial.println("====================================");
  Serial.println("Scanning for 6 seconds...");
  Serial.println("Please keep the microphone steady.");
  Serial.println("====================================");

  unsigned long scanStart = millis();

  int votes[8] = {0, 0, 0, 0, 0, 0, 0, 0};

  float envSum = 0;
  float p2pSum = 0;
  float zcrSum = 0;
  float burstSum = 0;
  float shortBurstSum = 0;
  float longBurstSum = 0;
  float smoothSum = 0;
  float activitySum = 0;
  int count = 0;

  while (millis() - scanStart < SCAN_DURATION_MS) {
    float env, p2p, zcrNorm, smoothness, activity;
    int burstCount, shortBurstCount, longBurstCount;

    readAudioWindow(env, p2p, zcrNorm, burstCount, shortBurstCount, longBurstCount, smoothness, activity);

    if (millis() - scanStart < IGNORE_START_MS) {
      continue;
    }

    int c = classifyWindow(env, p2p, zcrNorm, burstCount, shortBurstCount, longBurstCount, smoothness, activity);
    if (c >= 0 && c <= 7) votes[c]++;

    envSum += env;
    p2pSum += p2p;
    zcrSum += zcrNorm;
    burstSum += burstCount;
    shortBurstSum += shortBurstCount;
    longBurstSum += longBurstCount;
    smoothSum += smoothness;
    activitySum += activity;
    count++;
  }

  if (count == 0) {
    Serial.println("No samples collected.");
    delay(1000);
    return;
  }

  float avgEnv = envSum / count;
  float avgP2P = p2pSum / count;
  float avgZCR = zcrSum / count;
  float avgBursts = burstSum / count;
  float avgShortBursts = shortBurstSum / count;
  float avgLongBursts = longBurstSum / count;
  float avgSmooth = smoothSum / count;
  float avgActivity = activitySum / count;

  float envRatioAvg    = safeRatio(avgEnv, baseEnv, 1.0f);
  float zcrRatioAvg    = safeRatio(avgZCR, baseZCR, 0.0010f);
  float smoothRatioAvg = safeRatio(avgSmooth, baseSmooth, 1.0f);

  int finalClass = 7;
  int bestVotes = -1;

  for (int i = 0; i < 8; i++) {
    if (votes[i] > bestVotes) {
      bestVotes = votes[i];
      finalClass = i;
    }
  }

  // final scan validation
  if (finalClass == 4 &&
    !(avgLongBursts >= 3.5f && avgBursts >= 4.5f && smoothRatioAvg >= 1.6f)) {
  finalClass = 7;
}

  if (finalClass == 3 &&
      !(avgShortBursts >= 2.0f && avgBursts >= 3.0f && smoothRatioAvg >= 1.35f)) {
    finalClass = 7;
  }

  if (finalClass == 2 &&
      !(zcrRatioAvg >= 1.6f && avgBursts <= 2.0f)) {
    finalClass = 7;
  }

  if (finalClass == 5 &&
      !(zcrRatioAvg >= 2.2f && avgBursts <= 1.0f)) {
    finalClass = 7;
  }

  if (finalClass == 6 &&
      !(zcrRatioAvg < 0.95f && smoothRatioAvg >= 1.45f)) {
    finalClass = 7;
  }

  if (avgBursts < 1.0f &&
      avgShortBursts < 0.5f &&
      avgLongBursts < 0.5f &&
      smoothRatioAvg < 1.45f &&
      envRatioAvg > 0.75f &&
      envRatioAvg < 2.80f) {
    finalClass = 1;
  }

  float confidence = (count > 0) ? (100.0f * bestVotes / count) : 0.0f;
  confidence = clampf(confidence, 0.0f, 100.0f);

  Serial.println();
  Serial.println("--------- SCAN RESULT ---------");
  Serial.print("Average Env: "); Serial.println(avgEnv, 2);
  Serial.print("Average P2P: "); Serial.println(avgP2P, 2);
  Serial.print("Average ZCR(norm): "); Serial.println(avgZCR, 5);
  Serial.print("Average Bursts: "); Serial.println(avgBursts, 2);
  Serial.print("Average Short Bursts: "); Serial.println(avgShortBursts, 2);
  Serial.print("Average Long Bursts: "); Serial.println(avgLongBursts, 2);
  Serial.print("Average Smoothness: "); Serial.println(avgSmooth, 2);
  Serial.print("Average Activity: "); Serial.println(avgActivity, 2);
  Serial.print("Detected Class: "); Serial.println(getClassLabel(finalClass));
  Serial.print("Confidence: "); Serial.print(confidence, 1); Serial.println("%");
  Serial.println("-------------------------------");
  Serial.println();

  delay(1500);
}