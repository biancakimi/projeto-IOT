/**
 * ======================================================================================
 * PROJETO FINAL IoT (UNIFESP) — LÓGICA DO CLIENTE WEB (app.js)
 * ======================================================================================
 * Arquitetura completa:
 *   ESP 1 (Estufa) <--LoRa 915MHz--> ESP 2 (Gateway) <--MQTT WSS--> Site Web & Banco Cloud
 * ======================================================================================
 */

const TOPIC_TELEMETRIA = "unifesp/iot/estufa/telemetria";
const TOPIC_GW_STATUS  = "unifesp/iot/estufa/gateway/status";
const TOPIC_CMD_MAIN   = "unifesp/iot/estufa/comando";
const TOPIC_CMD_MODO   = "unifesp/iot/estufa/comando/modo";
const TOPIC_CMD_SERVO  = "unifesp/iot/estufa/comando/persiana";
const TOPIC_CMD_RELES  = "unifesp/iot/estufa/comando/reles";
const TOPIC_CMD_CONFIG = "unifesp/iot/estufa/comando/config";

let mqttClient = null;
let telemetryChart = null;
let lastDbSaveTime = 0;

// Estado espelhado da estufa (ESP 1)
const estadoEstufa = {
  modo: "AUTO",
  temp: null,
  umid: null,
  luz: null,
  adc: 0,
  servo: 90,
  reles: [0, 0, 0, 0],
  alarme: 0,
  buzzer: 1,
  invRele: 0,
  rssi: null,
  snr: null,
  seq: 0
};

// ================= 1. UTILITÁRIOS DE LOG E UI =================
function adicionarLog(tipo, texto) {
  const box = document.getElementById("logConsole");
  if (!box) return;
  const div = document.createElement("div");
  const hora = new Date().toLocaleTimeString("pt-BR");
  div.className = `log-entry log-${tipo}`;
  div.textContent = `[${hora}] ${texto}`;
  box.prepend(div);
  while (box.children.length > 80) {
    box.removeChild(box.lastChild);
  }
}

function limparLog() {
  const box = document.getElementById("logConsole");
  if (box) box.innerHTML = "";
}

function atualizarBadge(dotId, txtId, status, texto) {
  const dot = document.getElementById(dotId);
  const txt = document.getElementById(txtId);
  if (dot) {
    dot.className = `dot dot-${status}`;
  }
  if (txt) {
    txt.textContent = texto;
  }
}

// ================= 2. ATUALIZAÇÃO DA INTERFACE COM DADOS DO ESP 1 =================
function atualizarInterfaceTelemetria(dados) {
  if (typeof dados.t !== "undefined") estadoEstufa.temp = Number(dados.t);
  if (typeof dados.h !== "undefined") estadoEstufa.umid = Number(dados.h);
  if (typeof dados.l !== "undefined") estadoEstufa.luz = Number(dados.l);
  if (typeof dados.adc !== "undefined") estadoEstufa.adc = Number(dados.adc);
  if (typeof dados.srv !== "undefined") estadoEstufa.servo = Number(dados.srv);
  if (Array.isArray(dados.r) && dados.r.length >= 4) {
    estadoEstufa.reles = dados.r.map((v) => (Number(v) === 1 ? 1 : 0));
  }
  if (typeof dados.al !== "undefined") estadoEstufa.alarme = Number(dados.al);
  if (typeof dados.buz !== "undefined") estadoEstufa.buzzer = Number(dados.buz);
  if (typeof dados.inv !== "undefined") estadoEstufa.invRele = Number(dados.inv);
  if (typeof dados.m === "string") estadoEstufa.modo = dados.m;
  if (typeof dados.rssi !== "undefined") estadoEstufa.rssi = Number(dados.rssi);
  if (typeof dados.snr !== "undefined") estadoEstufa.snr = Number(dados.snr);
  if (typeof dados.seq !== "undefined") estadoEstufa.seq = Number(dados.seq);

  // Temperatura
  if (estadoEstufa.temp !== null && !Number.isNaN(estadoEstufa.temp)) {
    document.getElementById("valTemp").textContent = estadoEstufa.temp.toFixed(1);
    const pctTemp = Math.min(100, Math.max(0, ((estadoEstufa.temp - 10) / 35) * 100));
    document.getElementById("barTemp").style.width = `${pctTemp}%`;
  }

  const pillDht = document.getElementById("pillDht");
  if (pillDht && typeof dados.dht !== "undefined") {
    pillDht.textContent = Number(dados.dht) === 1 ? "DHT11 · OK" : "DHT11 · Simulado";
    pillDht.className = Number(dados.dht) === 1 ? "tag pill-green" : "tag pill-amber";
  }

  // Umidade
  if (estadoEstufa.umid !== null && !Number.isNaN(estadoEstufa.umid)) {
    document.getElementById("valUmid").textContent = estadoEstufa.umid.toFixed(0);
    const pctUmid = Math.min(100, Math.max(0, estadoEstufa.umid));
    document.getElementById("barUmid").style.width = `${pctUmid}%`;
  }

  // Luminosidade LDR
  if (estadoEstufa.luz !== null && !Number.isNaN(estadoEstufa.luz)) {
    document.getElementById("valLuz").textContent = estadoEstufa.luz;
    document.getElementById("barLuz").style.width = `${Math.min(100, Math.max(0, estadoEstufa.luz))}%`;
    document.getElementById("pillAdc").textContent = `ADC ${estadoEstufa.adc}`;
  }

  // Rádio LoRa (ESP 1 -> ESP 2)
  if (estadoEstufa.rssi !== null && !Number.isNaN(estadoEstufa.rssi)) {
    document.getElementById("valRssi").textContent = estadoEstufa.rssi;
    const pctRssi = Math.min(100, Math.max(5, ((estadoEstufa.rssi + 120) / 90) * 100));
    document.getElementById("barRssi").style.width = `${pctRssi}%`;
    atualizarBadge("dotLoRa", "txtLoRaStatus", "on", `LoRa: ${estadoEstufa.rssi} dBm`);
  }
  if (estadoEstufa.snr !== null && !Number.isNaN(estadoEstufa.snr)) {
    document.getElementById("valSnr").textContent = `${estadoEstufa.snr.toFixed(1)} dB`;
  }
  document.getElementById("pillSeq").textContent = `#${estadoEstufa.seq}`;
  atualizarBadge("dotGw", "txtGwStatus", "on", "Gateway: online");

  // Horário
  const agoraStr = new Date().toLocaleTimeString("pt-BR");
  document.getElementById("txtLastUpdate").textContent = `Último pacote recebido às ${agoraStr}`;

  // Modo Automático / Manual
  const isAuto = estadoEstufa.modo === "AUTO";
  document.getElementById("btnModoAuto").classList.toggle("active", isAuto);
  document.getElementById("btnModoMan").classList.toggle("active", !isAuto);

  // Persiana (Servomotor)
  const ang = estadoEstufa.servo;
  let descPersiana = "Aberta";
  if (ang <= 15) descPersiana = "Fechada";
  else if (ang < 70) descPersiana = "Meia-Luz";
  else if (ang >= 130) descPersiana = "Sombreamento";
  document.getElementById("lblEstadoPersiana").textContent = `${ang}° (${descPersiana})`;
  document.getElementById("sliderServo").value = ang;
  document.getElementById("lblSliderPreview").textContent = `${ang}°`;

  // 4 Relés
  for (let i = 0; i < 4; i++) {
    const ligado = estadoEstufa.reles[i] === 1;
    const btn = document.getElementById(`btnRele${i}`);
    const badge = document.getElementById(`stateRele${i}`);
    if (btn) {
      btn.classList.toggle("on", ligado);
      btn.classList.toggle("off", !ligado);
    }
    if (badge) {
      badge.textContent = ligado ? "ON" : "OFF";
    }
  }

  // Lógica dos Relés e Buzzer
  const invAtivo = estadoEstufa.invRele === 1;
  document.getElementById("pillLogicaRele").textContent = invAtivo ? "Ativo em LOW" : "Ativo em HIGH";
  document.getElementById("txtInvRele").textContent = invAtivo ? "LOW (0V)" : "HIGH (3.3V)";
  document.getElementById("txtBuzzer").textContent = estadoEstufa.buzzer === 1 ? "Ativo" : "Silenciado";

  // Banner de Alarme Crítico
  const banner = document.getElementById("alarmBanner");
  if (banner) {
    banner.classList.toggle("hidden", estadoEstufa.alarme !== 1);
  }

  // Adiciona ponto no gráfico em tempo real
  if (estadoEstufa.temp !== null && estadoEstufa.umid !== null && estadoEstufa.luz !== null) {
    adicionarPontoGrafico(agoraStr, estadoEstufa.temp, estadoEstufa.umid, estadoEstufa.luz);
  }

  // Salva periodicamente no banco em nuvem (a cada 20s)
  if (Date.now() - lastDbSaveTime >= 20000) {
    lastDbSaveTime = Date.now();
    salvarLeituraAtualNoBanco(true);
  }
}

// ================= 3. ENVIO DE ORDENS DO SITE -> MQTT & VERCEL API -> ESP 2 -> LORA -> ESP 1 =================
function publicarComandoMqtt(topico, payloadObj) {
  const payloadStr = typeof payloadObj === "string" ? payloadObj : JSON.stringify(payloadObj);

  // 1. Envia em tempo real via MQTT WebSockets (evita comandos duplicados/atrasados no servo)
  if (mqttClient && mqttClient.connected) {
    mqttClient.publish(topico, payloadStr, { qos: 0, retain: false });
    adicionarLog("tx", `ORDEM MQTT -> ${topico}: ${payloadStr}`);
  } else {
    // 2. Fallback via API da Vercel (/api/historico?tipo=comando) apenas quando MQTT estiver desconectado
    adicionarLog("sys", "MQTT reconectando — enviando ordem via API da Vercel...");
    fetch("/api/historico?tipo=comando", {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: payloadStr
    }).catch(() => {});
  }
}

function enviarModo(novoModo) {
  estadoEstufa.modo = novoModo;
  document.getElementById("btnModoAuto").classList.toggle("active", novoModo === "AUTO");
  document.getElementById("btnModoMan").classList.toggle("active", novoModo !== "AUTO");

  publicarComandoMqtt(TOPIC_CMD_MAIN, {
    cmd: "SET",
    m: novoModo
  });
}

function enviarPersiana(angulo) {
  const ang = Math.max(0, Math.min(180, parseInt(angulo, 10) || 0));
  estadoEstufa.modo = "MAN";
  estadoEstufa.servo = ang;
  document.getElementById("sliderServo").value = ang;
  document.getElementById("lblSliderPreview").textContent = `${ang}°`;

  publicarComandoMqtt(TOPIC_CMD_MAIN, {
    cmd: "SET",
    m: "MAN",
    srv: ang
  });
}

function alternarRele(canal) {
  const ch = Math.max(0, Math.min(3, Number(canal)));
  const novoEstado = estadoEstufa.reles[ch] === 1 ? 0 : 1;
  estadoEstufa.modo = "MAN";
  estadoEstufa.reles[ch] = novoEstado;

  // Feedback visual imediato
  const btn = document.getElementById(`btnRele${ch}`);
  const badge = document.getElementById(`stateRele${ch}`);
  if (btn) {
    btn.classList.toggle("on", novoEstado === 1);
    btn.classList.toggle("off", novoEstado === 0);
  }
  if (badge) badge.textContent = novoEstado === 1 ? "ON" : "OFF";

  publicarComandoMqtt(TOPIC_CMD_MAIN, {
    cmd: "SET",
    m: "MAN",
    ch: ch,
    state: novoEstado
  });
}

function alternarPolaridadeReles() {
  const novoInv = estadoEstufa.invRele === 1 ? 0 : 1;
  estadoEstufa.invRele = novoInv;
  publicarComandoMqtt(TOPIC_CMD_MAIN, {
    cmd: "SET",
    inv: novoInv
  });
}

function alternarBuzzer() {
  const novoBuz = estadoEstufa.buzzer === 1 ? 0 : 1;
  estadoEstufa.buzzer = novoBuz;
  publicarComandoMqtt(TOPIC_CMD_MAIN, {
    cmd: "SET",
    buz: novoBuz
  });
}

function enviarConfigLimiares() {
  const tMax  = parseFloat(document.getElementById("cfgTempMax").value) || 28.0;
  const tMin  = parseFloat(document.getElementById("cfgTempMin").value) || 18.0;
  const tCrit = parseFloat(document.getElementById("cfgTempCrit").value) || 35.0;
  const hMin  = parseFloat(document.getElementById("cfgUmidMin").value) || 45.0;
  const lMin  = parseInt(document.getElementById("cfgLuzMin").value, 10) || 25;
  const lMax  = parseInt(document.getElementById("cfgLuzMax").value, 10) || 75;

  document.getElementById("lblRangeTemp").textContent = `${tMin}°C – ${tMax}°C`;
  document.getElementById("lblMinUmid").textContent = `${hMin}%`;

  publicarComandoMqtt(TOPIC_CMD_CONFIG, {
    cmd: "CFG",
    tMax,
    tMin,
    tCrit,
    hMin,
    lMin,
    lMax
  });
}

// ================= 4. CONEXÃO MQTT OVER WEBSOCKETS (WSS) =================
function conectarMqtt() {
  const brokerInput = document.getElementById("inputBrokerUrl");
  const brokerUrl = brokerInput ? brokerInput.value.trim() : "wss://broker.emqx.io:8084/mqtt";

  if (mqttClient) {
    try {
      mqttClient.end(true);
    } catch (_) {}
  }

  atualizarBadge("dotMqtt", "txtMqttStatus", "wait", "MQTT: conectando");
  adicionarLog("sys", `Conectando ao broker MQTT: ${brokerUrl}`);

  const clientId = "unifesp_web_" + Math.random().toString(16).substring(2, 10);
  mqttClient = mqtt.connect(brokerUrl, {
    clientId,
    clean: true,
    connectTimeout: 8000,
    reconnectPeriod: 3000
  });

  mqttClient.on("connect", () => {
    atualizarBadge("dotMqtt", "txtMqttStatus", "on", "MQTT: conectado");
    adicionarLog("sys", `Conectado ao broker MQTT (${clientId}).`);
    mqttClient.subscribe([TOPIC_TELEMETRIA, TOPIC_GW_STATUS], { qos: 0 });
  });

  mqttClient.on("reconnect", () => {
    atualizarBadge("dotMqtt", "txtMqttStatus", "wait", "MQTT: reconectando");
  });

  mqttClient.on("error", (err) => {
    atualizarBadge("dotMqtt", "txtMqttStatus", "off", "MQTT: erro");
    adicionarLog("sys", `Erro MQTT: ${err.message}`);
  });

  mqttClient.on("message", (topic, message) => {
    const raw = message.toString();
    try {
      const json = JSON.parse(raw);
      if (topic === TOPIC_TELEMETRIA) {
        adicionarLog("rx", `RX ${raw}`);
        atualizarInterfaceTelemetria(json);
      } else if (topic === TOPIC_GW_STATUS) {
        atualizarBadge("dotGw", "txtGwStatus", "on", `Gateway: ${json.ip || "online"}`);
        if (json.ip) {
          document.getElementById("valGwIp").textContent = json.ip;
        }
      }
    } catch (e) {
      adicionarLog("rx", `[${topic}] ${raw}`);
    }
  });
}

// ================= 5. GRÁFICO TEMPORAL E BANCO DE DADOS EM NUVEM (/api/historico) =================
function inicializarGrafico() {
  const ctx = document.getElementById("telemetryChart");
  if (!ctx || typeof Chart === "undefined") return;

  telemetryChart = new Chart(ctx, {
    type: "line",
    data: {
      labels: [],
      datasets: [
        {
          label: "Temperatura (°C)",
          data: [],
          borderColor: "#c2410c",
          backgroundColor: "rgba(194, 65, 12, 0.08)",
          borderWidth: 2,
          pointRadius: 2,
          tension: 0.25,
          fill: false
        },
        {
          label: "Umidade (%)",
          data: [],
          borderColor: "#0369a1",
          backgroundColor: "rgba(3, 105, 161, 0.08)",
          borderWidth: 2,
          pointRadius: 2,
          tension: 0.25,
          fill: false
        },
        {
          label: "Luminosidade (%)",
          data: [],
          borderColor: "#b45309",
          backgroundColor: "rgba(180, 83, 9, 0.08)",
          borderWidth: 2,
          pointRadius: 2,
          tension: 0.25,
          fill: false
        }
      ]
    },
    options: {
      responsive: true,
      maintainAspectRatio: false,
      scales: {
        x: { ticks: { color: "#5c6860" }, grid: { color: "#eef1ec" } },
        y: { ticks: { color: "#5c6860" }, grid: { color: "#eef1ec" }, suggestedMin: 0, suggestedMax: 100 }
      },
      plugins: {
        legend: {
          labels: { color: "#18201b", boxWidth: 12, usePointStyle: true }
        }
      }
    }
  });
}

function adicionarPontoGrafico(labelHora, temp, umid, luz) {
  if (!telemetryChart) return;
  const labels = telemetryChart.data.labels;
  labels.push(labelHora);
  telemetryChart.data.datasets[0].data.push(temp);
  telemetryChart.data.datasets[1].data.push(umid);
  telemetryChart.data.datasets[2].data.push(luz);

  if (labels.length > 30) {
    labels.shift();
    telemetryChart.data.datasets.forEach((ds) => ds.data.shift());
  }
  telemetryChart.update("none");
}

async function carregarHistoricoBanco() {
  try {
    const resp = await fetch("/api/historico?limit=25");
    if (!resp.ok) return;
    const rows = await resp.json();
    if (!Array.isArray(rows) || rows.length === 0 || !telemetryChart) return;

    // Se ainda não chegou pacote MQTT ao vivo, preenche os cards com o registro mais recente do banco na Vercel
    if (estadoEstufa.temp === null && rows[0]) {
      const u = rows[0];
      atualizarInterfaceTelemetria({
        t: Number(u.temperatura),
        h: Number(u.umidade),
        l: Number(u.luminosidade),
        srv: Number(u.angulo_persiana),
        r: [
          u.rele1_exaustor ? 1 : 0,
          u.rele2_iluminacao ? 1 : 0,
          u.rele3_irrigacao ? 1 : 0,
          u.rele4_aquecedor ? 1 : 0
        ],
        al: u.alarme_ativo ? 1 : 0,
        m: u.modo_operacao || "AUTO",
        rssi: u.lora_rssi,
        snr: u.lora_snr
      });
    }

    const ordenados = rows.slice().reverse();
    telemetryChart.data.labels = [];
    telemetryChart.data.datasets[0].data = [];
    telemetryChart.data.datasets[1].data = [];
    telemetryChart.data.datasets[2].data = [];

    ordenados.forEach((r) => {
      const hora = r.created_at ? new Date(r.created_at).toLocaleTimeString("pt-BR") : "--";
      telemetryChart.data.labels.push(hora);
      telemetryChart.data.datasets[0].data.push(Number(r.temperatura));
      telemetryChart.data.datasets[1].data.push(Number(r.umidade));
      telemetryChart.data.datasets[2].data.push(Number(r.luminosidade));
    });
    telemetryChart.update();
    renderizarTabelaHistorico(rows.slice(0, 10));
    adicionarLog("sys", `Histórico carregado do PostgreSQL (${rows.length} registros).`);
  } catch (_) {
    // Silencioso caso esteja rodando localmente sem serverless API
  }
}

function renderizarTabelaHistorico(rows) {
  const tbody = document.getElementById("historyTableBody");
  if (!tbody) return;
  if (!Array.isArray(rows) || rows.length === 0) {
    tbody.innerHTML = '<tr><td colspan="8" class="table-empty">Nenhum registro encontrado no banco.</td></tr>';
    return;
  }
  tbody.innerHTML = rows.map((r) => {
    const hora = r.created_at ? new Date(r.created_at).toLocaleTimeString("pt-BR") : "--";
    const temp = r.temperatura != null ? `${Number(r.temperatura).toFixed(1)} °C` : "--";
    const umid = r.umidade != null ? `${Number(r.umidade).toFixed(0)}%` : "--";
    const luz = r.luminosidade != null ? `${r.luminosidade}%` : "--";
    const srv = r.angulo_persiana != null ? `${r.angulo_persiana}°` : "--";
    const reles = [
      r.rele1_exaustor ? "1" : "0",
      r.rele2_iluminacao ? "1" : "0",
      r.rele3_irrigacao ? "1" : "0",
      r.rele4_aquecedor ? "1" : "0"
    ].join(" ");
    const modo = r.modo_operacao || "--";
    const rssi = r.lora_rssi != null ? `${r.lora_rssi} dBm` : "--";
    return `<tr>
      <td class="mono">${hora}</td>
      <td class="mono">${temp}</td>
      <td class="mono">${umid}</td>
      <td class="mono">${luz}</td>
      <td class="mono">${srv}</td>
      <td class="mono">${reles}</td>
      <td>${modo}</td>
      <td class="mono">${rssi}</td>
    </tr>`;
  }).join("");
}

async function salvarLeituraAtualNoBanco(silencioso = false) {
  if (estadoEstufa.temp === null || estadoEstufa.umid === null || estadoEstufa.luz === null) return;
  try {
    const resp = await fetch("/api/historico", {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify({
        temperatura: estadoEstufa.temp,
        umidade: estadoEstufa.umid,
        luminosidade: estadoEstufa.luz,
        angulo_persiana: estadoEstufa.servo,
        rele1_exaustor: estadoEstufa.reles[0] === 1,
        rele2_iluminacao: estadoEstufa.reles[1] === 1,
        rele3_irrigacao: estadoEstufa.reles[2] === 1,
        rele4_aquecedor: estadoEstufa.reles[3] === 1,
        alarme_ativo: estadoEstufa.alarme === 1,
        modo_operacao: estadoEstufa.modo,
        lora_rssi: estadoEstufa.rssi,
        lora_snr: estadoEstufa.snr
      })
    });
    if (resp.ok && !silencioso) {
      adicionarLog("sys", "Leitura salva no banco PostgreSQL.");
      carregarHistoricoBanco();
    }
  } catch (_) {}
}

// ================= 6. INICIALIZAÇÃO DE EVENTOS DOM =================
document.addEventListener("DOMContentLoaded", () => {
  inicializarGrafico();
  carregarHistoricoBanco();
  conectarMqtt();

  const slider = document.getElementById("sliderServo");
  const preview = document.getElementById("lblSliderPreview");
  if (slider && preview) {
    slider.addEventListener("input", () => {
      preview.textContent = `${slider.value}°`;
    });
  }

  const btnServo = document.getElementById("btnAplicarServo");
  if (btnServo && slider) {
    btnServo.addEventListener("click", () => {
      enviarPersiana(slider.value);
    });
  }

  const btnPing = document.getElementById("btnPingLoRa");
  if (btnPing) {
    btnPing.addEventListener("click", () => {
      publicarComandoMqtt(TOPIC_CMD_MAIN, { cmd: "PING" });
    });
  }
});
