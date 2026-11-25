# Projeto de Placa de Circuito Impresso (PCI) — `pci_hardware/`

Esta pasta armazena os artefatos completos referentes à etapa de **Projeto e Confecção da Placa de Circuito Impresso (PCI)** do Nó Estufa (**Heltec WiFi LoRa 32 V3**), elaborados no **KiCad 10.0** seguindo rigorosamente o padrão das videoaulas da disciplina (**Prof. Dr. Gabriel de Freitas Fernandes**):

* [`kicad/estufa_lora_v3.kicad_pro`](kicad/estufa_lora_v3.kicad_pro): Arquivo principal do projeto KiCad.
* [`kicad/estufa_lora_v3.kicad_sch`](kicad/estufa_lora_v3.kicad_sch): Diagrama esquemático elétrico completo seguindo o padrão da disciplina:
  * Símbolos customizados na biblioteca local [`kicad/heltec_iot.kicad_sym`](kicad/heltec_iot.kicad_sym): **`U1` (`Heltec_WiFi_LoRa_32_V3`)** com a pinagem física oficial completa (2x18 pinos) e **`J2` (`Modulo_Rele_4_Canais_5V`)** representando explicitamente as 4 portas/canais (`K1` Ventoinha, `K2` Iluminação, `K3` Irrigação e `K4` Aquecedor).
  * Uso de símbolos de aterramento **`GND` (`power:GND`)**, etiquetas globais (**`global_label`**) para barramentos e sinais dos conectores (`5V`, `3V3`, `RELE1_P47`, `RELE2_P48`, `RELE3_P33`, `RELE4_P34`, `LDR_P4`, `DHT11_P5`, `SERVO_P6`, `LED_VERM_P40`, `LED_VERDE_P41`, `BUZZER_P42`) e ligação direta por fios dentro de cada subcircuito.
  * **Fonte externa de `5V` (`J1` + capacitor `C1` de `100uF`)** alimentando diretamente o **pino `5V` do Heltec V3 (`U1`)**, o **Módulo Relé de 4 Canais (`J2`, conector 1x06 pinos)** e o **Servomotor (`J3`)**.
  * **Resistores de `1k` (`R3`, `R4`)** nos LEDs de status (`D1` Verde e `D2` Vermelho), divisor de tensão do **LDR com resistor de `10k` (`R1`, `R2`)** e **DHT11 (`J4`) ligado diretamente ao `GPIO 5` (sem resistor externo)**.
  * Nota de documentação de projeto detalhando decisões de hardware, autores, revisão e data.
* [`kicad/estufa_lora_v3.kicad_pcb`](kicad/estufa_lora_v3.kicad_pcb): Layout da Placa de Circuito Impresso (**Face Única `F.Cu` + Plano Terra `GND`** sem alívio térmico e com espaçamento de `0.8 mm` / trilhas de `1.0 mm` para potência e `0.5 mm` para sinal, otimizado para a fresadora CNC do laboratório da UNIFESP, aprovado no DRC com **0 violações e 0 itens desconectados**).
* [`kicad/esquematico_estufa_lora.pdf`](kicad/esquematico_estufa_lora.pdf): PDF do esquemático elétrico pronto para impressão/apresentação.
* [`kicad/layout_pci_estufa_lora.pdf`](kicad/layout_pci_estufa_lora.pdf): PDF do layout das trilhas e Plano Terra da PCI.
* `kicad/gerbers/`: Arquivos industriais Gerber (`.gtl`, `.gbl`, `.gto`, `.gts`, `.gm1`) e furação Excellon (`.drl`) prontos para fabricação da PCI.
* [`lista_materiais_BOM.md`](lista_materiais_BOM.md): Lista de componentes (Bill of Materials) atualizada.
