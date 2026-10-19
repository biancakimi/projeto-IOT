/*
 * ======================================================================================
 * PROJETO FINAL IoT (UNIFESP) — NÓ ESTUFA REMOTA (ESP32 LoRa #1 - 915 MHz)
 * ======================================================================================
 * Descrição:
 *   Firmware responsável por operar fisicamente na estufa remota (sem necessidade de Wi-Fi):
 *   1. Leitura periódica dos sensores:
 *      - Luminosidade via sensor LDR (GPIO 36 / ADC1 ou via ADS1115 I2C)
 *      - Temperatura (°C) e Umidade Relativa (%) via sensor DHT11 (GPIO 13)
 *   2. Controle de atuadores:
 *      - Servomotores (GPIO 12 e GPIO 25): Abertura gradual das persianas (0° a 180°)
 *      - Módulo Relé 4 Canais (GPIOs 17, 2, 27, 32):
 *          * Canal 1: Exaustor / Ventilador
 *          * Canal 2: Iluminação Artificial (Grow LED)
 *          * Canal 3: Bomba / Solenoide de Irrigação ou Nebulização
 *          * Canal 4: Aquecedor Térmico
 *      - Sinalização Local: LEDs de Status (GPIOs 21, 22) e Buzzer de Alerta (GPIO 33)
 *   3. Comunicação LoRa P2P em 915 MHz (SX1276 - SPI: GPIOs 5, 18, 19, 23, 14, 26):
 *      - Envia pacotes JSON de telemetria para o Nó Gateway
 *      - Recebe comandos remotos (Modo AUTO/MAN, ângulo da persiana, relés e setpoints)
 * ======================================================================================
 */

#include <Arduino.h>

void setup() {
  Serial.begin(115200);
  // TODO: Inicializar pinos de sensores (DHT11, LDR), atuadores (Servos, Relés, LEDs, Buzzer) e rádio LoRa 915 MHz
}

void loop() {
  // TODO: Implementar leitura não-bloqueante (millis), máquina de estados (AUTO/MAN) e transcepção LoRa
}
