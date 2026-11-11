/*
 * ======================================================================================
 * PROJETO FINAL IoT (UNIFESP) — NÓ ESTUFA REMOTA (Heltec WiFi LoRa 32 V3 + Ponte H L298N)
 * ======================================================================================
 * Hardware configurado:
 *   - 1x Servomotor da Persiana no GPIO 6 (PWM 50 Hz)
 *   - 1x Sensor LDR no GPIO 4 (ADC1 12 bits: 0 a 4095 -> 0 a 100%)
 *   - 1x Sensor DHT11 no GPIO 5 (Temperatura °C e Umidade %)
 *   - Módulo Ponte H L298N (ZX-040) — Ativo em Nível ALTO (HIGH = +VCC no borne OUTx):
 *       * IN1 (GPIO 47) -> Controla OUT1: Ventoinha 5V (Exaustor)
 *       * IN2 (GPIO 48) -> Controla OUT2: LED de Cultivo (ou GND da Ventoinha se em OUT2)
 *       * IN3 (GPIO 45) -> Controla OUT3: Irrigação (simulado com LED Azul)
 *       * IN4 (GPIO 46) -> Controla OUT4: Aquecedor (simulado com LED Vermelho)
 *   - Sinalização Local: Buzzer (GPIO 42), LED Verde (GPIO 41), LED Vermelho (GPIO 40)
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
// --- Internos da Placa (Display OLED + LoRa SX1262) ---
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

// --- Pinos Externos para Protoboard / PCI da Estufa ---
#define PIN_LDR         4    // Entrada Analógica ADC1 (Pino '4')
#define PIN_DHT         5    // Entrada Digital DHT11 (Pino '5')
#define DHT_TYPE        DHT11

#define PIN_SERVO       6    // PWM do Único Servomotor da Persiana (Pino '6')

// --- Pinos conectados ao Módulo Ponte H L298N (ZX-040) ---
#define PIN_L298_IN1    47   // IN1 -> Borne OUT1: Ventoinha 5V (Pino '47')
#define PIN_L298_IN2    48   // IN2 -> Borne OUT2: LED Cultivo / ou Retorno Motor A (Pino '48')
#define PIN_L298_IN3    45   // IN3 -> Borne OUT3: LED Irrigação (Pino '45')
#define PIN_L298_IN4    46   // IN4 -> Borne OUT4: LED Aquecedor (Pino '46')

// Na Ponte H L298N: INx = HIGH coloca +VCC no borne OUTx; INx = LOW coloca GND (0V) no borne OUTx
#define DRIVER_ON       HIGH
#define DRIVER_OFF      LOW

#define PIN_BUZZER      42   // Saída Digital Buzzer (Pino '42')
#define PIN_LED_VERDE   41   // LED Verde Status Normal (Pino '41')
#define PIN_LED_VERM    40   // LED Vermelho Alarme (Pino '40')

// ================= 2. OBJETOS E VARIÁVEIS DE ESTADO =================
Adafruit_SSD1306 display(128, 64, &Wire, OLED_RST);
SPIClass loraSpi(FSPI);
SX1262 radio = new Module(LORA_NSS, LORA_DIO1, LORA_RST, LORA_BUSY, loraSpi);
DHT dht(PIN_DHT, DHT_TYPE);
Servo servoPersiana;
WebServer server(80);

// Leituras dos sensores
float tempAtual = 25.0;
float umidAtual = 60.0;
bool dhtConectado = false;
int adcLdrBruto = 0;
int luzPercent = 50;         // 0% (escuro) a 100% (muita luz)

// Se ao iluminar o LDR com a lanterna a porcentagem diminuir em vez de subir, mude para true
bool INVERTER_LDR = false;

// Estado dos atuadores
bool modoAutomatico = true;       // true = AUTO, false = MANUAL
bool servoProporcional = false;   // false = 3 estágios (0°/90°/160°), true = contínuo (0°..180°)
int anguloPersiana = 90;          // 0° a 180°
bool saidaEstado[4] = {false, false, false, false}; // [OUT1_Ventoinha, OUT2_LED_Grow, OUT3_Irrig, OUT4_Aquec]
bool alarmeAtivo = false;
bool buzzerMute = false;

// Limiares (Setpoints) do Modo Automático
float cfgTempMax = 28.0;     // Acima disso -> Liga OUT1 (Ventoinha 5V)
float cfgTempCritica = 35.0; // Acima disso -> Dispara Alarme + Buzzer
float cfgTempMin = 18.0;     // Abaixo disso -> Liga OUT4 (Aquecedor / LED simulador)
float cfgUmidMin = 45.0;     // Abaixo disso -> Liga OUT3 (Irrigação / LED simulador)
int cfgLuzBaixa = 25;        // Abaixo de 25% (tampando o LDR) -> Persiana 0° + Liga OUT2 (LED)
int cfgLuzAlta = 75;         // Acima de 75% (lanterna no LDR) -> Persiana 160° (Sombreamento)

// Contadores e Temporizadores
uint32_t seqLoRa = 0;
bool loraOk = false;
bool oledOk = false;
unsigned long lastSensorRead = 0;
unsigned long lastLoRaTx = 0;
const unsigned long SENSOR_INTERVAL_MS = 600;   // Lê sensores a cada 600ms p/ resposta rápida no servo!
const unsigned long LORA_TX_INTERVAL_MS = 5000; // Envia pacote LoRa a cada 5s

volatile bool loraRxFlag = false;
#if defined(ESP8266) || defined(ESP32)
  ICACHE_RAM_ATTR
#endif
void onLoRaPacket() {
  loraRxFlag = true;
}

// ================= 3. ATUAÇÃO FÍSICA (SERVO, L298N, LEDS, BUZZER) =================
void aplicarAnguloPersiana(int novoAngulo) {
  novoAngulo = constrain(novoAngulo, 0, 180);
  if (abs(novoAngulo - anguloPersiana) >= 3) {
    if (!buzzerMute && !servoProporcional) {
      digitalWrite(PIN_BUZZER, HIGH);
      delay(25);
      digitalWrite(PIN_BUZZER, LOW);
    }
    anguloPersiana = novoAngulo;
    servoPersiana.write(anguloPersiana);
    Serial.printf("[SERVO] Persiana movida para %d graus (Luz: %d%%)\n", anguloPersiana, luzPercent);
  }
}

void atualizarSaidasFisicas() {
  const int pinosL298[4] = {PIN_L298_IN1, PIN_L298_IN2, PIN_L298_IN3, PIN_L298_IN4};
  for (int i = 0; i < 4; i++) {
    digitalWrite(pinosL298[i], saidaEstado[i] ? DRIVER_ON : DRIVER_OFF);
  }

  if (alarmeAtivo) {
    digitalWrite(PIN_LED_VERM, HIGH);
    digitalWrite(PIN_LED_VERDE, LOW);
    digitalWrite(PIN_BUZZER, (!buzzerMute) ? HIGH : LOW);
  } else {
    digitalWrite(PIN_LED_VERM, LOW);
    digitalWrite(PIN_LED_VERDE, HIGH);
    digitalWrite(PIN_BUZZER, LOW);
  }
}

// ================= 4. LEITURA DOS SENSORES E LÓGICA AUTOMÁTICA =================
unsigned long lastDhtRead = 0;

void lerSensores() {
  // 1. Leitura rápida do LDR (média de 8 amostras no ADC1 GPIO 4)
  long somaAdc = 0;
  for (int i = 0; i < 8; i++) {
    somaAdc += analogRead(PIN_LDR);
  }
  adcLdrBruto = somaAdc / 8;

  int pct = map(adcLdrBruto, 0, 4095, 0, 100);
  pct = constrain(pct, 0, 100);
  luzPercent = INVERTER_LDR ? (100 - pct) : pct;

  // 2. Leitura do DHT11 a cada 2 segundos
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
  if (!modoAutomatico) return;

  // --- 1. Controle Automático do Servomotor da Persiana + OUT2 (LED Iluminação) ---
  if (servoProporcional) {
    int angAlvo = map(luzPercent, 0, 100, 0, 180);
    aplicarAnguloPersiana(angAlvo);
    saidaEstado[1] = (luzPercent < cfgLuzBaixa);
  } else {
    if (luzPercent < cfgLuzBaixa) {
      // Escuro (mão tampando o LDR): Fecha a persiana (0°) e liga OUT2 (LED Cultivo)
      aplicarAnguloPersiana(0);
      saidaEstado[1] = true;
    } else if (luzPercent > cfgLuzAlta) {
      // Luz muito forte (lanterna no LDR): Inclina persiana para sombreamento (160°)
      aplicarAnguloPersiana(160);
      saidaEstado[1] = false;
    } else {
      // Luz ambiente normal (entre 25% e 75%): Abre a persiana em 90°
      aplicarAnguloPersiana(90);
      saidaEstado[1] = false;
    }
  }

  // --- 2. Controle do OUT1 (Ventoinha 5V no L298N) ---
  saidaEstado[0] = (tempAtual > cfgTempMax || umidAtual > 80.0);

  // --- 3. Controle do OUT3 (Irrigação - LED Azul) ---
  saidaEstado[2] = (umidAtual < cfgUmidMin);

  // --- 4. Controle do OUT4 (Aquecedor - LED Vermelho) ---
  saidaEstado[3] = (tempAtual < cfgTempMin);

  // --- 5. Alarme Crítico ---
  alarmeAtivo = (tempAtual >= cfgTempCritica);
  if (alarmeAtivo) {
    saidaEstado[0] = true; // Força ventoinha ligada em alerta crítico
  }

  atualizarSaidasFisicas();
}

// ================= 5. DISPLAY OLED =================
void atualizarDisplayOLED() {
  if (!oledOk) return;

  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);

  // Linha 1: Modo + Status DHT11 + Sequência LoRa
  display.setCursor(0, 0);
  display.print(modoAutomatico ? "[AUTO]" : "[MAN] ");
  display.print(dhtConectado ? " DHT:OK" : " DHT:--");
  display.print(" LR:#");
  display.println(seqLoRa);
  display.drawLine(0, 9, 127, 9, SSD1306_WHITE);

  // Linha 2: Temperatura e Umidade
  display.setCursor(0, 12);
  display.print("Temp: ");
  display.print(tempAtual, 1);
  display.print("C Umid:");
  display.print(umidAtual, 0);
  display.println("%");

  // Linha 3: Luminosidade LDR
  display.setCursor(0, 23);
  display.print("Luz: ");
  display.print(luzPercent);
  display.print("% (ADC:");
  display.print(adcLdrBruto);
  display.println(")");

  // Linha 4: Posição do Servo da Persiana
  display.setCursor(0, 34);
  display.print("Persiana: ");
  display.print(anguloPersiana);
  display.println(anguloPersiana == 0 ? " gr (Fech)" : (anguloPersiana > 120 ? " gr (Somb)" : " gr (Aber)"));

  // Linha 5: Estado das 4 Saídas do L298N (OUT1..OUT4)
  display.setCursor(0, 45);
  display.printf("V:%d LED:%d Irr:%d Aq:%d",
                 saidaEstado[0], saidaEstado[1], saidaEstado[2], saidaEstado[3]);

  // Linha 6: IP da Bancada ou Alerta
  display.setCursor(0, 55);
  if (alarmeAtivo) {
    display.print("!! ALARME CRITICO !!");
  } else {
    display.print("Web: 192.168.4.1");
  }

  display.display();
}

// ================= 6. COMUNICAÇÃO LORA 915 MHz =================
String montarJsonTelemetria() {
  JsonDocument doc;
  doc["id"] = "EST01";
  doc["seq"] = seqLoRa;
  doc["t"] = serialized(String(tempAtual, 1));
  doc["h"] = serialized(String(umidAtual, 1));
  doc["l"] = luzPercent;
  doc["srv"] = anguloPersiana;
  JsonArray r = doc["r"].to<JsonArray>();
  for (int i = 0; i < 4; i++) {
    r.add(saidaEstado[i] ? 1 : 0);
  }
  doc["al"] = alarmeAtivo ? 1 : 0;
  doc["m"] = modoAutomatico ? "AUTO" : "MAN";

  String output;
  serializeJson(doc, output);
  return output;
}

void enviarTelemetriaLoRa() {
  if (!loraOk) return;

  seqLoRa++;
  String payload = montarJsonTelemetria();

  digitalWrite(LED_ONBOARD, HIGH);
  int state = radio.transmit(payload.c_str());
  digitalWrite(LED_ONBOARD, LOW);

  if (state == RADIOLIB_ERR_NONE) {
    Serial.printf("[LoRa TX #%lu] %s\n", seqLoRa, payload.c_str());
  } else {
    Serial.printf("[LoRa ERRO TX] Codigo: %d\n", state);
  }

  radio.startReceive();
}

void processarComandoJson(const String& jsonStr) {
  JsonDocument doc;
  if (deserializeJson(doc, jsonStr)) return;

  const char* cmd = doc["cmd"] | "SET";
  if (strcmp(cmd, "SET") == 0) {
    if (doc["m"].is<const char*>()) {
      modoAutomatico = (String(doc["m"].as<const char*>()) == "AUTO");
    }
    if (!modoAutomatico) {
      if (doc["srv"].is<int>()) {
        aplicarAnguloPersiana(doc["srv"].as<int>());
      }
      if (doc["r"].is<JsonArray>()) {
        JsonArray arr = doc["r"].as<JsonArray>();
        for (int i = 0; i < 4 && i < arr.size(); i++) {
          saidaEstado[i] = (arr[i].as<int>() == 1);
        }
      }
    }
    if (doc["buz"].is<int>()) {
      buzzerMute = (doc["buz"].as<int>() == 0);
    }
    atualizarSaidasFisicas();
  } else if (strcmp(cmd, "CFG") == 0) {
    if (doc["tMax"].is<float>()) cfgTempMax = doc["tMax"].as<float>();
    if (doc["tMin"].is<float>()) cfgTempMin = doc["tMin"].as<float>();
    if (doc["hMin"].is<float>()) cfgUmidMin = doc["hMin"].as<float>();
    if (doc["lMin"].is<int>())   cfgLuzBaixa = doc["lMin"].as<int>();
  }

  atualizarDisplayOLED();
}

void verificarComandosLoRa() {
  if (!loraOk || !loraRxFlag) return;
  loraRxFlag = false;

  String rxStr;
  if (radio.readData(rxStr) == RADIOLIB_ERR_NONE && rxStr.length() > 0) {
    Serial.printf("[LoRa RX Cmd] %s (RSSI: %.1f dBm)\n", rxStr.c_str(), radio.getRSSI());
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
    html += "<title>Validacao Nó Estufa - Heltec V3 + L298N</title>";
    html += "<style>body{font-family:sans-serif;background:#0f172a;color:#f8fafc;padding:14px;max-width:520px;margin:auto;}";
    html += ".card{background:#1e293b;padding:14px;border-radius:10px;margin-bottom:12px;}";
    html += ".grid{display:grid;grid-template-columns:1fr 1fr;gap:8px;}";
    html += ".btn{background:#22c55e;color:#000;border:none;padding:10px;border-radius:6px;font-weight:bold;cursor:pointer;width:100%;margin-top:6px;}";
    html += ".btn-sec{background:#38bdf8;color:#000;border:none;padding:8px;border-radius:6px;font-weight:bold;cursor:pointer;width:100%;margin-top:6px;}";
    html += ".btn-off{background:#475569;color:#fff;}";
    html += "</style></head><body>";
    html += "<h2>Nó Estufa — Heltec V3 + L298N</h2>";

    html += "<div class='card'><div class='grid'>";
    html += "<div><b>Temp (DHT11 P5):</b><br>" + String(tempAtual, 1) + " °C " + String(dhtConectado ? "(OK)" : "(Simulado)") + "</div>";
    html += "<div><b>Umidade (DHT11 P5):</b><br>" + String(umidAtual, 1) + " %</div>";
    html += "<div><b>Luz (LDR P4):</b><br>" + String(luzPercent) + "% (ADC: " + String(adcLdrBruto) + ")</div>";
    html += "<div><b>Persiana (Servo P6):</b><br>" + String(anguloPersiana) + "°</div>";
    html += "</div><p style='margin-bottom:0;font-size:12px;'><b>JSON LoRa:</b> <code>" + montarJsonTelemetria() + "</code></p></div>";

    html += "<div class='card'><h3>Modo Atual: " + String(modoAutomatico ? "AUTOMÁTICO (LDR -> Servo)" : "MANUAL (Pelos botões abaixo)") + "</h3>";
    html += "<form action='/modo' method='POST'><button class='btn' type='submit'>Alternar para Modo " + String(modoAutomatico ? "MANUAL" : "AUTOMÁTICO") + "</button></form>";
    html += "<form action='/curva' method='POST'><button class='btn-sec' type='submit'>Estilo Automático do Servo: " + String(servoProporcional ? "Contínuo Proporcional (0°-180°)" : "3 Estágios (0° Escuro / 90° Normal / 160° Sol)") + "</button></form>";
    html += "<form action='/invldr' method='POST'><button class='btn-sec' type='submit'>Inverter Polaridade do LDR (Atual: " + String(INVERTER_LDR ? "Invertido" : "Normal") + ")</button></form></div>";

    html += "<div class='card'><h3>Controle Manual da Persiana (Servo P6)</h3><div class='grid'>";
    html += "<form action='/servo?ang=0' method='POST'><button class='btn' type='submit'>Fechar (0°)</button></form>";
    html += "<form action='/servo?ang=45' method='POST'><button class='btn' type='submit'>Meia-Luz (45°)</button></form>";
    html += "<form action='/servo?ang=90' method='POST'><button class='btn' type='submit'>Abrir (90°)</button></form>";
    html += "<form action='/servo?ang=160' method='POST'><button class='btn' type='submit'>Sombra (160°)</button></form>";
    html += "</div></div>";

    html += "<div class='card'><h3>Saídas da Ponte H L298N (OUT1 a OUT4)</h3><div class='grid'>";
    const char* nomesSaida[4] = {
      "OUT1 Ventoinha (IN1/P47)",
      "OUT2 LED Cultivo (IN2/P48)",
      "OUT3 Irrigacao (IN3/P45)",
      "OUT4 Aquecedor (IN4/P46)"
    };
    for (int i = 0; i < 4; i++) {
      html += "<form action='/rele?ch=" + String(i) + "' method='POST'>";
      html += "<button class='btn " + String(saidaEstado[i] ? "" : "btn-off") + "' type='submit'>";
      html += String(nomesSaida[i]) + ": " + String(saidaEstado[i] ? "ON (HIGH)" : "OFF (LOW)") + "</button></form>";
    }
    html += "</div></div></body></html>";
    server.send(200, "text/html", html);
  });

  server.on("/modo", HTTP_POST, []() {
    modoAutomatico = !modoAutomatico;
    if (modoAutomatico) executarControleAutomatico();
    atualizarDisplayOLED();
    server.sendHeader("Location", "/");
    server.send(303);
  });

  server.on("/curva", HTTP_POST, []() {
    servoProporcional = !servoProporcional;
    modoAutomatico = true;
    executarControleAutomatico();
    atualizarDisplayOLED();
    server.sendHeader("Location", "/");
    server.send(303);
  });

  server.on("/invldr", HTTP_POST, []() {
    INVERTER_LDR = !INVERTER_LDR;
    lerSensores();
    executarControleAutomatico();
    atualizarDisplayOLED();
    server.sendHeader("Location", "/");
    server.send(303);
  });

  server.on("/servo", HTTP_POST, []() {
    modoAutomatico = false;
    int ang = server.arg("ang").toInt();
    aplicarAnguloPersiana(ang);
    atualizarDisplayOLED();
    server.sendHeader("Location", "/");
    server.send(303);
  });

  server.on("/rele", HTTP_POST, []() {
    modoAutomatico = false;
    int ch = constrain(server.arg("ch").toInt(), 0, 3);
    saidaEstado[ch] = !saidaEstado[ch];
    atualizarSaidasFisicas();
    atualizarDisplayOLED();
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
  pinMode(PIN_L298_IN1, OUTPUT);
  pinMode(PIN_L298_IN2, OUTPUT);
  pinMode(PIN_L298_IN3, OUTPUT);
  pinMode(PIN_L298_IN4, OUTPUT);
  pinMode(PIN_BUZZER, OUTPUT);
  pinMode(PIN_LED_VERDE, OUTPUT);
  pinMode(PIN_LED_VERM, OUTPUT);
  atualizarSaidasFisicas();

  analogReadResolution(12);
  pinMode(PIN_LDR, INPUT);

  dht.begin();

  // Inicializa apenas 1 Servomotor (Persiana no GPIO 6)
  ESP32PWM::allocateTimer(0);
  servoPersiana.setPeriodHertz(50);
  servoPersiana.attach(PIN_SERVO, 500, 2400);
  servoPersiana.write(anguloPersiana);

  // Inicializa Display OLED
  pinMode(VEXT_CTRL, OUTPUT);
  digitalWrite(VEXT_CTRL, LOW);
  delay(40);
  Wire.begin(OLED_SDA, OLED_SCL);
  oledOk = display.begin(SSD1306_SWITCHCAPVCC, 0x3C);

  // Inicializa Rádio LoRa SX1262 (915 MHz)
  loraSpi.begin(LORA_SCK, LORA_MISO, LORA_MOSI, LORA_NSS);
  if (radio.begin(LORA_FREQ_MHZ, 125.0, 7, 5, RADIOLIB_SX126X_SYNC_WORD_PRIVATE, 14, 8) == RADIOLIB_ERR_NONE) {
    loraOk = true;
    radio.setDio1Action(onLoRaPacket);
    radio.startReceive();
  }

  // Inicializa Wi-Fi AP para validação de bancada
  WiFi.mode(WIFI_AP);
  WiFi.softAP("Estufa_LoRa_V3", "12345678");
  configurarRotasWeb();

  lerSensores();
  executarControleAutomatico();
  atualizarDisplayOLED();

  Serial.println("\n=== NO ESTUFA HELTEC V3 + PONTE H L298N INICIADO ===");
}

void loop() {
  server.handleClient();
  verificarComandosLoRa();

  // Leitura rápida do LDR e atualização do Servo a cada 600ms
  if (millis() - lastSensorRead >= SENSOR_INTERVAL_MS) {
    lastSensorRead = millis();
    lerSensores();
    executarControleAutomatico();
    atualizarDisplayOLED();
  }

  // Envio periódico de telemetria LoRa 915 MHz a cada 5s
  if (millis() - lastLoRaTx >= LORA_TX_INTERVAL_MS) {
    lastLoRaTx = millis();
    enviarTelemetriaLoRa();
  }

  // Botão PRG físico na placa alterna AUTO/MAN
  if (digitalRead(BTN_PRG) == LOW) {
    delay(50);
    if (digitalRead(BTN_PRG) == LOW) {
      modoAutomatico = !modoAutomatico;
      if (modoAutomatico) executarControleAutomatico();
      atualizarDisplayOLED();
      while (digitalRead(BTN_PRG) == LOW) {
        server.handleClient();
        delay(10);
      }
    }
  }
}
