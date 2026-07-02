# Gateway LoRa Invernadero — ESP32 Heltec V2

Firmware (PlatformIO / Arduino) del **gateway** del sistema de monitoreo de invernaderos: recibe los paquetes LoRa de los nodos sensores, los valida y los publica en **Firebase**, con hora real vía NTP.

El gateway corre sobre una **Heltec WiFi LoRa 32 V2** en banda de 915 MHz. Centraliza la información de múltiples nodos sensores, responde a cada uno con la fecha/hora para que sincronicen su reloj y alimenta la app móvil en tiempo real.

## Características

- Recepción de **paquetes LoRa (915 MHz)** de nodos sensores.
- **Validación estricta del payload CSV** `temp,hum,lux,vpd` (formato y rangos físicos).
- **Respuesta al nodo** por LoRa con fecha/hora (NTP, UTC-6).
- **Publicación en Firebase:**
  - **Realtime Database** (PUT) — dato actual en `sensores/data.json`.
  - **Firestore** (POST) — histórico con timestamp ISO en `historico_sensores`.
- **Sincronización NTP** con re-sync automático cada 6 h.
- **Reconexión WiFi asíncrona** cada 30 s si se pierde la conexión.
- **Estado en OLED** (5 líneas): WiFi/Cloud, temperatura, humedad, luz+VPD y NTP.

## Hardware

| Componente | Nota |
| --- | --- |
| Heltec WiFi LoRa 32 V2 | ESP32 + SX1276 + OLED 128x64 |

## Configuración (credenciales)

Las credenciales no viven en el código fuente. Al clonar el repo:

1. Copia la plantilla:
   ```
   cp include/secrets.h.example include/secrets.h
   ```
2. Llena tus valores reales en `include/secrets.h`:
   - `WIFI_SSID` / `WIFI_PASSWORD`
   - `RTDB_URL` — URL de tu Realtime Database
   - `PROJECT_ID` / `FIREBASE_API_KEY` — Firestore

> `include/secrets.h` está en `.gitignore`: nunca se sube al repositorio.

## Flujo de datos

```
Nodo(s) ESP32 ──LoRa 915MHz──▶ Gateway Heltec
                                  │
                                  ├─▶ LoRa: respuesta fecha/hora (NTP) al nodo
                                  ├─▶ Firebase RTDB: dato en tiempo real
                                  └─▶ Firestore: histórico con timestamp
```

## Build y flash

1. Clona el repo e instala PlatformIO (extensión de VS Code o CLI).
2. Compila y sube el firmware:
   ```
   pio run -t upload
   ```
3. Monitorea por serie (115200 baud):
   ```
   pio device monitor
   ```

## Proyectos relacionados

| Repo | Descripción |
| --- | --- |
| [`InvernaderoNodo`](https://github.com/Crisls24/InvernaderoNodo) | Nodo sensor que envía datos por LoRa a este gateway |
| [`ServicioBioSensor`](https://github.com/Crisls24/ServicioBioSensor) | App Flutter que consume los datos de Firebase |