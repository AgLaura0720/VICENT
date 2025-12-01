/*  Doble BNO055 (muñeca + mano) en Arduino UNO + HX711 + sEMG
 *  Protocolo serie para Qt:
 *
 *  COMANDOS DESDE PC:
 *    '1' -> Modo Flexión/Extensión (ROM)
 *    '2' -> Modo Desviación Ulnar/Radial (ROM)
 *    '3' -> Modo Pronosupinación (ROM)
 *    '4' -> Modo Fuerza de prensión (solo fuerza + EMG)
 *    ' ' -> TARAR ROM (fijar 0° en postura actual) → responde ZERO_OK o ZERO_FAIL
 *    'e' -> Detener medición (modo NONE)
 *
 *  SALIDA SERIE (línea por muestra) – CSV:
 *    timestamp_s, angle_deg, force_kg, emg_env, threshold, activation
 *
 *  Convenciones:
 *    - En ejercicios 1–3 (ROM): force_kg = NaN
 *    - En ejercicio 4 (fuerza):  angle_deg = NaN
 *    - EMG se mide SIEMPRE (mismo formato en los 4 ejercicios)
 */

#include <Wire.h>
#include <Adafruit_Sensor.h>
#include <Adafruit_BNO055.h>
#include <math.h>
#include "HX711.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// ---------------------- BNO055 ----------------------
#define BNO_ADDR_WRIST  0x28   // muñeca
#define BNO_ADDR_HAND   0x29   // mano

const unsigned long PRINT_MS = 100;   // periodo de muestreo ~10 Hz

Adafruit_BNO055 bnoWrist = Adafruit_BNO055(55, BNO_ADDR_WRIST);
Adafruit_BNO055 bnoHand  = Adafruit_BNO055(56, BNO_ADDR_HAND);

struct Quat { float w, x, y, z; };

bool haveZero = false;
Quat qRelZero   = {1,0,0,0};
Quat qConjZero  = {1,0,0,0};
Quat qRel_latest = {1,0,0,0};

// ---------------------- HX711 (fuerza) ----------------------
HX711 balanza;
// CAMBIO DE PINES:
const int PIN_DOUT = 4;   // DT del HX711
const int PIN_SCK  = 2;  // SCK del HX711

float factorCal = -2121.82f;   // tu factor calibrado (para gramos)

// Filtro de media móvil para fuerza (en kg)
const uint8_t FORCE_FILT_N = 5;
float forceBuf[FORCE_FILT_N];
uint8_t forceIdx = 0;
float forceSum = 0.0f;

// ---------------------- sEMG (A0) ----------------------
const int EMG_PIN = A0;
int   emgBaseline   = 0;
float emgEnvelope   = 0.0f;
const int emgThreshold = 15;    // umbral fijo (puedes afinarlo)

// ---------------------- Modo de medición ----------------------
enum MeasurementMode { NONE, DEVIATIONS, FLEX_EXT, PRONO_SUP, FORCE_MODE };
MeasurementMode currentMode = NONE;

// ---------------------- Helpers BNO ----------------------
Quat readQuat(Adafruit_BNO055 &bno) {
  imu::Quaternion q = bno.getQuat();
  Quat out = { (float)q.w(), (float)q.x(), (float)q.y(), (float)q.z() };
  float n = sqrt(out.w*out.w + out.x*out.x + out.y*out.y + out.z*out.z);
  if (n > 0.0f) { out.w/=n; out.x/=n; out.y/=n; out.z/=n; }
  return out;
}

Quat qConj(const Quat &q) { return Quat{ q.w, -q.x, -q.y, -q.z }; }

Quat qMul(const Quat &a, const Quat &b) {
  return Quat{
    a.w*b.w - a.x*b.x - a.y*b.y - a.z*b.z,
    a.w*b.x + a.x*b.w + a.y*b.z - a.z*b.y,
    a.w*b.y - a.x*b.z + a.y*b.w + a.z*b.x,
    a.w*b.z + a.x*b.y - a.y*b.x + a.z*b.w
  };
}

float wrap180(float a) {
  while (a >  180.0f) a -= 360.0f;
  while (a < -180.0f) a += 360.0f;
  return a;
}

void printCalib(const char* tag, Adafruit_BNO055 &bno) {
  uint8_t sys, g, a, m;
  bno.getCalibration(&sys, &g, &a, &m);
  Serial.print(tag); Serial.print(" calib SYS:");
  Serial.print(sys); Serial.print(" G:");
  Serial.print(g);   Serial.print(" A:");
  Serial.print(a);   Serial.print(" M:");
  Serial.println(m);
}

// Ángulo puro de giro alrededor de un eje
float signedTwistAngleDeg(const Quat &q, float ax, float ay, float az) {
  float an = sqrt(ax*ax + ay*ay + az*az);
  if (an == 0) return 0.0f;
  ax/=an; ay/=an; az/=an;

  float dotv = q.x*ax + q.y*ay + q.z*az;
  float px = dotv * ax;
  float py = dotv * ay;
  float pz = dotv * az;

  float denom = sqrt(q.w*q.w + px*px + py*py + pz*pz);
  if (denom == 0) return 0.0f;
  float tw_w  = q.w / denom;
  float tw_vn = sqrt(px*px + py*py + pz*pz) / denom;

  float ang  = 2.0f * atan2(tw_vn, tw_w);
  float sign = (dotv >= 0.0f) ? 1.0f : -1.0f;
  float deg  = sign * (ang * 180.0f / M_PI);
  return wrap180(deg);
}

// Ajuste de signo según convención
inline void applyOrientationMapping(float &angle_deg, MeasurementMode mode) {
  if (mode == FLEX_EXT) {
    angle_deg *= -1.0f;
  } else if (mode == PRONO_SUP) {
    angle_deg *= -1.0f;
  } else if (mode == DEVIATIONS) {
    angle_deg *= 1.0f;
  }
}

// ---------------------- sEMG helpers ----------------------
int calibrarEMG() {
  long suma = 0;
  for (int i = 0; i < 800; i++) {
    suma += analogRead(EMG_PIN);
    delay(2);
  }
  return (int)(suma / 800L);
}

// Actualiza el envelope EMG y devuelve env, threshold y activación
void updateEMG(float &envOut, int &thrOut, int &actOut) {
  int emg = analogRead(EMG_PIN);
  float rect = fabs((float)emg - (float)emgBaseline);
  emgEnvelope = 0.90f * emgEnvelope + 0.10f * rect;

  envOut = emgEnvelope;
  thrOut = emgThreshold;
  actOut = (emgEnvelope > emgThreshold) ? 100 : 0;
}

// ---------------------- HX711 helpers ----------------------
void initForceFilter() {
  for (uint8_t i = 0; i < FORCE_FILT_N; i++) {
    forceBuf[i] = 0.0f;
  }
  forceIdx = 0;
  forceSum = 0.0f;
}

// Lee una muestra de fuerza en kg con filtro de media móvil
float readForceKg() {
  // OJO: quitamos el if (!is_ready()) return NAN;
  // read() ya espera a que haya nueva muestra
  long raw = balanza.read();  // bloquea hasta que el HX711 esté listo

  float gramos = (raw - balanza.get_offset()) / factorCal;
  float kg = gramos / 1000.0f;

  // filtro de media móvil
  forceSum -= forceBuf[forceIdx];
  forceBuf[forceIdx] = kg;
  forceSum += kg;
  forceIdx = (forceIdx + 1) % FORCE_FILT_N;

  return forceSum / FORCE_FILT_N;
}


// ---------------------- Serial control ----------------------
void showMenu() {
  Serial.println("\n=== MENÚ DE MEDICIÓN ===");
  Serial.println("1: Flexión/Extensión");
  Serial.println("2: Ulnar/Radial");
  Serial.println("3: Pronosupinación");
  Serial.println("4: Fuerza de prensión");
  Serial.println("e: Detener medición");
  Serial.println("Barra espaciadora: fijar cero ROM (responde ZERO_OK/ZERO_FAIL)");
}

// Lectura de comandos desde Serial
void handleSerial() {
  while (Serial.available() > 0) {
    char c = Serial.read();
    if (c == '\r' || c == '\n') continue;

    switch (c) {
      case '1':
        currentMode = FLEX_EXT;
        haveZero = false;
        Serial.println("Modo: Flexión/Extensión (1). Fija cero con espacio.");
        break;

      case '2':
        currentMode = DEVIATIONS;
        haveZero = false;
        Serial.println("Modo: Ulnar/Radial (2). Fija cero con espacio.");
        break;

      case '3':
        currentMode = PRONO_SUP;
        haveZero = false;
        Serial.println("Modo: Pronosupinación (3). Fija cero con espacio.");
        break;

      case '4':
        currentMode = FORCE_MODE;
        // Para fuerza no usamos haveZero
        Serial.println("Modo: Fuerza de prensión (4).");
        break;

      case 'e':
      case 'E':
        currentMode = NONE;
        haveZero = false;
        Serial.println(">> Medición detenida.");
        showMenu();
        break;

      case ' ':
      case 'z':
      case 'Z':
        // Solo calibramos ROM si estamos en modo ROM
        if (currentMode == FLEX_EXT || currentMode == DEVIATIONS || currentMode == PRONO_SUP) {
          qRelZero  = qRel_latest;
          qConjZero = qConj(qRelZero);
          haveZero  = true;
          Serial.println("ZERO_OK");
        } else {
          Serial.println("ZERO_FAIL");
        }
        break;

      default:
        // ignorado
        break;
    }
  }
}

// ---------------------- SETUP ----------------------
void setup() {
  Serial.begin(115200);
  while (!Serial) {}

  Wire.begin();

  if (!bnoWrist.begin(OPERATION_MODE_NDOF)) {
    Serial.println("ERROR: no inicia BNO055 muñeca (0x28).");
    while (1) { delay(10); }
  }
  if (!bnoHand.begin(OPERATION_MODE_NDOF)) {
    Serial.println("ERROR: no inicia BNO055 mano (0x29).");
    while (1) { delay(10); }
  }

  bnoWrist.setExtCrystalUse(true);
  bnoHand.setExtCrystalUse(true);

  delay(500);
  printCalib("Muñeca", bnoWrist);
  printCalib("Mano  ", bnoHand);

  // Inicializar HX711
  balanza.begin(PIN_DOUT, PIN_SCK);
  if (!balanza.is_ready()) {
    Serial.println("⚠️ HX711 no responde. Verifica cableado.");
  }
  // tare inicial
  balanza.set_scale(1.0f);
  balanza.tare(20);
  balanza.set_scale(factorCal);
  initForceFilter();

  // Calibrar EMG (baseline)
  emgBaseline = calibrarEMG();
  Serial.print("Baseline EMG: ");
  Serial.println(emgBaseline);

  showMenu();
}

// ---------------------- LOOP ----------------------
void loop() {
  static unsigned long lastPrint = 0;

  // 1) comandos de control
  handleSerial();

  if (currentMode == NONE) {
    delay(10);
    return;
  }

  // 2) EMG siempre
  float emgEnv;
  int   emgThr;
  int   emgAct;
  updateEMG(emgEnv, emgThr, emgAct);

  // 3) Fuerza (si está conectada)
  float fuerzaKg = -readForceKg();  // será NaN si no está lista

  // 4) Control de periodo de muestreo
  unsigned long now = millis();
  if (now - lastPrint < PRINT_MS) {
    delay(2);
    return;
  }
  lastPrint = now;
  float t = now / 1000.0f;

  // 5) Cálculo de ángulo según modo
  float angle_deg = NAN;

  if (currentMode == FLEX_EXT || currentMode == DEVIATIONS || currentMode == PRONO_SUP) {
    // ROM con BNO
    Quat qW = readQuat(bnoWrist);
    Quat qH = readQuat(bnoHand);

    Quat qRel = qMul(qH, qConj(qW));
    float n = sqrt(qRel.w*qRel.w + qRel.x*qRel.x + qRel.y*qRel.y + qRel.z*qRel.z);
    if (n > 0.0f) {
      qRel.w/=n; qRel.x/=n; qRel.y/=n; qRel.z/=n;
    }

    qRel_latest = qRel;

    if (!haveZero) {
      // Aún no tenemos cero ROM: no emitimos línea de datos
      Serial.println("Esperando cero ROM (espacio)...");
      return;
    }

    // Compensar respecto al cero
    Quat qCal = qMul(qConjZero, qRel);
    n = sqrt(qCal.w*qCal.w + qCal.x*qCal.x + qCal.y*qCal.y + qCal.z*qCal.z);
    if (n > 0.0f) {
      qCal.w/=n; qCal.x/=n; qCal.y/=n; qCal.z/=n;
    }

    if (currentMode == DEVIATIONS) {
      angle_deg = signedTwistAngleDeg(qCal, 0.0f, 0.0f, 1.0f); // roll Z
    } else if (currentMode == FLEX_EXT) {
      angle_deg = signedTwistAngleDeg(qCal, 0.0f, 1.0f, 0.0f); // pitch Y
    } else if (currentMode == PRONO_SUP) {
      angle_deg = signedTwistAngleDeg(qCal, 1.0f, 0.0f, 0.0f); // yaw X
    }

    applyOrientationMapping(angle_deg, currentMode);

    // Fuerza en estos ejercicios NO aplica → NaN
    fuerzaKg = NAN;
  }
  else if (currentMode == FORCE_MODE) {
    // Ejercicio de fuerza de prensión:
    //   - No usamos ángulo → NaN
    //   - fuerzaKg ya calculada
    angle_deg = NAN;
  }

  // 6) Imprimir CSV:
  // timestamp_s, angle_deg, force_kg, emg_env, threshold, activation
  Serial.print(t, 3);
  Serial.print(',');

  if (isnan(angle_deg)) Serial.print("NaN");
  else                  Serial.print(angle_deg, 2);
  Serial.print(',');

  if (isnan(fuerzaKg))  Serial.print("NaN");
  else                  Serial.print(fuerzaKg, 3);
  Serial.print(',');

  Serial.print(emgEnv, 2);
  Serial.print(',');
  Serial.print(emgThr);
  Serial.print(',');
  Serial.println(emgAct);

  // pequeño respiro
  delay(2);
}
