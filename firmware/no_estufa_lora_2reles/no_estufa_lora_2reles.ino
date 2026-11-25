/*
 * ======================================================================================
 * PROJETO FINAL IoT (UNIFESP) — NÓ ESTUFA REMOTA (VERSÃO 2x MÓDULOS RELÉ DE 2 CANAIS)
 * ======================================================================================
 * Hardware configurado para Heltec WiFi LoRa 32 V3 (ESP32-S3) com 2x Relés Duplos:
 *   - Pino 4  (GPIO 4):  Sensor LDR (ADC1 12 bits)
 *   - Pino 5  (GPIO 5):  Sensor DHT11 (Temperatura e Umidade)
 *   - Pino 6  (GPIO 6):  Servomotor da Persiana (PWM 50 Hz)
 *   - 2x Módulos Relé de 2 Canais (Pinos 47, 48, 33 e 34 — lógica ajustável por módulo!):
 *       * Módulo 1 — D1 (Ventoinha 5V):       Pino 47 (GPIO 47)
 *       * Módulo 1 — D2 (Iluminação Cultivo): Pino 48 (GPIO 48)
 *       * Módulo 2 — D1 (Bomba Irrigação):    Pino 33 (GPIO 33)
 *       * Módulo 2 — D2 (Aquecedor):          Pino 34 (GPIO 34)
 *   - Sinalização Local: Pino 41 (LED Verde Status), Pino 42 (Buzzer Alerta), Pino 40 (LED Vermelho Alarme)
 *
 * Comunicação LoRa 915 MHz (100% compatível com ESP 2 Gateway e Dashboard Web):
 *   - Envia telemetria JSON a cada 5s e imediatamente após receber comandos do ESP 2.
 *   - Recebe ordens do site via ESP 2 (Modo AUTO/MAN, Persiana, 4 Relés, Buzzer e Limiares).
 * ======================================================================================
 */

#include <Arduino.h>
#include <SPI.h>
#include <Wire.h>
#include <WiFi.h>
#include <WebServer.h>
#include <RadioLib.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <DHT.h>
#include <ESP32Servo.h>
#include <ArduinoJson.h>

// ================= 1. PINAGEM HELTEC WIFI LORA 32 V3 (ESP32-S3) =================
#define VEXT_CTRL       36   // LOW liga alimentação 3.3V do OLED
#define OLED_SDA        17
#define OLED_SCL        18
#define OLED_RST        21

#define LORA_NSS        8
#define LORA_SCK        9
#define LORA_MISO       11
#define LORA_MOSI       10
#define LORA_RST        12
#define LORA_BUSY       13
#define LORA_DIO1       14
#define LORA_FREQ_MHZ   915.0

#define LED_ONBOARD     35   // LED Branco onboard da Heltec V3
#define BTN_PRG         0    // Botão PRG onboard (alterna AUTO/MAN)

// --- Pinos Externos dos Sensores e Servo (Lado Direito da Heltec V3: 4, 5, 6) ---
#define PIN_LDR         4    // Pino '4' -> Sensor LDR
#define PIN_DHT         5    // Pino '5' -> Sensor DHT11
#define DHT_TYPE        DHT11
#define PIN_SERVO       6    // Pino '6' -> Servomotor da Persiana

// --- Pinos dos 2 Módulos Relé de 2 Canais (Lado Esquerdo da Heltec V3: 47, 48, 33, 34) ---
// Módulo Relé 1 (2 portas)
#define PIN_RELE_M1_D1  47   // Módulo 1 D1 -> Ventoinha / Exaustor 5V (Pino '47')
#define PIN_RELE_M1_D2  48   // Módulo 1 D2 -> Iluminação Artificial / Cultivo (Pino '48')
// Módulo Relé 2 (2 portas)
#define PIN_RELE_M2_D1  33   // Módulo 2 D1 -> Bomba de Irrigação (Pino '33')
#define PIN_RELE_M2_D2  34   // Módulo 2 D2 -> Aquecedor (Pino '34')

// Sinalização Local de Alarme (Lado Direito da Heltec V3: 40, 41, 42)
#define PIN_LED_VERM    40   // Pino '40' -> LED Vermelho Alarme Crítico
#define PIN_LED_VERDE   41   // Pino '41' -> LED Verde Status Normal
#define PIN_BUZZER      42   // Pino '42' -> Buzzer de Alerta

// ================= 2. OBJETOS E VARIÁVEIS DE ESTADO =================
Adafruit_SSD1306 display(128, 64, &Wire, OLED_RST);
SPIClass loraSpi(FSPI);
SX1262 radio = new Module(LORA_NSS, LORA_DIO1, LORA_RST, LORA_BUSY, loraSpi);
DHT dht(PIN_DHT, DHT_TYPE);
Servo servoPersiana;
WebServer server(80);

float tempAtual = 25.0;
float umidAtual = 60.0;
bool dhtConectado = false;
int adcLdrBruto = 0;
int luzPercent = 50;
float luzFiltrada = 50.0;
bool ldrInicializado = false;

bool INVERTER_LDR = false;

// Lógica independente por módulo relé (false = HIGH 3.3V liga | true = LOW 0V liga)
// Permite usar dois módulos de 2 canais iguais OU com polaridades diferentes!
bool RELE_MOD1_ATIVO_LOW = false; // Módulo 1 (Pinos 47 e 48)
bool RELE_MOD2_ATIVO_LOW = false; // Módulo 2 (Pinos 33 e 34)

bool modoAutomatico = true;
bool servoProporcional = false;
int anguloPersiana = 90;
int zonaLuzAuto = -1; // -1 = inicial, 0 = Escuro (0°), 1 = Normal (90°), 2 = Sol Forte (160°)
unsigned long lastServoAutoMove = 0;
const unsigned long SERVO_AUTO_COOLDOWN_MS = 3000;
const int HYST_LUZ = 5; // Histerese de ±5% evita oscilação e apitos no modo AUTO
bool saidaEstado[4] = {false, false, false, false}; // [0:M1_D1 Vent, 1:M1_D2 Ilum, 2:M2_D1 Irrig, 3:M2_D2 Aquec]
bool alarmeAtivo = false;
bool buzzerMute = false;

float cfgTempMax = 28.0;
float cfgTempCritica = 35.0;
float cfgTempMin = 18.0;
float cfgUmidMin = 45.0;
int cfgLuzBaixa = 25;
int cfgLuzAlta = 75;

uint32_t seqLoRa = 0;
uint32_t cmdRxCount = 0;
float lastCmdRssi = 0.0;
float lastCmdSnr = 0.0;
bool loraOk = false;
bool oledOk = false;
unsigned long lastSensorRead = 0;
unsigned long lastLoRaTx = 0;
const unsigned long SENSOR_INTERVAL_MS = 600;
const unsigned long LORA_TX_INTERVAL_MS = 5000;

bool pendingImmediateTx = false;
unsigned long immediateTxTime = 0;

volatile bool loraRxFlag = false;
#if defined(ESP8266) || defined(ESP32)
  ICACHE_RAM_ATTR
#endif
void onLoRaPacket() {
  loraRxFlag = true;
}

void agendarTelemetriaImediata(unsigned long delayMs = 140) {
  pendingImmediateTx = true;
  immediateTxTime = millis() + delayMs;
}

// ================= 3. ATUAÇÃO FÍSICA (SERVO, 2x RELÉS DUPLOS, LEDS, BUZZER) =================
void escreverPwmServo(int angulo) {
  angulo = constrain(angulo, 0, 180);
  uint32_t pulseUs = map(angulo, 0, 180, 550, 2400);
#if defined(ESP_ARDUINO_VERSION_MAJOR) && (ESP_ARDUINO_VERSION_MAJOR >= 3)
  // PWM nativo LEDC 50 Hz (14 bits) no ESP32-S3 — imune a bugs de timer do ESP32Servo no Core 3.x
  uint32_t duty = (pulseUs * 16383UL) / 20000UL;
  ledcWrite(PIN_SERVO, duty);
#else
  if (!servoPersiana.attached()) {
    servoPersiana.setPeriodHertz(50);
    servoPersiana.attach(PIN_SERVO, 550, 2400);
  }
  servoPersiana.writeMicroseconds(pulseUs);
#endif
  Serial.printf("[SERVO] GPIO %d -> %d graus (%lu us | Luz: %d%%)\n", PIN_SERVO, angulo, pulseUs, luzPercent);
}

void aplicarAnguloPersiana(int novoAngulo, bool emitirBip = true, bool forcarEnvio = false) {
  novoAngulo = constrain(novoAngulo, 0, 180);
  if (forcarEnvio || abs(novoAngulo - anguloPersiana) >= 2) {
    if (emitirBip && !buzzerMute && !servoProporcional) {
      digitalWrite(PIN_BUZZER, HIGH);
      delay(25);
      digitalWrite(PIN_BUZZER, LOW);
    }
    anguloPersiana = novoAngulo;
    escreverPwmServo(anguloPersiana);
  }
}

void atualizarSaidasFisicas() {
  int onMod1  = RELE_MOD1_ATIVO_LOW ? LOW  : HIGH;
  int offMod1 = RELE_MOD1_ATIVO_LOW ? HIGH : LOW;
  int onMod2  = RELE_MOD2_ATIVO_LOW ? LOW  : HIGH;
  int offMod2 = RELE_MOD2_ATIVO_LOW ? HIGH : LOW;

  // Módulo Relé 1 (2 Canais)
  digitalWrite(PIN_RELE_M1_D1, saidaEstado[0] ? onMod1 : offMod1); // Pino 47: Mód.1 D1 (Ventoinha)
  digitalWrite(PIN_RELE_M1_D2, saidaEstado[1] ? onMod1 : offMod1); // Pino 48: Mód.1 D2 (Iluminação)

  // Módulo Relé 2 (2 Canais)
  digitalWrite(PIN_RELE_M2_D1, saidaEstado[2] ? onMod2 : offMod2); // Pino 33: Mód.2 D1 (Irrigação)
  digitalWrite(PIN_RELE_M2_D2, saidaEstado[3] ? onMod2 : offMod2); // Pino 34: Mód.2 D2 (Aquecedor)

  // O LED Branco da própria placa Heltec (GPIO 35) acende se qualquer relé estiver ativo
  digitalWrite(LED_ONBOARD, (saidaEstado[0] || saidaEstado[1] || saidaEstado[2] || saidaEstado[3]) ? HIGH : LOW);

  if (alarmeAtivo) {
    digitalWrite(PIN_LED_VERDE, LOW);
    digitalWrite(PIN_LED_VERM, HIGH);
    digitalWrite(PIN_BUZZER, (!buzzerMute) ? HIGH : LOW);
  } else {
    digitalWrite(PIN_LED_VERDE, HIGH);
    digitalWrite(PIN_LED_VERM, LOW);
    digitalWrite(PIN_BUZZER, LOW);
  }
}

// ================= 4. LEITURA DOS SENSORES E LÓGICA AUTOMÁTICA =================
unsigned long lastDhtRead = 0;

void lerSensores() {
  // Amostra distribuída ao longo de ~16ms para eliminar flicker de lâmpadas 60Hz/120Hz
  long somaAdc = 0;
  for (int i = 0; i < 8; i++) {
    somaAdc += analogRead(PIN_LDR);
    delay(2);
  }
  adcLdrBruto = somaAdc / 8;

  int pct = map(adcLdrBruto, 0, 4095, 0, 100);
  pct = constrain(pct, 0, 100);
  int luzInstantanea = INVERTER_LDR ? (100 - pct) : pct;

  if (!ldrInicializado) {
    luzFiltrada = luzInstantanea;
    ldrInicializado = true;
  } else {
    luzFiltrada = (luzFiltrada * 0.7f) + (luzInstantanea * 0.3f);
  }
  luzPercent = (int)round(luzFiltrada);

  if (millis() - lastDhtRead >= 2000) {
    lastDhtRead = millis();
    float t = dht.readTemperature();
    float h = dht.readHumidity();
    if (!isnan(t) && !isnan(h)) {
      tempAtual = t;
      umidAtual = h;
      dhtConectado = true;
    } else {
      dhtConectado = false;
    }
  }
}

void executarControleAutomatico() {
  if (!modoAutomatico) {
    zonaLuzAuto = -1;
    return;
  }

  if (servoProporcional) {
    int angAlvo = map(luzPercent, 0, 100, 0, 180);
    if (abs(angAlvo - anguloPersiana) >= 8) {
      aplicarAnguloPersiana(angAlvo, false);
    }
    saidaEstado[1] = (luzPercent < cfgLuzBaixa);
  } else {
    // Controle com histerese (±5%) para impedir troca repetitiva de zona e apitos no modo AUTO
    int novaZona = zonaLuzAuto;
    if (zonaLuzAuto < 0) {
      if (luzPercent < cfgLuzBaixa) novaZona = 0;
      else if (luzPercent > cfgLuzAlta) novaZona = 2;
      else novaZona = 1;
    } else if (zonaLuzAuto == 0) {
      if (luzPercent > cfgLuzBaixa + HYST_LUZ) novaZona = 1;
    } else if (zonaLuzAuto == 2) {
      if (luzPercent < cfgLuzAlta - HYST_LUZ) novaZona = 1;
    } else {
      if (luzPercent < cfgLuzBaixa) novaZona = 0;
      else if (luzPercent > cfgLuzAlta) novaZona = 2;
    }

    if (novaZona != zonaLuzAuto && (zonaLuzAuto < 0 || millis() - lastServoAutoMove >= SERVO_AUTO_COOLDOWN_MS)) {
      zonaLuzAuto = novaZona;
      lastServoAutoMove = millis();
      int angAlvo = (zonaLuzAuto == 0) ? 0 : (zonaLuzAuto == 2 ? 160 : 90);
      aplicarAnguloPersiana(angAlvo, false); // Sem bip no ajuste automático!
    }
    saidaEstado[1] = (zonaLuzAuto == 0);
  }

  saidaEstado[0] = (tempAtual > cfgTempMax || umidAtual > 80.0);
  saidaEstado[2] = (umidAtual < cfgUmidMin);
  saidaEstado[3] = (tempAtual < cfgTempMin);

  alarmeAtivo = (tempAtual >= cfgTempCritica);
  if (alarmeAtivo) {
    saidaEstado[0] = true;
  }

  atualizarSaidasFisicas();
}

// ================= 5. DISPLAY OLED =================
void atualizarDisplayOLED() {
  if (!oledOk) return;

  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);

  display.setCursor(0, 0);
  display.print(modoAutomatico ? "[AUTO]" : "[MAN] ");
  display.print(dhtConectado ? " DHT:OK" : " DHT:--");
  display.print(" TX:");
  display.println(seqLoRa);
  display.drawLine(0, 9, 127, 9, SSD1306_WHITE);

  display.setCursor(0, 12);
  display.print("Temp: ");
  display.print(tempAtual, 1);
  display.print("C Umid:");
  display.print(umidAtual, 0);
  display.println("%");

  display.setCursor(0, 23);
  display.print("Luz: ");
  display.print(luzPercent);
  display.print("% (ADC:");
  display.print(adcLdrBruto);
  display.println(")");

  display.setCursor(0, 34);
  display.print("Persiana: ");
  display.print(anguloPersiana);
  display.println(anguloPersiana == 0 ? " gr (Fech)" : (anguloPersiana > 120 ? " gr (Somb)" : " gr (Aber)"));

  display.setCursor(0, 45);
  display.printf("M1:%d%d M2:%d%d",
                 saidaEstado[0], saidaEstado[1], saidaEstado[2], saidaEstado[3]);

  display.setCursor(0, 55);
  if (alarmeAtivo) {
    display.print("ALARME TEMP CRITICA!");
  } else if (cmdRxCount > 0) {
    display.printf("RX:#%lu RSSI:%.0fdBm", cmdRxCount, lastCmdRssi);
  } else {
    display.print("2xRele2CH LoRa->ESP2");
  }

  display.display();
}

// ================= 6. COMUNICAÇÃO LORA 915 MHz (ESP 1 <-> ESP 2 GATEWAY) =================
String montarJsonTelemetria() {
  JsonDocument doc;
  doc["id"] = "EST01";
  doc["seq"] = seqLoRa;
  doc["t"] = serialized(String(tempAtual, 1));
  doc["h"] = serialized(String(umidAtual, 1));
  doc["l"] = luzPercent;
  doc["adc"] = adcLdrBruto;
  doc["srv"] = anguloPersiana;
  JsonArray r = doc["r"].to<JsonArray>();
  for (int i = 0; i < 4; i++) {
    r.add(saidaEstado[i] ? 1 : 0);
  }
  doc["al"] = alarmeAtivo ? 1 : 0;
  doc["buz"] = buzzerMute ? 0 : 1;
  doc["inv"] = (RELE_MOD1_ATIVO_LOW && RELE_MOD2_ATIVO_LOW) ? 1 : 0;
  doc["dht"] = dhtConectado ? 1 : 0;
  doc["m"] = modoAutomatico ? "AUTO" : "MAN";

  String output;
  serializeJson(doc, output);
  return output;
}

void enviarTelemetriaLoRa() {
  if (!loraOk) return;
  seqLoRa++;
  String payload = montarJsonTelemetria();
  int state = radio.transmit(payload.c_str());
  if (state == RADIOLIB_ERR_NONE) {
    Serial.printf("[LoRa TX -> ESP2 #%lu] %s\n", seqLoRa, payload.c_str());
  } else {
    Serial.printf("[LoRa TX ERRO] Codigo: %d\n", state);
  }
  radio.startReceive();
  atualizarDisplayOLED();
}

void processarComandoJson(const String& jsonStr) {
  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, jsonStr);
  if (err) {
    Serial.printf("[LoRa RX ERRO JSON] %s | Payload: %s\n", err.c_str(), jsonStr.c_str());
    return;
  }

  if (doc["id"].is<const char*>() && !doc["cmd"].is<const char*>()) {
    return;
  }

  cmdRxCount++;
  Serial.printf("[LoRa RX <- ESP2 #%lu] %s (RSSI: %.1f dBm | SNR: %.1f dB)\n",
                cmdRxCount, jsonStr.c_str(), lastCmdRssi, lastCmdSnr);

  const char* cmd = doc["cmd"] | "SET";

  if (strcmp(cmd, "PING") == 0) {
    agendarTelemetriaImediata(120);
    atualizarDisplayOLED();
    return;
  }

  if (strcmp(cmd, "CFG") == 0) {
    if (doc["tMax"].is<float>() || doc["tMax"].is<int>()) cfgTempMax = doc["tMax"].as<float>();
    if (doc["tMin"].is<float>() || doc["tMin"].is<int>()) cfgTempMin = doc["tMin"].as<float>();
    if (doc["tCrit"].is<float>() || doc["tCrit"].is<int>()) cfgTempCritica = doc["tCrit"].as<float>();
    if (doc["hMin"].is<float>() || doc["hMin"].is<int>()) cfgUmidMin = doc["hMin"].as<float>();
    if (doc["lMin"].is<int>()) cfgLuzBaixa = constrain(doc["lMin"].as<int>(), 0, 100);
    if (doc["lMax"].is<int>()) cfgLuzAlta = constrain(doc["lMax"].as<int>(), 0, 100);
    if (doc["inv"].is<int>()) {
      bool invVal = (doc["inv"].as<int>() == 1);
      RELE_MOD1_ATIVO_LOW = invVal;
      RELE_MOD2_ATIVO_LOW = invVal;
    }
    if (doc["invLdr"].is<int>()) INVERTER_LDR = (doc["invLdr"].as<int>() == 1);
    if (modoAutomatico) {
      executarControleAutomatico();
    } else {
      atualizarSaidasFisicas();
    }
    atualizarDisplayOLED();
    agendarTelemetriaImediata(140);
    return;
  }

  // Comandos de atuação (SET)
  if (doc["m"].is<const char*>()) {
    modoAutomatico = (String(doc["m"].as<const char*>()) == "AUTO");
  }

  if (doc["srv"].is<int>() || doc["r"].is<JsonArray>() || doc["ch"].is<int>()) {
    if (!doc["m"].is<const char*>() || String(doc["m"].as<const char*>()) != "AUTO") {
      modoAutomatico = false;
    }
  }

  if (modoAutomatico) {
    executarControleAutomatico();
  } else {
    if (doc["srv"].is<int>()) {
      aplicarAnguloPersiana(doc["srv"].as<int>(), true, true);
    }
    if (doc["r"].is<JsonArray>()) {
      JsonArray arr = doc["r"].as<JsonArray>();
      for (int i = 0; i < 4 && i < arr.size(); i++) {
        saidaEstado[i] = (arr[i].as<int>() == 1);
      }
    }
    if (doc["ch"].is<int>()) {
      int ch = constrain(doc["ch"].as<int>(), 0, 3);
      if (doc["state"].is<int>()) {
        saidaEstado[ch] = (doc["state"].as<int>() == 1);
      } else if (doc["state"].is<bool>()) {
        saidaEstado[ch] = doc["state"].as<bool>();
      } else {
        saidaEstado[ch] = !saidaEstado[ch];
      }
    }
  }

  if (doc["buz"].is<int>()) {
    buzzerMute = (doc["buz"].as<int>() == 0);
  }
  if (doc["inv"].is<int>()) {
    bool invVal = (doc["inv"].as<int>() == 1);
    RELE_MOD1_ATIVO_LOW = invVal;
    RELE_MOD2_ATIVO_LOW = invVal;
  }

  atualizarSaidasFisicas();
  atualizarDisplayOLED();
  agendarTelemetriaImediata(140);
}

void verificarComandosLoRa() {
  if (!loraOk || !loraRxFlag) return;
  loraRxFlag = false;
  String rxStr;
  int state = radio.readData(rxStr);
  if (state == RADIOLIB_ERR_NONE && rxStr.length() > 0) {
    lastCmdRssi = radio.getRSSI();
    lastCmdSnr = radio.getSNR();
    processarComandoJson(rxStr);
  }
  radio.startReceive();
}

// ================= 7. SERVIDOR WEB DE VALIDAÇÃO DE BANCADA (192.168.4.1) =================
void configurarRotasWeb() {
  server.on("/", HTTP_GET, []() {
    String html = "<!DOCTYPE html><html lang='pt-BR'><head><meta charset='UTF-8'>";
    html += "<meta name='viewport' content='width=device-width,initial-scale=1'>";
    html += "<meta http-equiv='refresh' content='3'>";
    html += "<title>Nó Estufa — 2x Relés Duplos (Heltec V3)</title>";
    html += "<style>body{font-family:sans-serif;background:#0f172a;color:#f8fafc;padding:14px;max-width:520px;margin:auto;}";
    html += ".card{background:#1e293b;padding:14px;border-radius:10px;margin-bottom:12px;}";
    html += ".grid{display:grid;grid-template-columns:1fr 1fr;gap:8px;}";
    html += ".btn{background:#22c55e;color:#000;border:none;padding:10px;border-radius:6px;font-weight:bold;cursor:pointer;width:100%;margin-top:6px;}";
    html += ".btn-sec{background:#38bdf8;color:#000;border:none;padding:8px;border-radius:6px;font-weight:bold;cursor:pointer;width:100%;margin-top:6px;}";
    html += ".btn-off{background:#475569;color:#fff;}";
    html += "</style></head><body>";
    html += "<h2>ESP 1: Nó Estufa — 2x Relés de 2 Canais</h2>";

    html += "<div class='card'><div class='grid'>";
    html += "<div><b>Temp (DHT11 P5):</b><br>" + String(tempAtual, 1) + " °C " + String(dhtConectado ? "(OK)" : "(Simulado)") + "</div>";
    html += "<div><b>Umidade (DHT11 P5):</b><br>" + String(umidAtual, 1) + " %</div>";
    html += "<div><b>Luz (LDR P4):</b><br>" + String(luzPercent) + "% (ADC: " + String(adcLdrBruto) + ")</div>";
    html += "<div><b>Persiana (Servo P6):</b><br>" + String(anguloPersiana) + "°</div>";
    html += "<div><b>LoRa TX -> ESP2:</b><br>#" + String(seqLoRa) + "</div>";
    html += "<div><b>Comandos RX <- ESP2:</b><br>#" + String(cmdRxCount) + " (" + String(lastCmdRssi, 0) + " dBm)</div>";
    html += "</div></div>";

    html += "<div class='card'><h3>Modo Atual: " + String(modoAutomatico ? "AUTOMÁTICO" : "MANUAL") + "</h3>";
    html += "<form action='/modo' method='POST'><button class='btn' type='submit'>Alternar para Modo " + String(modoAutomatico ? "MANUAL" : "AUTOMÁTICO") + "</button></form></div>";

    html += "<div class='card'><h3>Controle Manual da Persiana (Servo P6)</h3><div class='grid'>";
    html += "<form action='/servo?ang=0' method='POST'><button class='btn' type='submit'>Fechar (0°)</button></form>";
    html += "<form action='/servo?ang=45' method='POST'><button class='btn' type='submit'>Meia-Luz (45°)</button></form>";
    html += "<form action='/servo?ang=90' method='POST'><button class='btn' type='submit'>Abrir (90°)</button></form>";
    html += "<form action='/servo?ang=160' method='POST'><button class='btn' type='submit'>Sombra (160°)</button></form>";
    html += "</div></div>";

    // Card Módulo Relé 1 (Pinos 47 e 48)
    html += "<div class='card'><h3>Módulo Relé 1 — 2 Canais (Pinos 47 e 48)</h3><div class='grid'>";
    html += "<form action='/rele?ch=0' method='POST'><button class='btn " + String(saidaEstado[0] ? "" : "btn-off") + "' type='submit'>M1 D1 Ventoinha (P47): " + String(saidaEstado[0] ? "ON" : "OFF") + "</button></form>";
    html += "<form action='/rele?ch=1' method='POST'><button class='btn " + String(saidaEstado[1] ? "" : "btn-off") + "' type='submit'>M1 D2 Iluminação (P48): " + String(saidaEstado[1] ? "ON" : "OFF") + "</button></form>";
    html += "</div><form action='/invmod?m=1' method='POST'><button class='btn-sec' type='submit'>Lógica Mód.1: " + String(RELE_MOD1_ATIVO_LOW ? "Ativo em LOW (0V Liga)" : "Ativo em HIGH (3.3V Liga)") + "</button></form></div>";

    // Card Módulo Relé 2 (Pinos 33 e 34)
    html += "<div class='card'><h3>Módulo Relé 2 — 2 Canais (Pinos 33 e 34)</h3><div class='grid'>";
    html += "<form action='/rele?ch=2' method='POST'><button class='btn " + String(saidaEstado[2] ? "" : "btn-off") + "' type='submit'>M2 D1 Irrigação (P33): " + String(saidaEstado[2] ? "ON" : "OFF") + "</button></form>";
    html += "<form action='/rele?ch=3' method='POST'><button class='btn " + String(saidaEstado[3] ? "" : "btn-off") + "' type='submit'>M2 D2 Aquecedor (P34): " + String(saidaEstado[3] ? "ON" : "OFF") + "</button></form>";
    html += "</div><form action='/invmod?m=2' method='POST'><button class='btn-sec' type='submit'>Lógica Mód.2: " + String(RELE_MOD2_ATIVO_LOW ? "Ativo em LOW (0V Liga)" : "Ativo em HIGH (3.3V Liga)") + "</button></form></div>";

    html += "</body></html>";
    server.send(200, "text/html", html);
  });

  server.on("/modo", HTTP_POST, []() {
    modoAutomatico = !modoAutomatico;
    if (modoAutomatico) executarControleAutomatico();
    atualizarDisplayOLED();
    agendarTelemetriaImediata(80);
    server.sendHeader("Location", "/");
    server.send(303);
  });

  server.on("/invmod", HTTP_POST, []() {
    int m = server.arg("m").toInt();
    if (m == 1) {
      RELE_MOD1_ATIVO_LOW = !RELE_MOD1_ATIVO_LOW;
    } else if (m == 2) {
      RELE_MOD2_ATIVO_LOW = !RELE_MOD2_ATIVO_LOW;
    }
    atualizarSaidasFisicas();
    atualizarDisplayOLED();
    agendarTelemetriaImediata(80);
    server.sendHeader("Location", "/");
    server.send(303);
  });

  server.on("/servo", HTTP_POST, []() {
    modoAutomatico = false;
    int ang = server.arg("ang").toInt();
    aplicarAnguloPersiana(ang, true, true);
    atualizarDisplayOLED();
    agendarTelemetriaImediata(80);
    server.sendHeader("Location", "/");
    server.send(303);
  });

  server.on("/rele", HTTP_POST, []() {
    modoAutomatico = false;
    int ch = constrain(server.arg("ch").toInt(), 0, 3);
    saidaEstado[ch] = !saidaEstado[ch];
    atualizarSaidasFisicas();
    atualizarDisplayOLED();
    agendarTelemetriaImediata(80);
    server.sendHeader("Location", "/");
    server.send(303);
  });

  server.begin();
}

// ================= 8. SETUP E LOOP PRINCIPAL =================
void setup() {
  Serial.begin(115200);
  delay(300);

  pinMode(LED_ONBOARD, OUTPUT);
  pinMode(BTN_PRG, INPUT_PULLUP);
  pinMode(PIN_RELE_M1_D1, OUTPUT);
  pinMode(PIN_RELE_M1_D2, OUTPUT);
  pinMode(PIN_RELE_M2_D1, OUTPUT);
  pinMode(PIN_RELE_M2_D2, OUTPUT);
  pinMode(PIN_LED_VERM, OUTPUT);
  pinMode(PIN_BUZZER, OUTPUT);
  pinMode(PIN_LED_VERDE, OUTPUT);
  atualizarSaidasFisicas();

  analogReadResolution(12);
  pinMode(PIN_LDR, INPUT);
  dht.begin();

#if defined(ESP_ARDUINO_VERSION_MAJOR) && (ESP_ARDUINO_VERSION_MAJOR >= 3)
  ledcAttach(PIN_SERVO, 50, 14);
#else
  ESP32PWM::allocateTimer(0);
  ESP32PWM::allocateTimer(1);
  ESP32PWM::allocateTimer(2);
  ESP32PWM::allocateTimer(3);
  servoPersiana.setPeriodHertz(50);
  servoPersiana.attach(PIN_SERVO, 550, 2400);
#endif
  escreverPwmServo(anguloPersiana);

  pinMode(VEXT_CTRL, OUTPUT);
  digitalWrite(VEXT_CTRL, LOW);
  delay(40);
  Wire.begin(OLED_SDA, OLED_SCL);
  oledOk = display.begin(SSD1306_SWITCHCAPVCC, 0x3C);

  loraSpi.begin(LORA_SCK, LORA_MISO, LORA_MOSI, LORA_NSS);
#if defined(RADIOLIB_UNIT_MEGA)
  ConfigLoRa_t loraCfg;
  loraCfg.frequency = RADIOLIB_UNIT_MEGA(915);
  loraCfg.bandwidth = RADIOLIB_UNIT_KILO(125);
  loraCfg.spreadingFactor = 7;
  loraCfg.codingRate = 5;
  loraCfg.syncWord = RADIOLIB_LORA_SYNC_WORD_PRIVATE;
  loraCfg.power = 14;
  loraCfg.preambleLength = 8;
  int loraState = radio.begin(loraCfg);
#else
  int loraState = radio.begin(LORA_FREQ_MHZ, 125.0, 7, 5, RADIOLIB_SX126X_SYNC_WORD_PRIVATE, 14, 8);
#endif
  if (loraState == RADIOLIB_ERR_NONE) {
    loraOk = true;
    radio.setDio1Action(onLoRaPacket);
    radio.startReceive();
    Serial.println("[LoRa SX1262] Iniciado em 915 MHz (Versao 2x Reles de 2 Canais).");
  } else {
    Serial.printf("[ERRO LoRa] Falha ao iniciar SX1262: %d\n", loraState);
  }

  WiFi.mode(WIFI_AP);
  WiFi.softAP("Estufa_LoRa_V3", "12345678");
  configurarRotasWeb();

  lerSensores();
  executarControleAutomatico();
  atualizarDisplayOLED();
  enviarTelemetriaLoRa();
}

void loop() {
  server.handleClient();
  verificarComandosLoRa();

  if (pendingImmediateTx && millis() >= immediateTxTime) {
    pendingImmediateTx = false;
    lastLoRaTx = millis();
    enviarTelemetriaLoRa();
  }

  if (millis() - lastSensorRead >= SENSOR_INTERVAL_MS) {
    lastSensorRead = millis();
    lerSensores();
    executarControleAutomatico();
    atualizarDisplayOLED();
  }

  if (millis() - lastLoRaTx >= LORA_TX_INTERVAL_MS) {
    lastLoRaTx = millis();
    enviarTelemetriaLoRa();
  }

  if (digitalRead(BTN_PRG) == LOW) {
    delay(50);
    if (digitalRead(BTN_PRG) == LOW) {
      modoAutomatico = !modoAutomatico;
      if (modoAutomatico) executarControleAutomatico();
      atualizarDisplayOLED();
      agendarTelemetriaImediata(80);
      while (digitalRead(BTN_PRG) == LOW) {
        server.handleClient();
        delay(10);
      }
    }
  }
}
