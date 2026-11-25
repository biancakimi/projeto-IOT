/*
 * ======================================================================================
 * PROJETO FINAL IoT (UNIFESP) — NÓ GATEWAY CENTRAL (ESP 2: Heltec WiFi LoRa 32 V3)
 * ======================================================================================
 * Descrição:
 *   Firmware do ESP 2 responsável por atuar como Ponte Bidirecional (LoRa 915 MHz <-> MQTT & Vercel):
 *   1. Recebe os pacotes LoRa 915 MHz enviados pelo ESP 1 (Nó Estufa).
 *   2. Captura métricas de qualidade do enlace de rádio (RSSI em dBm e SNR em dB).
 *   3. Conecta-se ao Wi-Fi e sobe os dados simultaneamente:
 *      a) Em tempo real para o Broker MQTT (Tópico: unifesp/iot/estufa/telemetria).
 *      b) Diretamente via HTTPS POST para a API da Vercel (/api/historico -> Neon PostgreSQL).
 *   4. Recebe ordens do Site Web (tanto via MQTT quanto via resposta HTTPS da Vercel)
 *      e transmite imediatamente para o ESP 1 via LoRa 915 MHz.
 *   5. Mantém um Portal Web Local (AP: Gateway_LoRa_V3 -> http://192.168.4.1) para
 *      configurar Wi-Fi, MQTT e a URL da Vercel sem precisar regravar código.
 *   6. Suporta Atualização Remota de Firmware (ArduinoOTA).
 * ======================================================================================
 */

#include <Arduino.h>
#include <SPI.h>
#include <Wire.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <WebServer.h>
#include <Preferences.h>
#include <ArduinoOTA.h>
#include <RadioLib.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

// ================= 1. PINAGEM HELTEC WIFI LORA 32 V3 (ESP32-S3) =================
#define VEXT_CTRL       36   // LOW liga alimentação 3.3V do OLED
#define OLED_SDA        17
#define OLED_SCL        18
#define OLED_RST        21

#define LORA_NSS        8
#define LORA_SCK        9
#define LORA_MOSI       10
#define LORA_MISO       11
#define LORA_RST        12
#define LORA_BUSY       13
#define LORA_DIO1       14
#define LORA_FREQ_MHZ   915.0

#define LED_ONBOARD     35   // LED Branco onboard da Heltec V3
#define BTN_PRG         0    // Botão PRG onboard (dispara PING para o ESP 1)

// ================= 2. CREDENCIAIS PADRÃO (TAMBÉM CONFIGURÁVEIS PELO SITE 192.168.4.1) =================
const char* DEFAULT_WIFI_SSID   = "Yuji G75";
const char* DEFAULT_WIFI_PASS   = "bncrsa6yp5wfm3r";

// Broker MQTT Público compatível com TCP (1883 no ESP32) e WSS (8084 no Site Web HTTPS da Vercel)
const char* DEFAULT_MQTT_HOST   = "broker.emqx.io";
const uint16_t DEFAULT_MQTT_PORT = 1883;

// URL da API Serverless na Vercel (também editável pelo celular em http://192.168.4.1)
const char* DEFAULT_VERCEL_URL  = "https://projeto-iot-eight.vercel.app/api/historico";

// Ponto de Acesso local do ESP 2 para configuração e diagnóstico
const char* GW_AP_SSID          = "Gateway_LoRa_V3";
const char* GW_AP_PASS          = "12345678";

// Tópicos MQTT (mesmos tópicos usados pelo Site Web na Vercel e Node-RED)
const char* TOPIC_TELEMETRIA    = "unifesp/iot/estufa/telemetria";
const char* TOPIC_GW_STATUS     = "unifesp/iot/estufa/gateway/status";
const char* TOPIC_CMD_ROOT      = "unifesp/iot/estufa/comando";
const char* TOPIC_CMD_WILDCARD  = "unifesp/iot/estufa/comando/#";

// ================= 3. OBJETOS E VARIÁVEIS GLOBAIS =================
Adafruit_SSD1306 display(128, 64, &Wire, OLED_RST);
SPIClass loraSpi(FSPI);
SX1262 radio = new Module(LORA_NSS, LORA_DIO1, LORA_RST, LORA_BUSY, loraSpi);
WiFiClient espWifiClient;
PubSubClient mqtt(espWifiClient);
WebServer server(80);
Preferences prefs;

String wifiSsid = "";
String wifiPass = "";
String mqttHost = "";
uint16_t mqttPort = 1883;
String vercelApiUrl = "";
String gwNodeId = "";

bool oledOk = false;
bool loraOk = false;
bool otaStarted = false;

// Métricas LoRa, MQTT e Vercel
uint32_t loraRxCount = 0;
uint32_t loraTxCount = 0;
uint32_t mqttPubCount = 0;
uint32_t mqttCmdCount = 0;
uint32_t vercelPostCount = 0;
int lastVercelHttpCode = 0;
float lastLoRaRssi = 0.0;
float lastLoRaSnr = 0.0;
unsigned long lastLoRaRxMillis = 0;
String lastLoRaPayload = "{}";
String lastCmdSent = "Nenhum";

// Últimos dados recebidos do ESP 1 (Nó Estufa)
uint32_t estSeq = 0;
float estTemp = 0.0;
float estUmid = 0.0;
int estLuz = 0;
int estAdc = 0;
int estServo = 90;
int estReles[4] = {0, 0, 0, 0};
int estAlarme = 0;
int estBuzzer = 1;
int estInvRele = 0;
String estModo = "AUTO";
bool estDadosRecebidos = false;

// Temporizadores
unsigned long lastWifiCheck = 0;
unsigned long lastMqttRetry = 0;
unsigned long lastOledUpdate = 0;
unsigned long lastGwHeartbeat = 0;
unsigned long lastVercelSync = 0;
bool pendingVercelPost = false;

// Fila de comando LoRa pendente
bool pendingLoRaCmd = false;
String pendingLoRaCmdPayload = "";

volatile bool loraRxFlag = false;
#if defined(ESP8266) || defined(ESP32)
  ICACHE_RAM_ATTR
#endif
void onLoRaReceive() {
  loraRxFlag = true;
}

void enfileirarComandoLoRa(const String& jsonStr) {
  pendingLoRaCmdPayload = jsonStr;
  pendingLoRaCmd = true;
}

// ================= 4. DISPLAY OLED DO GATEWAY (ESP 2) =================
void atualizarDisplayOLED() {
  if (!oledOk) return;

  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);

  // Linha 1: Status Wi-Fi, MQTT e Vercel
  display.setCursor(0, 0);
  display.print("GW2 W:");
  display.print(WiFi.status() == WL_CONNECTED ? "OK" : "AP");
  display.print(" MQ:");
  display.print(mqtt.connected() ? "OK" : "--");
  display.print(" VC:");
  display.println(vercelPostCount);
  display.drawLine(0, 9, 127, 9, SSD1306_WHITE);

  // Linha 2: IP na rede Wi-Fi ou IP do AP
  display.setCursor(0, 12);
  if (WiFi.status() == WL_CONNECTED) {
    display.print("IP:");
    display.println(WiFi.localIP());
  } else {
    display.print("AP:192.168.4.1 (Cfg)");
  }

  // Linha 3: Contadores LoRa RX (vindos do ESP1) e TX (ordens p/ ESP1) + RSSI
  display.setCursor(0, 22);
  display.printf("RX:%lu TX:%lu ", loraRxCount, loraTxCount);
  if (loraRxCount > 0) {
    display.printf("%.0fdBm", lastLoRaRssi);
  } else {
    display.print("Aguard.");
  }

  // Linha 4: Dados climáticos recebidos do ESP 1
  display.setCursor(0, 33);
  if (estDadosRecebidos) {
    display.printf("T:%.1fC U:%.0f%% L:%d%%", estTemp, estUmid, estLuz);
  } else {
    display.print("Aguardando ESP1 LoRa");
  }

  // Linha 5: Estado dos Atuadores do ESP 1
  display.setCursor(0, 44);
  if (estDadosRecebidos) {
    display.printf("Srv:%d R:%d%d%d%d [%s]",
                   estServo, estReles[0], estReles[1], estReles[2], estReles[3], estModo.c_str());
  } else {
    display.print("Freq: 915MHz SF7 BW125");
  }

  // Linha 6: Último comando encaminhado
  display.setCursor(0, 55);
  display.print("Cmd:");
  display.print(lastCmdSent.substring(0, 16));

  display.display();
}

// ================= 5. ENVIO DIRETO HTTPS PARA A API DA VERCEL (/api/historico) =================
void enviarParaVercelHTTPS() {
  if (WiFi.status() != WL_CONNECTED || vercelApiUrl.length() < 10 || lastLoRaPayload.length() < 5) {
    return;
  }

  WiFiClientSecure clientSecure;
  clientSecure.setInsecure();
  HTTPClient https;
  https.setTimeout(3500);

  if (https.begin(clientSecure, vercelApiUrl)) {
    https.addHeader("Content-Type", "application/json");
    int httpCode = https.POST(lastLoRaPayload);
    lastVercelHttpCode = httpCode;

    if (httpCode >= 200 && httpCode < 300) {
      vercelPostCount++;
      String resp = https.getString();
      Serial.printf("[VERCEL HTTPS #%lu -> %d] %s\n", vercelPostCount, httpCode, resp.c_str());

      // Se a API da Vercel devolveu uma ordem pendente do site no campo "cmd", envia via LoRa ao ESP 1!
      JsonDocument respDoc;
      if (deserializeJson(respDoc, resp) == DeserializationError::Ok && respDoc["cmd"].is<JsonObject>()) {
        String cmdStr;
        serializeJson(respDoc["cmd"], cmdStr);
        Serial.printf("[VERCEL -> LoRa ESP1] Ordem recebida da Vercel: %s\n", cmdStr.c_str());
        enfileirarComandoLoRa(cmdStr);
      }
    } else {
      Serial.printf("[VERCEL HTTPS AVISO] HTTP %d (%s)\n", httpCode, vercelApiUrl.c_str());
    }
    https.end();
  }
  radio.startReceive();
}

// ================= 6. TRANSMISSÃO E RECEPÇÃO LORA 915 MHz =================
void transmitirComandoLoRa(const String& jsonCmd) {
  if (!loraOk || jsonCmd.length() == 0) return;

  digitalWrite(LED_ONBOARD, HIGH);
  loraTxCount++;
  lastCmdSent = jsonCmd;

  Serial.printf("[LoRa TX -> ESP1 #%lu] %s\n", loraTxCount, jsonCmd.c_str());
  int state = radio.transmit(jsonCmd.c_str());
  if (state != RADIOLIB_ERR_NONE) {
    Serial.printf("[LoRa TX ERRO] Codigo RadioLib: %d\n", state);
  }

  // Volta imediatamente para modo de escuta para receber a confirmação do ESP 1
  radio.startReceive();
  digitalWrite(LED_ONBOARD, LOW);
  atualizarDisplayOLED();
}

void processarPacoteEstufaLoRa(const String& rawStr) {
  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, rawStr);
  if (err) {
    Serial.printf("[LoRa RX] Pacote nao-JSON ignorado: %s\n", rawStr.c_str());
    return;
  }

  // Extrai campos enviados pelo ESP 1
  estDadosRecebidos = true;
  estSeq = doc["seq"] | (estSeq + 1);
  estTemp = doc["t"] | 0.0f;
  estUmid = doc["h"] | 0.0f;
  estLuz = doc["l"] | 0;
  estAdc = doc["adc"] | 0;
  estServo = doc["srv"] | 90;
  if (doc["r"].is<JsonArray>()) {
    JsonArray arr = doc["r"].as<JsonArray>();
    for (int i = 0; i < 4 && i < arr.size(); i++) {
      estReles[i] = arr[i].as<int>();
    }
  }
  estAlarme = doc["al"] | 0;
  estBuzzer = doc["buz"] | 1;
  estInvRele = doc["inv"] | 0;
  if (doc["m"].is<const char*>()) {
    estModo = String(doc["m"].as<const char*>());
  }

  // Enriquece o JSON com métricas de rádio capturadas pelo ESP 2 (Gateway)
  doc["rssi"] = (int)round(lastLoRaRssi);
  doc["snr"] = serialized(String(lastLoRaSnr, 1));
  doc["gwId"] = gwNodeId;
  doc["gwRx"] = loraRxCount;

  String enrichedJson;
  serializeJson(doc, enrichedJson);
  lastLoRaPayload = enrichedJson;

  // 1. Publica imediatamente no Broker MQTT em nuvem para o Site Web na Vercel
  if (mqtt.connected()) {
    if (mqtt.publish(TOPIC_TELEMETRIA, enrichedJson.c_str(), true)) {
      mqttPubCount++;
      Serial.printf("[MQTT PUB #%lu -> %s] %s\n", mqttPubCount, TOPIC_TELEMETRIA, enrichedJson.c_str());
    } else {
      Serial.println("[MQTT ERRO] Falha ao publicar telemetria.");
    }
  }

  // 2. Agenda envio HTTPS direto para a API /api/historico da Vercel (a cada 10s)
  if (millis() - lastVercelSync >= 10000 || vercelPostCount == 0) {
    pendingVercelPost = true;
  }
}

void verificarRecepcaoLoRa() {
  if (!loraOk || !loraRxFlag) return;
  loraRxFlag = false;

  String rxStr;
  int state = radio.readData(rxStr);
  if (state == RADIOLIB_ERR_NONE && rxStr.length() > 0) {
    digitalWrite(LED_ONBOARD, HIGH);
    loraRxCount++;
    lastLoRaRxMillis = millis();
    lastLoRaRssi = radio.getRSSI();
    lastLoRaSnr = radio.getSNR();

    Serial.printf("[LoRa RX <- ESP1 #%lu] RSSI: %.1f dBm | SNR: %.1f dB | Payload: %s\n",
                  loraRxCount, lastLoRaRssi, lastLoRaSnr, rxStr.c_str());

    processarPacoteEstufaLoRa(rxStr);
    atualizarDisplayOLED();
    digitalWrite(LED_ONBOARD, LOW);
  }
  radio.startReceive();
}

// ================= 7. CALLBACK E CONEXÃO MQTT (NUVEM <-> GATEWAY) =================
void onMqttMessage(char* topic, byte* payload, unsigned int length) {
  String msg;
  msg.reserve(length + 1);
  for (unsigned int i = 0; i < length; i++) {
    msg += (char)payload[i];
  }
  msg.trim();
  String topStr = String(topic);

  mqttCmdCount++;
  Serial.printf("[MQTT RX <- SITE #%lu] Topico: %s | Msg: %s\n",
                mqttCmdCount, topStr.c_str(), msg.c_str());

  // 1. Se já vier um JSON completo em unifesp/iot/estufa/comando ou subtópicos
  if (msg.startsWith("{") && msg.endsWith("}")) {
    JsonDocument doc;
    if (deserializeJson(doc, msg) == DeserializationError::Ok) {
      if (topStr.endsWith("/config") && !doc["cmd"].is<const char*>()) {
        doc["cmd"] = "CFG";
      } else if (!doc["cmd"].is<const char*>()) {
        doc["cmd"] = "SET";
      }
      String out;
      serializeJson(doc, out);
      enfileirarComandoLoRa(out);
      return;
    }
  }

  // 2. Comandos simples por subtópico (Node-RED / Site Web)
  JsonDocument cmdDoc;
  cmdDoc["cmd"] = "SET";

  if (topStr.endsWith("/modo")) {
    String modoUpper = msg;
    modoUpper.toUpperCase();
    cmdDoc["m"] = (modoUpper.indexOf("AUTO") >= 0) ? "AUTO" : "MAN";
  } else if (topStr.endsWith("/persiana")) {
    cmdDoc["m"] = "MAN";
    cmdDoc["srv"] = constrain(msg.toInt(), 0, 180);
  } else if (topStr.endsWith("/buzzer")) {
    cmdDoc["buz"] = (msg == "1" || msg.equalsIgnoreCase("ON") || msg.equalsIgnoreCase("true")) ? 1 : 0;
  } else if (topStr.endsWith("/invrele")) {
    cmdDoc["inv"] = (msg == "1" || msg.equalsIgnoreCase("LOW") || msg.equalsIgnoreCase("true")) ? 1 : 0;
  } else {
    return;
  }

  String out;
  serializeJson(cmdDoc, out);
  enfileirarComandoLoRa(out);
}

void publicarStatusGateway() {
  if (!mqtt.connected()) return;
  JsonDocument doc;
  doc["gwId"] = gwNodeId;
  doc["online"] = true;
  doc["ip"] = WiFi.localIP().toString();
  doc["wifiRssi"] = WiFi.RSSI();
  doc["loraRx"] = loraRxCount;
  doc["loraTx"] = loraTxCount;
  doc["vercelPosts"] = vercelPostCount;
  doc["lastRssi"] = (int)round(lastLoRaRssi);
  doc["lastSnr"] = serialized(String(lastLoRaSnr, 1));
  doc["uptimeSec"] = millis() / 1000;

  String out;
  serializeJson(doc, out);
  mqtt.publish(TOPIC_GW_STATUS, out.c_str(), true);
}

void gerenciarConexaoMqtt() {
  if (WiFi.status() != WL_CONNECTED) return;

  if (mqtt.connected()) {
    mqtt.loop();
    if (millis() - lastGwHeartbeat >= 15000) {
      lastGwHeartbeat = millis();
      publicarStatusGateway();
    }
    return;
  }

  if (millis() - lastMqttRetry < 4000) return;
  lastMqttRetry = millis();

  mqtt.setServer(mqttHost.c_str(), mqttPort);
  mqtt.setCallback(onMqttMessage);
  mqtt.setBufferSize(512);

  String clientId = "UNIFESP-GW2-" + gwNodeId + "-" + String(random(1000, 9999));
  Serial.printf("[MQTT] Conectando ao broker %s:%u como %s...\n",
                mqttHost.c_str(), mqttPort, clientId.c_str());

  if (mqtt.connect(clientId.c_str())) {
    Serial.println("[MQTT] Conectado com sucesso!");
    mqtt.subscribe(TOPIC_CMD_ROOT);
    mqtt.subscribe(TOPIC_CMD_WILDCARD);
    publicarStatusGateway();
    if (estDadosRecebidos && lastLoRaPayload.length() > 2) {
      mqtt.publish(TOPIC_TELEMETRIA, lastLoRaPayload.c_str(), true);
    }
    atualizarDisplayOLED();
  } else {
    Serial.printf("[MQTT ERRO] Falha rc=%d. Tentando novamente em 4s...\n", mqtt.state());
  }
}

// ================= 8. WI-FI E PORTAL WEB DO GATEWAY (CONFIGURAÇÃO + TESTE) =================
void iniciarOTA() {
  if (otaStarted || WiFi.status() != WL_CONNECTED) return;
  ArduinoOTA.setHostname(("SmartGreenhouse-GW-" + gwNodeId).c_str());
  ArduinoOTA.onStart([]() {
    Serial.println("[OTA] Iniciando atualizacao remota de firmware...");
  });
  ArduinoOTA.onEnd([]() {
    Serial.println("\n[OTA] Concluido!");
  });
  ArduinoOTA.onError([](ota_error_t error) {
    Serial.printf("[OTA ERRO] Codigo: %u\n", error);
  });
  ArduinoOTA.begin();
  otaStarted = true;
  Serial.println("[OTA] Servico ArduinoOTA ativo.");
}

void conectarWiFi() {
  WiFi.setSleep(false);
  WiFi.setAutoReconnect(true);
  WiFi.setTxPower(WIFI_POWER_15dBm);

  int targetChannel = 1;
  Serial.println("[Wi-Fi] Escaneando redes 2.4 GHz disponiveis...");
  int n = WiFi.scanNetworks();
  bool ssidEncontrado = false;
  for (int i = 0; i < n; i++) {
    Serial.printf("  -> SSID: '%s' (%d dBm, Canal %d)\n",
                  WiFi.SSID(i).c_str(), WiFi.RSSI(i), WiFi.channel(i));
    if (WiFi.SSID(i) == wifiSsid) {
      ssidEncontrado = true;
      targetChannel = WiFi.channel(i);
    }
  }
  WiFi.scanDelete();

  // Inicia o AP no mesmo canal da rede alvo para evitar queda de handshake ("sta bss deleted")
  WiFi.softAP(GW_AP_SSID, GW_AP_PASS, targetChannel);
  Serial.printf("[Wi-Fi AP] Portal do Gateway ativo em '%s' (Senha: %s, Canal %d) -> http://192.168.4.1\n",
                GW_AP_SSID, GW_AP_PASS, targetChannel);

  if (wifiSsid.length() == 0 || wifiSsid == "SEU_WIFI_AQUI") {
    Serial.println("[Wi-Fi] Configure o SSID em http://192.168.4.1");
    return;
  }

  if (!ssidEncontrado) {
    Serial.printf("[Wi-Fi AVISO] A rede '%s' nao apareceu no scan 2.4 GHz! Verifique se o roteador/hotspot do celular esta ligado e em 2.4 GHz.\n",
                  wifiSsid.c_str());
  }

  Serial.printf("[Wi-Fi] Conectando a '%s'...\n", wifiSsid.c_str());
  if (wifiPass.length() > 0) {
    WiFi.begin(wifiSsid.c_str(), wifiPass.c_str());
  } else {
    WiFi.begin(wifiSsid.c_str());
  }
}

void configurarRotasWebGateway() {
  server.on("/", HTTP_GET, []() {
    String html = "<!DOCTYPE html><html lang='pt-BR'><head><meta charset='UTF-8'>";
    html += "<meta name='viewport' content='width=device-width,initial-scale=1'>";
    html += "<title>ESP 2 Gateway LoRa <-> MQTT & Vercel</title>";
    html += "<style>body{font-family:sans-serif;background:#0f172a;color:#f8fafc;padding:14px;max-width:540px;margin:auto;}";
    html += ".card{background:#1e293b;padding:14px;border-radius:10px;margin-bottom:12px;}";
    html += ".grid{display:grid;grid-template-columns:1fr 1fr;gap:8px;}";
    html += ".btn{background:#22c55e;color:#000;border:none;padding:10px;border-radius:6px;font-weight:bold;cursor:pointer;width:100%;margin-top:6px;}";
    html += ".btn-sec{background:#38bdf8;color:#000;border:none;padding:8px;border-radius:6px;font-weight:bold;cursor:pointer;width:100%;margin-top:6px;}";
    html += "input{width:100%;padding:8px;box-sizing:border-box;border-radius:6px;border:1px solid #475569;background:#0f172a;color:#fff;margin-top:4px;margin-bottom:8px;}";
    html += "</style></head><body>";
    html += "<h2>ESP 2: Nó Gateway (LoRa 915 MHz &harr; MQTT &amp; Vercel)</h2>";

    html += "<div class='card'><div class='grid'>";
    html += "<div><b>Wi-Fi:</b><br>" + String(WiFi.status() == WL_CONNECTED ? ("Conectado (" + WiFi.localIP().toString() + ")") : "Apenas AP (192.168.4.1)") + "</div>";
    html += "<div><b>Broker MQTT:</b><br>" + String(mqtt.connected() ? "ONLINE" : "OFFLINE") + " (Pubs: " + String(mqttPubCount) + ")</div>";
    html += "<div><b>LoRa RX (do ESP 1):</b><br>#" + String(loraRxCount) + " (" + String(lastLoRaRssi, 0) + " dBm / SNR " + String(lastLoRaSnr, 1) + ")</div>";
    html += "<div><b>Vercel API Posts:</b><br>#" + String(vercelPostCount) + " (HTTP " + String(lastVercelHttpCode) + ")</div>";
    html += "</div></div>";

    html += "<div class='card'><h3>Última Leitura Recebida do ESP 1 (via LoRa)</h3>";
    if (estDadosRecebidos) {
      html += "<div class='grid'>";
      html += "<div><b>Temp:</b> " + String(estTemp, 1) + " °C</div>";
      html += "<div><b>Umidade:</b> " + String(estUmid, 1) + " %</div>";
      html += "<div><b>Luz (LDR):</b> " + String(estLuz) + " %</div>";
      html += "<div><b>Persiana:</b> " + String(estServo) + "° (" + estModo + ")</div>";
      html += "<div><b>Relés R1..R4:</b> " + String(estReles[0]) + " / " + String(estReles[1]) + " / " + String(estReles[2]) + " / " + String(estReles[3]) + "</div>";
      html += "<div><b>Pacote #seq:</b> " + String(estSeq) + "</div>";
      html += "</div>";
    } else {
      html += "<p>Aguardando primeiro pacote LoRa 915 MHz do ESP 1...</p>";
    }
    html += "<form action='/ping' method='POST'><button class='btn-sec' type='submit'>Solicitar Leitura Imediata do ESP 1 (LoRa PING)</button></form>";
    html += "</div>";

    html += "<div class='card'><h3>Testar Envio de Ordem via LoRa para o ESP 1</h3><div class='grid'>";
    html += "<form action='/cmd?m=AUTO' method='POST'><button class='btn' type='submit'>Modo AUTOMÁTICO</button></form>";
    html += "<form action='/cmd?m=MAN' method='POST'><button class='btn' type='submit'>Modo MANUAL</button></form>";
    html += "<form action='/cmd?srv=0' method='POST'><button class='btn-sec' type='submit'>Persiana 0°</button></form>";
    html += "<form action='/cmd?srv=90' method='POST'><button class='btn-sec' type='submit'>Persiana 90°</button></form>";
    html += "<form action='/cmd?ch=0&st=1' method='POST'><button class='btn-sec' type='submit'>Ligar R1 Ventoinha</button></form>";
    html += "<form action='/cmd?ch=0&st=0' method='POST'><button class='btn-sec' type='submit'>Desligar R1 Ventoinha</button></form>";
    html += "</div></div>";

    html += "<div class='card'><h3>Configurar Wi-Fi, MQTT e URL da Vercel</h3>";
    html += "<form action='/savewifi' method='POST'>";
    html += "<label>Nome da Rede Wi-Fi (SSID 2.4 GHz):</label>";
    html += "<input type='text' name='ssid' value='" + wifiSsid + "' required>";
    html += "<label>Senha do Wi-Fi:</label>";
    html += "<input type='password' name='pass' value='" + wifiPass + "'>";
    html += "<label>Broker MQTT (Host):</label>";
    html += "<input type='text' name='mqtt' value='" + mqttHost + "' required>";
    html += "<label>URL da API na Vercel (/api/historico):</label>";
    html += "<input type='text' name='vercel' value='" + vercelApiUrl + "'>";
    html += "<button class='btn' type='submit'>Salvar e Conectar Wi-Fi / MQTT / Vercel</button>";
    html += "</form></div></body></html>";
    server.send(200, "text/html", html);
  });

  server.on("/ping", HTTP_POST, []() {
    enfileirarComandoLoRa("{\"cmd\":\"PING\"}");
    server.sendHeader("Location", "/");
    server.send(303);
  });

  server.on("/cmd", HTTP_POST, []() {
    JsonDocument doc;
    doc["cmd"] = "SET";
    if (server.hasArg("m")) doc["m"] = server.arg("m");
    if (server.hasArg("srv")) {
      doc["m"] = "MAN";
      doc["srv"] = server.arg("srv").toInt();
    }
    if (server.hasArg("ch")) {
      doc["m"] = "MAN";
      doc["ch"] = server.arg("ch").toInt();
      doc["state"] = server.arg("st").toInt();
    }
    String out;
    serializeJson(doc, out);
    enfileirarComandoLoRa(out);
    server.sendHeader("Location", "/");
    server.send(303);
  });

  server.on("/savewifi", HTTP_POST, []() {
    if (server.hasArg("ssid")) {
      wifiSsid = server.arg("ssid");
      wifiSsid.trim();
      wifiPass = server.arg("pass");
      mqttHost = server.arg("mqtt");
      mqttHost.trim();
      if (mqttHost.length() == 0) mqttHost = DEFAULT_MQTT_HOST;
      if (server.hasArg("vercel")) {
        vercelApiUrl = server.arg("vercel");
        vercelApiUrl.trim();
      }

      prefs.begin("gw_cfg", false);
      prefs.putString("ssid", wifiSsid);
      prefs.putString("pass", wifiPass);
      prefs.putString("mqtt", mqttHost);
      prefs.putString("vercel", vercelApiUrl);
      prefs.end();

      WiFi.disconnect();
      conectarWiFi();
    }
    server.sendHeader("Location", "/");
    server.send(303);
  });

  server.begin();
}

// ================= 9. SETUP E LOOP PRINCIPAL =================
void setup() {
  Serial.begin(115200);
  delay(300);

  pinMode(LED_ONBOARD, OUTPUT);
  digitalWrite(LED_ONBOARD, LOW);
  pinMode(BTN_PRG, INPUT_PULLUP);

  uint64_t mac = ESP.getEfuseMac();
  char buf[6];
  snprintf(buf, sizeof(buf), "%04X", (uint16_t)(mac >> 32));
  gwNodeId = String(buf);

  // Carrega credenciais salvas na memória flash (NVS) ou usa os valores padrão
  prefs.begin("gw_cfg", true);
  wifiSsid = prefs.getString("ssid", DEFAULT_WIFI_SSID);
  wifiPass = prefs.getString("pass", DEFAULT_WIFI_PASS);
  mqttHost = prefs.getString("mqtt", DEFAULT_MQTT_HOST);
  mqttPort = prefs.getUShort("port", DEFAULT_MQTT_PORT);
  vercelApiUrl = prefs.getString("vercel", DEFAULT_VERCEL_URL);
  prefs.end();

  if (String(DEFAULT_WIFI_SSID) != "SEU_WIFI_AQUI" && wifiSsid == "SEU_WIFI_AQUI") {
    wifiSsid = DEFAULT_WIFI_SSID;
    wifiPass = DEFAULT_WIFI_PASS;
  }
  if (vercelApiUrl == "https://projeto-iot.vercel.app/api/historico" || vercelApiUrl.length() < 10) {
    vercelApiUrl = DEFAULT_VERCEL_URL;
  }

  // Inicializa Display OLED SSD1306
  pinMode(VEXT_CTRL, OUTPUT);
  digitalWrite(VEXT_CTRL, LOW);
  delay(40);
  Wire.begin(OLED_SDA, OLED_SCL);
  oledOk = display.begin(SSD1306_SWITCHCAPVCC, 0x3C);
  atualizarDisplayOLED();

  // Inicializa Rádio LoRa SX1262 em 915 MHz (mesmos parâmetros do ESP 1)
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
    radio.setDio1Action(onLoRaReceive);
    radio.startReceive();
    Serial.println("[LoRa SX1262] Gateway 915 MHz pronto! Escutando pacotes do ESP 1...");
  } else {
    Serial.printf("[ERRO LoRa] Codigo RadioLib: %d\n", loraState);
  }

  // Inicia Wi-Fi em modo AP + Station simultâneo (mesmo canal para evitar conflito de rádio)
  WiFi.mode(WIFI_AP_STA);
  conectarWiFi();
  configurarRotasWebGateway();
  atualizarDisplayOLED();
}

void loop() {
  server.handleClient();

  // 1. Escuta pacotes LoRa vindos do ESP 1 e publica no MQTT
  verificarRecepcaoLoRa();

  // 2. Se chegou algum comando do Site (via MQTT, Vercel ou Portal Local), transmite via LoRa ao ESP 1
  if (pendingLoRaCmd) {
    pendingLoRaCmd = false;
    String cmdToSend = pendingLoRaCmdPayload;
    pendingLoRaCmdPayload = "";
    transmitirComandoLoRa(cmdToSend);
  }

  // 3. Mantém conexão Wi-Fi, MQTT, Vercel HTTPS e ArduinoOTA
  if (WiFi.status() == WL_CONNECTED) {
    iniciarOTA();
    if (otaStarted) ArduinoOTA.handle();
    gerenciarConexaoMqtt();

    if (pendingVercelPost) {
      pendingVercelPost = false;
      lastVercelSync = millis();
      enviarParaVercelHTTPS();
    }
  } else if (millis() - lastWifiCheck >= 30000) {
    lastWifiCheck = millis();
    wl_status_t st = WiFi.status();
    if ((st == WL_NO_SSID_AVAIL || st == WL_CONNECT_FAILED || st == WL_DISCONNECTED) &&
        wifiSsid.length() > 0 && wifiSsid != "SEU_WIFI_AQUI") {
      Serial.printf("[Wi-Fi] Tentando conectar a '%s' (ou acesse http://192.168.4.1 no AP '%s')...\n",
                    wifiSsid.c_str(), GW_AP_SSID);
      WiFi.disconnect();
      delay(50);
      if (wifiPass.length() > 0) {
        WiFi.begin(wifiSsid.c_str(), wifiPass.c_str());
      } else {
        WiFi.begin(wifiSsid.c_str());
      }
    }
  }

  // 4. Atualiza OLED periodicamente
  if (millis() - lastOledUpdate >= 1000) {
    lastOledUpdate = millis();
    atualizarDisplayOLED();
  }

  // 5. Botão PRG na placa do ESP 2 envia um PING via LoRa para pedir dados imediatos ao ESP 1
  if (digitalRead(BTN_PRG) == LOW) {
    delay(50);
    if (digitalRead(BTN_PRG) == LOW) {
      transmitirComandoLoRa("{\"cmd\":\"PING\"}");
      while (digitalRead(BTN_PRG) == LOW) {
        server.handleClient();
        delay(10);
      }
    }
  }
}
