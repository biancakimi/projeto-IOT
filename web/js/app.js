/**
 * ======================================================================================
 * PROJETO FINAL IoT (UNIFESP) — LÓGICA DO CLIENTE WEB (app.js)
 * ======================================================================================
 * Descrição:
 *   Script principal do painel web responsável por:
 *   1. Conectar ao Broker MQTT em nuvem via WebSockets Seguro (WSS - porta 8884).
 *   2. Subscrever ao tópico `unifesp/iot/estufa/telemetria` para atualizar os indicadores
 *      na tela em tempo real.
 *   3. Publicar comandos nos tópicos `unifesp/iot/estufa/comando/*` ao mover o slider
 *      da persiana ou acionar os botões dos 4 relés e buzzer.
 *   4. Buscar dados históricos da API REST do banco de dados em nuvem (Supabase/PostgreSQL)
 *      para renderizar os gráficos temporais.
 * ======================================================================================
 */

console.log("SmartGreenhouse LoRa Web Client inicializado.");
// TODO: Implementar conexão MQTT.js (WSS) e integração com banco de dados de histórico
