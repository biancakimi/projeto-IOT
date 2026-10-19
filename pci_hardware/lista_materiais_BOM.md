# Lista de Materiais (BOM — Bill of Materials) da PCI

> Este arquivo documenta todos os componentes eletrônicos necessários para a montagem da **Placa de Circuito Impresso (PCI)** do Nó Estufa e do Nó Gateway.

| Item | Quantidade | Componente / Especificação | Designador na PCI | Observações |
| :---: | :---: | :--- | :---: | :--- |
| 1 | 2 | Módulo ESP32 com LoRa 915 MHz (SX1276) | `U1` (Estufa), `U2` (Gateway) | 1x na PCI da Estufa + 1x no Gateway |
| 2 | 1 | Sensor de Temperatura e Umidade DHT11 | `S1` | Conectado ao `GPIO 13` |
| 3 | 1 | Fotoresistor LDR 5mm | `LDR1` | Divisor de tensão no `GPIO 36 (VP)` |
| 4 | 1 | Servomotor MG996R (ou SG90) | `SRV1` | Controle mecânico da persiana (`GPIO 12`) |
| 5 | 1 | Módulo Relé 4 Canais 5V Optoacoplado | `REL1` | Acionamento de cargas (`GPIOs 17, 2, 27, 32`) |
| 6 | 1 | Capacitor Eletrolítico 100 µF / 16V (ou 470 µF) | `C1` | Desacoplamento de alimentação do Servomotor |
| 7 | 1 | Capacitor Cerâmico 100 nF | `C2` | Filtragem de ruído de alta frequência |
| 8 | 1 | Buzzer Ativo 5V + Transistor NPN BC548 | `BZ1`, `Q1` | Alarme sonoro local (`GPIO 33`) |
| 9 | 2 | LEDs Difusos 5mm (Verde e Vermelho) | `D1`, `D2` | Sinalização de status (`GPIOs 21, 22`) |
| 10 | 5 | Resistores diversos (10 kΩ, 4.7 kΩ, 1 kΩ, 330 Ω) | `R1`–`R5` | Pull-up DHT11, divisor LDR, base transistor e LEDs |
| 11 | 1 | Borne KRE 2 Vias (Alimentação 5V / GND) + Barras de Pinos | `J1`–`J5` | Conectores modulares da PCI |
