# Lista de Materiais (BOM — Bill of Materials) da PCI

> Este arquivo documenta todos os componentes eletrônicos utilizados no esquemático ([`kicad/estufa_lora_v3.kicad_sch`](kicad/estufa_lora_v3.kicad_sch)) e no layout de face única da **Placa de Circuito Impresso (PCI)** ([`kicad/estufa_lora_v3.kicad_pcb`](kicad/estufa_lora_v3.kicad_pcb)).

| Item | Quantidade | Componente / Especificação | Designador no KiCad | Função / Ligação na PCI |
| :---: | :---: | :--- | :---: | :--- |
| 1 | 2 | Módulo Heltec WiFi LoRa 32 V3 (ESP32-S3 + Semtech SX1262 915 MHz + OLED) | `U1` (Estufa), `U2` (Gateway) | `U1` encaixado na PCI da Estufa (alimentado pelo pino `5V` e `GND`) + `U2` no Gateway |
| 2 | 1 | Borne KRE 2 Vias (Passo 5.08 mm) — Entrada da Fonte Externa 5V / GND | `J1` | Alimenta o pino `5V` do Heltec V3 (`U1`), o Módulo Relé 4 Canais (`J2`) e o Servo (`J3`) |
| 3 | 1 | Capacitor Eletrolítico 100 µF / 16V | `C1` | Filtragem e desacoplamento da linha de `+5V` e `GND` comum |
| 4 | 1 | Módulo Relé 4 Canais 5V (Conector 1x06 Pinos 2.54 mm) | `J2` | `1: 5V`, `2: GPIO 34` (Aquecedor), `3: GPIO 33` (Irrigação), `4: GPIO 47` (Ventoinha), `5: GPIO 48` (Iluminação), `6: GND` |
| 5 | 1 | Servomotor da Persiana (Conector 1x03 Pinos 2.54 mm) | `J3` | Alimentado em `+5V` / `GND` e controlado via PWM no `GPIO 6` (`SERVO_P6`) |
| 6 | 1 | Sensor de Temperatura e Umidade DHT11 (Conector 1x03 Pinos 2.54 mm) | `J4` | Alimentado em `+3V3` / `GND` e ligado direto ao `GPIO 5` (`DHT11_P5`, sem resistor externo) |
| 7 | 1 | Fotoresistor LDR 5mm | `R1` | Ligado entre `+3V3` e o pino analógico `GPIO 4` (`LDR_P4`) |
| 8 | 1 | Resistor 10 kΩ (1/4 W Axial THT) | `R2` | Divisor de tensão do LDR entre `GPIO 4` (`LDR_P4`) e `GND` |
| 9 | 2 | Resistores 1 kΩ (1/4 W Axial THT) | `R3`, `R4` | Limitadores de corrente dos LEDs de status (`GPIO 41` e `GPIO 40`) |
| 10 | 2 | LEDs Difusos 5mm (Verde e Vermelho) | `D1`, `D2` | `D1` (Verde — Status Normal no `GPIO 41`) e `D2` (Vermelho — Alarme Crítico no `GPIO 40`) |
| 11 | 1 | Buzzer Ativo 5V / 3.3V (Passo 7.6 mm) | `BZ1` | Alarme sonoro local conectado entre `GPIO 42` (`BUZZER_P42`) e `GND` |
| 12 | 1 | Ventoinha 5V (Cooler / Exaustor) | `FAN1` | Acionada pelo contato `NA` do canal `D1` do Módulo Relé 1 (`GPIO 47`) |
