/*
 * ======================================================================================
 * PROJETO FINAL IoT (UNIFESP) — NÓ GATEWAY CENTRAL (ESP32 LoRa #2 - 915 MHz <-> MQTT)
 * ======================================================================================
 * Descrição:
 *   Firmware responsável por atuar como ponte (Bridge) entre o rádio LoRa e a Nuvem:
 *   1. Recebe pacotes LoRa 915 MHz enviados pelo Nó Estufa (#1).
 *   2. Captura métricas de qualidade do enlace de rádio (RSSI em dBm e SNR em dB).
 *   3. Conecta-se à rede Wi-Fi e publica o payload JSON no Broker MQTT em nuvem
 *      (Tópico: unifesp/iot/estufa/telemetria).
 *   4. Subscreve aos tópicos de comando vindos do Node-RED / Site Web
 *      (Tópico: unifesp/iot/estufa/comando/#) e retransmite imediatamente via LoRa.
 *   5. Suporta Atualização Remota de Firmware (ArduinoOTA / HTTPUpdate) - Desafio Extra.
 * ======================================================================================
 */

#include <Arduino.h>

void setup() {
  Serial.begin(115200);
  // TODO: Inicializar rádio LoRa 915 MHz, conexão Wi-Fi, cliente MQTT e serviço ArduinoOTA
}

void loop() {
  // TODO: Escutar pacotes LoRa -> Publicar no MQTT | Escutar callbacks MQTT -> Transmitir via LoRa
}
