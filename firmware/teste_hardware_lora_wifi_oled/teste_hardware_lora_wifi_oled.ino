/*
 * ======================================================================================
 * TESTE COMPLETO PARA 1 PLACA (OU 2) — HELTEC WIFI LORA 32 V3 (ESP32-S3 + SX1262 + OLED)
 * ======================================================================================
 * Hardware detectado na porta COM14:
 *   - Placa: Heltec WiFi LoRa 32 (V3)
 *   - MCU: ESP32-S3 (QFN56, 8MB Flash)
 *   - Rádio LoRa: Semtech SX1262 (915 MHz)
 *   - Display OLED: SSD1306 128x64 I2C (SDA=17, SCL=18, RST=21, Vext=36)
 *   - LED Onboard (Branco): GPIO 35 | Botão PRG: GPIO 0
 *
 * O que este código testa (mesmo tendo apenas 1 ESP32 LoRa em mãos!):
 *   1. Display OLED: Mostra status do chip LoRa SX1262, redes Wi-Fi, IP, TX/RX e ruído RF.
 *   2. Rádio LoRa SX1262 (915 MHz):
 *      - Testa a comunicação SPI com o chip SX1262 e valida o disparo TX real (medindo ms).
 *      - Mede o RSSI do ambiente em 915 MHz (ruído de fundo / portadora) em tempo real.
 *      - Escuta pacotes RX (caso haja outra placa LoRa 915 MHz transmitindo na sala).
 *   3. Wi-Fi + Mini Site de Controle Local (para testar pelo celular/PC agora mesmo!):
 *      - Escaneia as redes 2.4 GHz do laboratório.
 *      - Cria uma rede Wi-Fi própria na placa:
 *          SSID:  Estufa_LoRa_V3
 *          Senha: 12345678
 *      - Ao conectar nessa rede pelo celular/PC e abrir http://192.168.4.1 no navegador,
 *        você controla o LED da placa, envia mensagens para o Display OLED e dispara
 *        pacotes LoRa 915 MHz pelo site!
 *
 * Bibliotecas para instalar na Arduino IDE (Gerenciador de Bibliotecas):
 *   1. "RadioLib" (por Jan Gromes) -> Necessária para o chip LoRa SX1262 da Heltec V3
 *   2. "Adafruit SSD1306" e "Adafruit GFX Library" -> Para o Display OLED
 *
 * Placa para selecionar em Ferramentas -> Placa:
 *   - "Heltec WiFi LoRa 32(V3)" ou "ESP32S3 Dev Module"
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

// ================= 1. PINAGEM OFICIAL HELTEC WIFI LORA 32 V3 (ESP32-S3) =================
// Display OLED SSD1306
#define VEXT_CTRL     36   // LOW = Liga alimentação 3.3V do OLED
#define OLED_SDA      17
#define OLED_SCL      18
#define OLED_RST      21
#define SCREEN_WIDTH  128
#define SCREEN_HEIGHT 64

// Rádio LoRa Semtech SX1262 (Barramento SPI dedicado)
#define LORA_NSS      8
#define LORA_SCK      9
#define LORA_MOSI     10
#define LORA_MISO     11
#define LORA_RST      12
#define LORA_BUSY     13
#define LORA_DIO1     14
#define LORA_FREQ_MHZ 915.0

// Periféricos da Placa
#define LED_ONBOARD   35   // LED Branco da Heltec V3
#define BTN_PRG       0    // Botão PRG / USER

// ================= 2. CONFIGURAÇÃO DO WI-FI (AP + SCANNER) =================
const char* AP_SSID = "Estufa_LoRa_V3";
const char* AP_PASS = "12345678";

// ================= 3. OBJETOS GLOBAIS =================
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RST);
SPIClass loraSpi(FSPI);
SX1262 radio = new Module(LORA_NSS, LORA_DIO1, LORA_RST, LORA_BUSY, loraSpi);
WebServer server(80);

// Estado do sistema
String nodeId = "";
bool oledOk = false;
bool loraOk = false;
bool ledState = false;
int wifiScanCount = 0;
int strongestWifiRssi = 0;

unsigned long txCount = 0;
unsigned long rxCount = 0;
unsigned long lastTxDurationMs = 0;
float ambientRssiDbm = 0.0;
float lastRxRssi = 0.0;
float lastRxSnr = 0.0;
String lastMessage = "Pronto p/ teste!";

unsigned long lastAutoTx = 0;
const unsigned long AUTO_TX_INTERVAL = 4000; // Dispara um pacote LoRa a cada 4s

// Flag de interrupção quando o SX1262 recebe um pacote
volatile bool rxFlag = false;
#if defined(ESP8266) || defined(ESP32)
  ICACHE_RAM_ATTR
#endif
void onLoRaReceive() {
  rxFlag = true;
}

// ================= 4. DISPLAY OLED =================
void inicializarOLED() {
  // Liga a alimentação Vext (pino 36 em LOW)
  pinMode(VEXT_CTRL, OUTPUT);
  digitalWrite(VEXT_CTRL, LOW);
  delay(50);

  Wire.begin(OLED_SDA, OLED_SCL);
  if (display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    oledOk = true;
    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);
    display.setCursor(0, 0);
    display.println("Heltec LoRa 32 V3");
    display.println("Iniciando Hardware...");
    display.display();
    Serial.println("[OLED] Display SSD1306 inicializado (SDA=17, SCL=18, RST=21).");
  } else {
    Serial.println("[ERRO OLED] Falha ao iniciar SSD1306!");
  }
}

void atualizarOLED() {
  if (!oledOk) return;

  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);

  // Linha 1: Cabeçalho + Status do Chip SX1262
  display.setCursor(0, 0);
  display.print("V3:");
  display.print(nodeId);
  display.print(" SX1262:");
  display.println(loraOk ? "OK" : "ERR");
  display.drawLine(0, 9, 127, 9, SSD1306_WHITE);

  // Linha 2: Wi-Fi Scan + IP do Painel Web
  display.setCursor(0, 12);
  display.print("WiFi:");
  display.print(wifiScanCount);
  display.print(" | ");
  display.println(WiFi.softAPIP());

  // Linha 3: Contadores TX / RX e Tempo de Ar
  display.setCursor(0, 23);
  display.print("TX:");
  display.print(txCount);
  display.print(" (");
  display.print(lastTxDurationMs);
  display.print("ms) RX:");
  display.println(rxCount);

  // Linha 4: Ruído RF em 915 MHz ou RSSI do último pacote
  display.setCursor(0, 34);
  if (rxCount > 0) {
    display.print("RX RSSI:");
    display.print(lastRxRssi, 0);
    display.print(" SNR:");
    display.println(lastRxSnr, 1);
  } else {
    display.print("RF 915MHz: ");
    display.print(ambientRssiDbm, 1);
    display.println("dBm");
  }

  // Linha 5: Última mensagem ou comando recebido pelo site/LoRa
  display.setCursor(0, 45);
  display.print("Msg: ");
  display.println(lastMessage.substring(0, 15));

  // Linha 6: Rodapé com dica do Wi-Fi AP
  display.setCursor(0, 56);
  display.print("AP: Estufa_LoRa_V3");

  display.display();
}

// ================= 5. RÁDIO LORA SX1262 (915 MHz) =================
void inicializarLoRaSX1262() {
  Serial.println("\n--- TESTANDO CHIP LORA SX1262 (915 MHz) ---");
  loraSpi.begin(LORA_SCK, LORA_MISO, LORA_MOSI, LORA_NSS);

  // Inicializa SX1262: 915.0 MHz, BW=125 kHz, SF=7, CR=5, SyncWord=0x12, Power=14 dBm
  int state = radio.begin(LORA_FREQ_MHZ, 125.0, 7, 5, RADIOLIB_SX126X_SYNC_WORD_PRIVATE, 14, 8);

  if (state == RADIOLIB_ERR_NONE) {
    loraOk = true;
    Serial.println("[LoRa SX1262] Sucesso! Transceptor 915 MHz respondeu via SPI.");

    // Configura interrupção de recepção no pino DIO1 (GPIO 14)
    radio.setDio1Action(onLoRaReceive);
    radio.startReceive();
    ambientRssiDbm = radio.getRSSI(false);
  } else {
    loraOk = false;
    Serial.printf("[ERRO LoRa] Falha ao iniciar SX1262 (Codigo RadioLib: %d)\n", state);
  }
}

void transmitirPacoteLoRa(const String& textoCustomizado) {
  if (!loraOk) return;

  txCount++;
  String payload = textoCustomizado.length() > 0
                   ? textoCustomizado
                   : ("ESTUFA#" + String(txCount) + ":" + nodeId);

  digitalWrite(LED_ONBOARD, HIGH);
  unsigned long t0 = millis();

  // Transmite o pacote em 915 MHz (bloqueante por ~40-60ms)
  int state = radio.transmit(payload.c_str());
  lastTxDurationMs = millis() - t0;

  if (!ledState) {
    digitalWrite(LED_ONBOARD, LOW);
  }

  if (state == RADIOLIB_ERR_NONE) {
    lastMessage = payload;
    Serial.printf("[LoRa TX #%lu] Enviado '%s' em %lu ms (915 MHz)\n",
                  txCount, payload.c_str(), lastTxDurationMs);
  } else {
    Serial.printf("[LoRa TX ERRO] Codigo: %d\n", state);
  }

  // Volta imediatamente para o modo de escuta (RX contínuo) e lê o RSSI instantâneo
  radio.startReceive();
  ambientRssiDbm = radio.getRSSI(false);
  atualizarOLED();
}

void verificarRecepcaoLoRa() {
  if (!loraOk || !rxFlag) return;
  rxFlag = false;

  String str;
  int state = radio.readData(str);
  if (state == RADIOLIB_ERR_NONE && str.length() > 0) {
    rxCount++;
    lastMessage = str;
    lastRxRssi = radio.getRSSI();
    lastRxSnr = radio.getSNR();

    Serial.printf("[LoRa RX #%lu] Recebido: '%s' | RSSI: %.1f dBm | SNR: %.1f dB\n",
                  rxCount, str.c_str(), lastRxRssi, lastRxSnr);
    atualizarOLED();
  }
  radio.startReceive();
}

// ================= 6. WI-FI + PÁGINA WEB DE TESTE (PARA 1 PLACA) =================
void enviarPaginaWeb() {
  String html = "<!DOCTYPE html><html lang='pt-BR'><head><meta charset='UTF-8'>";
  html += "<meta name='viewport' content='width=device-width,initial-scale=1'>";
  html += "<title>Teste Heltec LoRa V3</title>";
  html += "<style>body{font-family:sans-serif;background:#0f172a;color:#f8fafc;padding:18px;max-width:480px;margin:auto;}";
  html += ".card{background:#1e293b;padding:16px;border-radius:10px;margin-bottom:14px;}";
  html += "button,input[type=submit]{background:#22c55e;color:#000;border:none;padding:10px 16px;border-radius:6px;font-weight:bold;cursor:pointer;width:100%;margin-top:8px;}";
  html += "input[type=text]{width:100%;padding:8px;box-sizing:border-box;border-radius:6px;border:1px solid #475569;margin-top:6px;}";
  html += "</style></head><body>";
  html += "<h2>Heltec WiFi LoRa 32 V3</h2>";
  html += "<div class='card'><p><b>Node ID:</b> " + nodeId + "</p>";
  html += "<p><b>Radio SX1262 (915 MHz):</b> " + String(loraOk ? "OK (Operacional)" : "ERRO") + "</p>";
  html += "<p><b>Pacotes LoRa TX:</b> " + String(txCount) + " (ultimo levou " + String(lastTxDurationMs) + " ms)</p>";
  html += "<p><b>Pacotes LoRa RX:</b> " + String(rxCount) + "</p>";
  html += "<p><b>Ruido RF 915 MHz:</b> " + String(ambientRssiDbm, 1) + " dBm</p>";
  html += "<p><b>Redes Wi-Fi Escaneadas:</b> " + String(wifiScanCount) + " (Mais forte: " + String(strongestWifiRssi) + " dBm)</p>";
  html += "<p><b>Ultima Msg no OLED:</b> " + lastMessage + "</p></div>";

  html += "<div class='card'><h3>Testar Atuadores e LoRa</h3>";
  html += "<form action='/led' method='POST'><button type='submit'>Alternar LED da Placa (Atual: " + String(ledState ? "ACESO" : "APAGADO") + ")</button></form>";
  html += "<form action='/tx' method='POST' style='margin-top:12px;'>";
  html += "<label>Enviar mensagem p/ OLED + Disparar LoRa 915MHz:</label>";
  html += "<input type='text' name='msg' placeholder='Ex: Persiana 90 graus' required>";
  html += "<input type='submit' value='Transmitir via LoRa e Mostrar no OLED'></form></div>";
  html += "</body></html>";

  server.send(200, "text/html", html);
}

void inicializarWiFiEServidor() {
  Serial.println("\n--- TESTANDO WI-FI (SCANNER + ACCESS POINT) ---");
  WiFi.mode(WIFI_AP_STA);

  wifiScanCount = WiFi.scanNetworks();
  if (wifiScanCount > 0) {
    strongestWifiRssi = WiFi.RSSI(0);
  }
  Serial.printf("[Wi-Fi Scan] %d redes encontradas (Mais forte: %d dBm)\n",
                wifiScanCount, strongestWifiRssi);

  WiFi.softAP(AP_SSID, AP_PASS);
  Serial.printf("[Wi-Fi AP] Rede criada! Conecte-se no Wi-Fi '%s' (Senha: %s)\n", AP_SSID, AP_PASS);
  Serial.printf("[Web Server] Acesse no navegador: http://%s\n", WiFi.softAPIP().toString().c_str());

  server.on("/", HTTP_GET, enviarPaginaWeb);

  server.on("/led", HTTP_POST, []() {
    ledState = !ledState;
    digitalWrite(LED_ONBOARD, ledState ? HIGH : LOW);
    lastMessage = ledState ? "LED: ACESO" : "LED: APAGADO";
    atualizarOLED();
    server.sendHeader("Location", "/");
    server.send(303);
  });

  server.on("/tx", HTTP_POST, []() {
    String msg = server.arg("msg");
    transmitirPacoteLoRa(msg);
    server.sendHeader("Location", "/");
    server.send(303);
  });

  server.begin();
}

// ================= 7. SETUP E LOOP =================
void setup() {
  Serial.begin(115200);
  delay(400);

  pinMode(LED_ONBOARD, OUTPUT);
  digitalWrite(LED_ONBOARD, LOW);
  pinMode(BTN_PRG, INPUT_PULLUP);

  uint64_t mac = ESP.getEfuseMac();
  char buf[6];
  snprintf(buf, sizeof(buf), "%04X", (uint16_t)(mac >> 32));
  nodeId = String(buf);

  Serial.println("\n=======================================================");
  Serial.printf(" DIAGNOSTICO HELTEC WIFI LORA 32 V3 (ID: %s)\n", nodeId.c_str());
  Serial.println("=======================================================");

  inicializarOLED();
  inicializarWiFiEServidor();
  inicializarLoRaSX1262();
  atualizarOLED();
}

void loop() {
  server.handleClient();
  verificarRecepcaoLoRa();

  // Disparo periódico de teste LoRa a cada 4 segundos
  if (millis() - lastAutoTx >= AUTO_TX_INTERVAL) {
    lastAutoTx = millis();
    transmitirPacoteLoRa("");
  }

  // Botão PRG na placa (GPIO 0): alterna o LED e envia pacote LoRa manual
  if (digitalRead(BTN_PRG) == LOW) {
    delay(50);
    if (digitalRead(BTN_PRG) == LOW) {
      ledState = !ledState;
      digitalWrite(LED_ONBOARD, ledState ? HIGH : LOW);
      transmitirPacoteLoRa("BTN_PRG:" + nodeId);
      while (digitalRead(BTN_PRG) == LOW) {
        server.handleClient();
        delay(10);
      }
    }
  }
}