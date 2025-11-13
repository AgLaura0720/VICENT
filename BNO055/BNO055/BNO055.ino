/*  Doble BNO055 (muñeca + mano) en Arduino UNO con "cero" por barra espaciadora
 *  - UNO I2C: SDA=A4, SCL=A5
 *  - BNO muñeca: ADO->GND => 0x28
 *  - BNO mano:   ADO->3V3 => 0x29
 *  Montaje (TU ORIENTACIÓN):
 *    +X hacia dedos (distal), +Y hacia lado RADIAL (pulgar), +Z ENTRANDO (hacia el cuerpo)
 *  Salida instantánea (SIN promedio):
 *    Según selección: Flex/Ext (pitch Y), Ulnar/Radial (roll Z), Prono/Sup (yaw X)
 *  Cero:
 *    Presiona ESPACIO (o 'z') en el Monitor Serie para fijar la postura inicial (0°)
 *  Frecuencia de impresión:
 *    Cambia PRINT_MS para ajustar cada cuánto imprime (ms)
 *  Control:
 *    "1" = Flexión/Extensión
 *    "2" = Ulnar/Radial
 *    "3" = Prono/Supinación
 *    "e" = Detener medición (volver al menú)
 */

#include <Wire.h>
#include <Adafruit_Sensor.h>
#include <Adafruit_BNO055.h>
#include <math.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#define BNO_ADDR_WRIST  0x28   // muñeca
#define BNO_ADDR_HAND   0x29   // mano

// === Ajusta esto para la velocidad de impresión ===
const unsigned long PRINT_MS = 100;   // p.ej. 100=10Hz, 50=20Hz, 200=5Hz

Adafruit_BNO055 bnoWrist = Adafruit_BNO055(55, BNO_ADDR_WRIST);
Adafruit_BNO055 bnoHand  = Adafruit_BNO055(56, BNO_ADDR_HAND);

struct Quat { float w, x, y, z; };

// referencia (cero por Serial)
bool haveZero = false;
Quat qRelZero   = {1,0,0,0};
Quat qConjZero  = {1,0,0,0};

// último qRel calculado (para fijar cero desde el teclado)
Quat qRel_latest = {1,0,0,0};

// Variable para el modo de medición
enum MeasurementMode { NONE, DEVIATIONS, FLEX_EXT, PRONO_SUP };
MeasurementMode currentMode = NONE;

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

void showMenu() {
  Serial.println("\n=== MENÚ DE MEDICIÓN ===");
  Serial.println("1: Flexión/Extensión (pitch Y)");
  Serial.println("2: Ulnar/Radial (roll Z)");
  Serial.println("3: Prono/Supinación (yaw X)");
  Serial.println("e: Detener medición (volver al menú)");
  Serial.println("Durante medición, fija el cero con ESPACIO/'z'.");
}

/* =========== ÁNGULO DE "TWIST" (giro puro) ALREDEDOR DE UN EJE ===========
   q     : cuaternión unitario (mano respecto a muñeca ya calibrado)
   axis  : eje unitario (ax, ay, az) en el marco de la muñeca
   retorno: ángulo firmado en grados, rango (-180, 180]
*/
float signedTwistAngleDeg(const Quat &q, float ax, float ay, float az) {
  float an = sqrt(ax*ax + ay*ay + az*az);
  if (an == 0) return 0.0f;
  ax/=an; ay/=an; az/=an;

  float dotv = q.x*ax + q.y*ay + q.z*az; // componente sobre el eje
  float px = dotv * ax;
  float py = dotv * ay;
  float pz = dotv * az;

  float denom = sqrt(q.w*q.w + px*px + py*py + pz*pz);
  if (denom == 0) return 0.0f;
  float tw_w  = q.w / denom;
  float tw_vn = sqrt(px*px + py*py + pz*pz) / denom;

  float ang  = 2.0f * atan2(tw_vn, tw_w); // rad
  float sign = (dotv >= 0.0f) ? 1.0f : -1.0f;
  float deg  = sign * (ang * 180.0f / M_PI);
  return wrap180(deg);
}

/* ======= MAPEO DE ORIENTACIÓN =======
   Tu montaje: +Y radial (pulgar), +Z entrando.
   Convenciones:
   - FLEXIÓN PALMAR positiva
   - DESVIACIÓN ULNAR positiva / RADIAL negativa
   - PRONACIÓN/SUPINACIÓN: ajustar signo según protocolo
*/
inline void applyOrientationMapping(float &angle_deg, MeasurementMode mode) {
  if (mode == FLEX_EXT) {
    angle_deg *= -1.0f;  // invierte Y para flex/ext
  } else if (mode == PRONO_SUP) {
    angle_deg *= -1.0f;  // mantiene inversión para prono/sup (ahora en yaw X)
  } else if (mode == DEVIATIONS) {
    angle_deg *= 1.0f;   // sin invertir por ahora (ahora en roll Z)
  }
}

/* ======= LECTURA SERIE UNIFICADA (comandos + cero) ======= */
void handleSerial() {
  while (Serial.available() > 0) {
    char c = Serial.read();

    // Ignorar retornos de carro / saltos de línea del monitor serie
    if (c == '\r' || c == '\n') continue;

    switch (c) {
      case '1':
        currentMode = FLEX_EXT;
        haveZero = false;
        Serial.println("Modo: Flexión/Extensión (pitch Y). Fija cero con ESPACIO/'z'.");
        break;

      case '2':
        currentMode = DEVIATIONS;      // Ulnar/Radial (roll Z)
        haveZero = false;
        Serial.println("Modo: Ulnar/Radial (roll Z). Fija cero con ESPACIO/'z'.");
        break;

      case '3':
        currentMode = PRONO_SUP;       // Prono/Sup (yaw X)
        haveZero = false;
        Serial.println("Modo: Pronosupinación (yaw X). Fija cero con ESPACIO/'z'.");
        break;

      case 'e': case 'E':
        currentMode = NONE;
        haveZero = false;
        Serial.println(">> Medición detenida. Presiona 1/2/3 para iniciar.");
        showMenu();
        break;

      case ' ': case 'z': case 'Z':
        if (currentMode != NONE) {
          qRelZero  = qRel_latest;       // fijar cero con el qRel más reciente
          qConjZero = qConj(qRelZero);
          haveZero  = true;
          Serial.println(">> Cero fijado (postura actual = 0°).");
        } else {
          Serial.println(">> No hay medición activa. Presiona 1/2/3 primero.");
        }
        break;

      default:
        // opcional: reportar caracter ignorado
        // Serial.print("Ignorado: "); Serial.println(c);
        break;
    }
  }
}

void setup() {
  Serial.begin(115200);
  while (!Serial) {}

  Wire.begin(); // UNO: SDA=A4, SCL=A5

  if (!bnoWrist.begin(OPERATION_MODE_NDOF)) {
    Serial.println("ERROR: no inicia BNO055 muñeca (0x28). Revisa ADO y cableado.");
    while (1) { delay(10); }
  }
  if (!bnoHand.begin(OPERATION_MODE_NDOF)) {
    Serial.println("ERROR: no inicia BNO055 mano (0x29). Revisa ADO y cableado.");
    while (1) { delay(10); }
  }

  bnoWrist.setExtCrystalUse(true);
  bnoHand.setExtCrystalUse(true);

  delay(500);
  printCalib("Muñeca", bnoWrist);
  printCalib("Mano  ", bnoHand);
  showMenu();
}

void loop() {
  static unsigned long lastPrint = 0;

  // Entrada de control unificada
  handleSerial();

  // Si no hay medición activa, espera en menú
  if (currentMode == NONE) {
    delay(50);
    return;
  }

  // 1) Quats absolutos
  Quat qW = readQuat(bnoWrist);
  Quat qH = readQuat(bnoHand);

  // 2) Rotación relativa mano respecto a muñeca
  Quat qRel = qMul(qH, qConj(qW));
  float n = sqrt(qRel.w*qRel.w + qRel.x*qRel.x + qRel.y*qRel.y + qRel.z*qRel.z);
  if (n > 0.0f) { qRel.w/=n; qRel.x/=n; qRel.y/=n; qRel.z/=n; }

  // Guardar qRel para poder fijar cero exactamente sobre la orientación actual
  qRel_latest = qRel;

  // 3) Si no hay cero fijado, pedirlo y no avanzar
  if (!haveZero) {
    unsigned long now = millis();
    if (now - lastPrint >= 500) {
      Serial.println("Esperando ESPACIO/'z' para fijar cero...");
      lastPrint = now;
    }
    delay(5);
    return;
  }

  // 4) Compensar respecto al cero: qCal = conj(qRelZero) * qRel
  Quat qCal = qMul(qConjZero, qRel);
  n = sqrt(qCal.w*qCal.w + qCal.x*qCal.x + qCal.y*qCal.y + qCal.z*qCal.z);
  if (n > 0.0f) { qCal.w/=n; qCal.x/=n; qCal.y/=n; qCal.z/=n; }

  // 5) Ángulo según modo
  float angle_deg = 0.0f;
  const char* label = "";
  if (currentMode == DEVIATIONS) {
    angle_deg = signedTwistAngleDeg(qCal, 0.0f, 0.0f, 1.0f); // Ulnar/Radial → eje +Z (roll)
    label = "ULNAR/RADIAL (roll) [deg]: ";
  } else if (currentMode == FLEX_EXT) {
    angle_deg = signedTwistAngleDeg(qCal, 0.0f, 1.0f, 0.0f); // Flex/Ext → eje +Y (pitch)
    label = "FLEX/EXT (pitch) [deg]: ";
  } else if (currentMode == PRONO_SUP) {
    angle_deg = signedTwistAngleDeg(qCal, 1.0f, 0.0f, 0.0f); // Prono/Sup → eje +X (yaw)
    label = "PRONO/SUP (yaw) [deg]: ";
  }

  // 6) Ajuste de orientación (signos)
  applyOrientationMapping(angle_deg, currentMode);

  // 7) Imprime a la tasa definida por PRINT_MS
  unsigned long now = millis();
  if (now - lastPrint >= PRINT_MS) {
    Serial.print(label);
    Serial.println(angle_deg, 2);
    lastPrint = now;
  }

  // Pequeño respiro al bus I2C
  delay(5);
}