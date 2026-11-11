# Lista de Materiais (BOM — Bill of Materials) da PCI

> Este arquivo documenta todos os componentes eletrônicos necessários para a montagem da **Placa de Circuito Impresso (PCI)** do Nó Estufa e do Nó Gateway.

| Item | Quantidade | Componente / Especificação | Designador na PCI | Observações |
| :---: | :---: | :--- | :---: | :--- |
| 1 | 2 | Módulo Heltec WiFi LoRa 32 V3 (ESP32-S3 + Semtech SX1262 915 MHz + OLED) | `U1` (Estufa), `U2` (Gateway) | 1x na PCI da Estufa + 1x no Gateway |
| 2 | 1 | Sensor de Temperatura e Umidade DHT11 | `S1` | Conectado ao `GPIO 5` |
| 3 | 1 | Fotoresistor LDR 5mm | `LDR1` | Divisor de tensão no `GPIO 4 (ADC1)` |
| 4 | 1 | Servomotor da Persiana MG996R (ou SG90) | `SRV1` | Controle mecânico da persiana (`GPIO 6`) |
| 5 | 1 | Módulo Driver Ponte H L298N (ZX-040) | `DRV1` | Aciona Ventoinha 5V e LEDs (`GPIOs 47, 48, 45, 46`) |
| 6 | 1 | Ventoinha 5V (Cooler / Exaustor) | `FAN1` | Conectada ao borne `OUT1` da Ponte H L298N |
| 7 | 3 | LEDs Difusos 5mm (Branco, Azul, Vermelho) | `LED1`–`LED3` | OUT2 (Grow), OUT3 (Irrigação), OUT4 (Aquecimento) |
| 8 | 1 | Capacitor Eletrolítico 100 µF / 16V (ou 470 µF) | `C1` | Desacoplamento da alimentação do Servomotor |
| 9 | 1 | Capacitor Cerâmico 100 nF | `C2` | Filtragem de ruído de alta frequência |
| 10 | 1 | Buzzer Ativo 5V / 3.3V | `BZ1` | Alarme sonoro local (`GPIO 42`) |
| 11 | 2 | LEDs Difusos 5mm (Verde e Vermelho) | `D1`, `D2` | Sinalização de status local (`GPIOs 41, 40`) |
| 12 | 6 | Resistores diversos (10 kΩ, 4.7 kΩ, 330 Ω) | `R1`–`R6` | Pull-up DHT11, divisor LDR e limitadores de LEDs |
| 13 | 1 | Borne KRE 2 Vias (Alimentação 5V / GND) + Barras de Pinos | `J1`–`J5` | Conectores modulares da PCI |
