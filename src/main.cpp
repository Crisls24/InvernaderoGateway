/**
 * Gateway LoRa Heltec V2 — Invernadero
 * Recibe datos de sensores por LoRa, los envía a Firebase (RTDB + Firestore)
 * y muestra el estado en pantalla OLED.
 * Frecuencia: 915 MHz
 */

#include <heltec.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <time.h>

#include "secrets.h"

#define BAND    915E6

/* ===================== CONFIGURACION ===================== */
/* Credenciales en include/secrets.h (ver secrets.h.example)  */

const char* ntpServer       = "pool.ntp.org";
const long  gmtOffset_sec   = -21600;    // UTC-6
const int   daylightOffset_sec = 0;

String rtdbUrl = String(RTDB_URL) + "/sensores/data.json";
String projectID   = PROJECT_ID;
String apiKey      = FIREBASE_API_KEY;
String firestoreUrl =
  "https://firestore.googleapis.com/v1/projects/" + projectID +
  "/databases/(default)/documents/artifacts/default-app-id/public/data/"
  "historico_sensores?key=" + apiKey;

/* ===================== ESTADO GLOBAL ===================== */

unsigned long lastWiFiReconnectAttempt = 0;
const unsigned long WiFiReconnectInterval = 30000;

unsigned long lastDisplayUpdate = 0;
const unsigned long displayUpdateInterval = 1000;

bool ntpSincronizado = false;
int lastRTDBResponseCode = 0;
int lastFirestoreResponseCode = 0;

float lastTemp = 0.0, lastHum = 0.0, lastLuz = 0.0, lastVpd = 0.0;

unsigned long lastNTPResync = 0;
const unsigned long NTPResyncInterval = 21600000; // 6 h

/* ===================== PROTOTIPOS ===================== */

void initHeltec();
void reconectarWiFi();
void sincronizarNTP();
String leerPaqueteLoRa();
bool validarPayload(const String& payload, float& temp, float& hum, float& luz, float& vpd);
void responderLoRa(const String& timestamp);
void subirNube(float temp, float hum, float luz, float vpd);
void mostrarDisplay(float temp, float hum, float luz, float vpd, bool wifiOk);
String formatoISO(const struct tm& timeinfo);

/* ===================== SETUP ===================== */

void setup() {
  initHeltec();
  delay(1000);

  Serial.begin(115200);
  Serial.println("\n=== GATEWAY HELTEC LORA 915MHz ===");

  // Conexion WiFi con timeout de 8s
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  unsigned long timeout = millis() + 8000;
  while (WiFi.status() != WL_CONNECTED && millis() < timeout) {
    delay(500);
    Serial.print(".");
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\nWiFi conectado.");
    sincronizarNTP();
  } else {
    Serial.println("\nSin WiFi. Modo offline.");
  }

  LoRa.setPins(SS, RST_LoRa, DIO0);
  LoRa.receive();
}

/* ===================== LOOP PRINCIPAL ===================== */

void loop() {
  unsigned long now = millis();

  // Reconexion WiFi asincrona cada 30s
  if (WiFi.status() != WL_CONNECTED &&
      now - lastWiFiReconnectAttempt >= WiFiReconnectInterval) {
    lastWiFiReconnectAttempt = now;
    reconectarWiFi();
  }

  // Recibir paquete LoRa
  int size = LoRa.parsePacket();
  if (size) {
    String payload = leerPaqueteLoRa();

    float temp, hum, luz, vpd;
    if (validarPayload(payload, temp, hum, luz, vpd)) {
      Serial.printf("Datos: T=%.1f H=%.1f L=%.0f VPD=%.2f\n", temp, hum, luz, vpd);

      // Obtener timestamp
      struct tm tm;
      String ts = "No_Sync";
      if (getLocalTime(&tm)) {
        char buf[32];
        strftime(buf, sizeof(buf), "%d/%m/%y %H:%M:%S", &tm);
        ts = String(buf);
      }

      responderLoRa(ts);

      if (WiFi.status() == WL_CONNECTED) {
        subirNube(temp, hum, luz, vpd);
      } else {
        lastRTDBResponseCode = 0;
        lastFirestoreResponseCode = 0;
      }

      lastTemp = temp; lastHum = hum; lastLuz = luz; lastVpd = vpd;
    }
  }

  // Refrescar pantalla cada 1s
  if (now - lastDisplayUpdate >= displayUpdateInterval) {
    lastDisplayUpdate = now;
    mostrarDisplay(lastTemp, lastHum, lastLuz, lastVpd, WiFi.status() == WL_CONNECTED);
  }

  // Resincronizar NTP cada 6h
  if (WiFi.status() == WL_CONNECTED && now - lastNTPResync >= NTPResyncInterval) {
    lastNTPResync = now;
    sincronizarNTP();
  }
}

/* ===================== FUNCIONES ===================== */

void initHeltec() {
  Heltec.begin(true, true, true, true, BAND);
  Heltec.display->clear();
  Heltec.display->drawString(0, 0, "GATEWAY LoRa");
  Heltec.display->drawString(0, 10, "915 MHz");
  Heltec.display->display();
  delay(100);
}

void reconectarWiFi() {
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
}

void sincronizarNTP() {
  configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
  ntpSincronizado = true;
  Serial.println("NTP sincronizado.");
}

/**
 * Lee el paquete LoRa entrante como string.
 */
String leerPaqueteLoRa() {
  String s;
  s.reserve(50);
  while (LoRa.available()) s += (char)LoRa.read();
  return s;
}

/**
 * Valida el payload CSV: temp, hum, luz, vpd
 * Retorna false si el formato es invalido o los valores estan fuera de rango.
 */
bool validarPayload(const String& p, float& temp, float& hum, float& luz, float& vpd) {
  if (p.length() == 0) return false;

  int c1 = p.indexOf(',');
  int c2 = p.indexOf(',', c1 + 1);
  int c3 = p.indexOf(',', c2 + 1);
  if (c1 == -1 || c2 == -1 || c3 == -1) return false;

  String t = p.substring(0, c1);
  String h = p.substring(c1 + 1, c2);
  String l = p.substring(c2 + 1, c3);
  String v = p.substring(c3 + 1);

  if (t.length() == 0 || h.length() == 0 || l.length() == 0 || v.length() == 0)
    return false;

  temp = t.toFloat();
  hum  = h.toFloat();
  luz  = l.toFloat();
  vpd  = v.toFloat();

  return (temp >= -50 && temp <= 100 && hum >= 0 && hum <= 100 &&
          luz >= 0 && luz <= 100000 && vpd >= 0);
}

/**
 * Envia timestamp al nodo sensor por LoRa y vuelve a modo escucha.
 */
void responderLoRa(const String& ts) {
  LoRa.beginPacket();
  LoRa.print(ts);
  LoRa.endPacket();
  LoRa.receive();
}

/**
 * Sube datos a Firebase Realtime Database (PUT) y Firestore (POST).
 * Firestore requiere timestamp NTP valido.
 */
void subirNube(float temp, float hum, float luz, float vpd) {
  HTTPClient http;

  struct tm tm;
  String iso = "1970-01-01T00:00:00Z";
  if (getLocalTime(&tm)) iso = formatoISO(tm);

  // --- RTDB ---
  String rtdb = "{\"temperatura\":" + String(temp) +
                ",\"humedad\":" + String(hum) +
                ",\"luz_lumenes\":" + String(luz) +
                ",\"vpd\":" + String(vpd) + "}";

  http.begin(rtdbUrl);
  http.addHeader("Content-Type", "application/json");
  http.setTimeout(2000);
  lastRTDBResponseCode = http.PUT(rtdb);
  http.end();

  // --- Firestore (solo si hay timestamp) ---
  if (iso == "1970-01-01T00:00:00Z") {
    lastFirestoreResponseCode = -1;
    return;
  }

  String fs = "{\"fields\":{"
    "\"temperatura\":{\"doubleValue\":" + String(temp) + "},"
    "\"humedad\":{\"doubleValue\":" + String(hum) + "},"
    "\"luz_lumenes\":{\"doubleValue\":" + String(luz) + "},"
    "\"vpd\":{\"doubleValue\":" + String(vpd) + "},"
    "\"fecha_registro\":{\"timestampValue\":\"" + iso + "\"}"
  "}}";

  http.begin(firestoreUrl);
  http.addHeader("Content-Type", "application/json");
  http.setTimeout(2000);
  lastFirestoreResponseCode = http.POST(fs);
  http.end();
}

String formatoISO(const struct tm& tm) {
  char buf[30];
  strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", &tm);
  return String(buf);
}

/**
 * OLED 128x64 — 5 lineas:
 *   Fila 1: Estado WiFi/Cloud
 *   Fila 2-4: Temp, Hum, Luz+VPD
 *   Fila 5: NTP
 */
void mostrarDisplay(float temp, float hum, float luz, float vpd, bool wifiOk) {
  Heltec.display->clear();

  String linea1;
  if (wifiOk) {
    if (lastRTDBResponseCode == 200 &&
        (lastFirestoreResponseCode == 200 || lastFirestoreResponseCode == 201))
      linea1 = "WiFi+Cloud OK";
    else if (lastRTDBResponseCode > 0 && lastFirestoreResponseCode > 0)
      linea1 = "WiFi OK | Cloud ERR:" + String(lastFirestoreResponseCode);
    else
      linea1 = "WiFi OK | Enviando...";
  } else {
    linea1 = "WiFi OFFLINE";
  }

  Heltec.display->drawString(0, 0,  linea1);
  Heltec.display->drawString(0, 10, "Temp: " + String(temp, 1) + " C");
  Heltec.display->drawString(0, 20, "Hum:  " + String(hum,  1) + " %");
  Heltec.display->drawString(0, 30, "Luz: " + String(luz, 0) +
                                    " VPD:" + String(vpd, 2));
  Heltec.display->drawString(0, 40, ntpSincronizado ?
                              "NTP: OK" : "NTP: Pendiente");
  Heltec.display->display();
}
