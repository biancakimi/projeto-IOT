import { neon } from '@neondatabase/serverless';

// Cache em memória da instância Serverless (fallback e fila rápida de comandos)
let memoriaHistorico = [];
let filaComandosMemoria = [];
let tabelasVerificadas = false;

export default async function handler(req, res) {
  // Configuração de CORS para permitir requisições do front-end e do ESP32 Gateway
  res.setHeader('Access-Control-Allow-Credentials', 'true');
  res.setHeader('Access-Control-Allow-Origin', '*');
  res.setHeader('Access-Control-Allow-Methods', 'GET,OPTIONS,POST');
  res.setHeader(
    'Access-Control-Allow-Headers',
    'X-CSRF-Token, X-Requested-With, Accept, Accept-Version, Content-Length, Content-MD5, Content-Type, Date, X-Api-Version'
  );

  if (req.method === 'OPTIONS') {
    return res.status(200).end();
  }

  const databaseUrl = process.env.DATABASE_URL;
  const sql = databaseUrl ? neon(databaseUrl) : null;

  try {
    if (sql && !tabelasVerificadas) {
      await sql`
        CREATE TABLE IF NOT EXISTS historico_estufa (
          id BIGSERIAL PRIMARY KEY,
          created_at TIMESTAMPTZ DEFAULT CURRENT_TIMESTAMP,
          temperatura REAL NOT NULL,
          umidade REAL NOT NULL,
          luminosidade INTEGER NOT NULL,
          angulo_persiana INTEGER NOT NULL,
          rele1_exaustor BOOLEAN NOT NULL,
          rele2_iluminacao BOOLEAN NOT NULL,
          rele3_irrigacao BOOLEAN NOT NULL,
          rele4_aquecedor BOOLEAN NOT NULL,
          alarme_ativo BOOLEAN NOT NULL,
          modo_operacao VARCHAR(10) NOT NULL,
          lora_rssi INTEGER,
          lora_snr REAL
        )
      `;
      await sql`
        CREATE TABLE IF NOT EXISTS comandos_estufa (
          id BIGSERIAL PRIMARY KEY,
          created_at TIMESTAMPTZ DEFAULT CURRENT_TIMESTAMP,
          payload JSONB NOT NULL,
          entregue BOOLEAN DEFAULT FALSE
        )
      `;
      tabelasVerificadas = true;
    }

    // 1. GET /api/historico -> Retorna últimas leituras para o Site na Vercel
    if (req.method === 'GET') {
      const limit = Math.min(200, Math.max(1, parseInt(req.query.limit || '50', 10)));

      // Se o ESP 2 Gateway consultar apenas comandos pendentes (?tipo=comando)
      if (req.query.tipo === 'comando') {
        let cmdObj = null;
        if (sql) {
          const rows = await sql`
            UPDATE comandos_estufa
            SET entregue = TRUE
            WHERE id = (
              SELECT id FROM comandos_estufa
              WHERE entregue = FALSE
              ORDER BY created_at ASC
              LIMIT 1
            )
            RETURNING payload
          `;
          if (rows.length > 0) cmdObj = rows[0].payload;
        }
        if (!cmdObj && filaComandosMemoria.length > 0) {
          cmdObj = filaComandosMemoria.shift();
        }
        return res.status(200).json({ cmd: cmdObj });
      }

      if (sql) {
        const rows = await sql`
          SELECT 
            id,
            created_at,
            temperatura,
            umidade,
            luminosidade,
            angulo_persiana,
            rele1_exaustor,
            rele2_iluminacao,
            rele3_irrigacao,
            rele4_aquecedor,
            alarme_ativo,
            modo_operacao,
            lora_rssi,
            lora_snr
          FROM historico_estufa
          ORDER BY created_at DESC
          LIMIT ${limit}
        `;
        return res.status(200).json(rows);
      }

      return res.status(200).json(memoriaHistorico.slice(0, limit));
    }

    // 2. POST /api/historico -> Recebe Telemetria (do ESP 2 Gateway ou Site) OU Ordem de Comando (do Site)
    if (req.method === 'POST') {
      const body = typeof req.body === 'string' ? JSON.parse(req.body || '{}') : (req.body || {});

      // 2a. Se for uma ordem de comando enviada pelo Site Web para o ESP 2 -> ESP 1
      if (req.query.tipo === 'comando' || (body.cmd && typeof body.temperatura === 'undefined' && typeof body.t === 'undefined')) {
        filaComandosMemoria.push(body);
        if (filaComandosMemoria.length > 20) filaComandosMemoria.shift();

        if (sql) {
          await sql`
            INSERT INTO comandos_estufa (payload, entregue)
            VALUES (${JSON.stringify(body)}::jsonb, FALSE)
          `;
        }
        return res.status(201).json({ success: true, queued: body });
      }

      // 2b. Aceita tanto o formato expandido quanto o JSON compacto do ESP 2 Gateway
      const rArr = Array.isArray(body.r) ? body.r : [0, 0, 0, 0];
      const temperatura     = Number(body.temperatura ?? body.t ?? 25.0);
      const umidade         = Number(body.umidade ?? body.h ?? 60.0);
      const luminosidade    = parseInt(body.luminosidade ?? body.l ?? 50, 10);
      const angulo_persiana = parseInt(body.angulo_persiana ?? body.srv ?? 90, 10);
      const rele1_exaustor  = Boolean(body.rele1_exaustor ?? (Number(rArr[0]) === 1));
      const rele2_iluminacao= Boolean(body.rele2_iluminacao ?? (Number(rArr[1]) === 1));
      const rele3_irrigacao = Boolean(body.rele3_irrigacao ?? (Number(rArr[2]) === 1));
      const rele4_aquecedor = Boolean(body.rele4_aquecedor ?? (Number(rArr[3]) === 1));
      const alarme_ativo    = Boolean(body.alarme_ativo ?? (Number(body.al) === 1));
      const modo_operacao   = String(body.modo_operacao ?? body.m ?? 'AUTO').substring(0, 10);
      const lora_rssi       = body.lora_rssi ?? body.rssi ?? null;
      const lora_snr        = body.lora_snr ?? body.snr ?? null;

      const regMemoria = {
        id: Date.now(),
        created_at: new Date().toISOString(),
        temperatura,
        umidade,
        luminosidade,
        angulo_persiana,
        rele1_exaustor,
        rele2_iluminacao,
        rele3_irrigacao,
        rele4_aquecedor,
        alarme_ativo,
        modo_operacao,
        lora_rssi,
        lora_snr
      };
      memoriaHistorico.unshift(regMemoria);
      if (memoriaHistorico.length > 100) memoriaHistorico.pop();

      let insertedRow = regMemoria;
      let pendingCmd = null;

      if (sql) {
        const inserted = await sql`
          INSERT INTO historico_estufa (
            temperatura,
            umidade,
            luminosidade,
            angulo_persiana,
            rele1_exaustor,
            rele2_iluminacao,
            rele3_irrigacao,
            rele4_aquecedor,
            alarme_ativo,
            modo_operacao,
            lora_rssi,
            lora_snr
          ) VALUES (
            ${temperatura},
            ${umidade},
            ${luminosidade},
            ${angulo_persiana},
            ${rele1_exaustor},
            ${rele2_iluminacao},
            ${rele3_irrigacao},
            ${rele4_aquecedor},
            ${alarme_ativo},
            ${modo_operacao},
            ${lora_rssi},
            ${lora_snr}
          )
          RETURNING id, created_at
        `;
        insertedRow = inserted[0];

        // Verifica se há ordem pendente do site para já devolver na resposta HTTP ao ESP 2 Gateway
        const cmdRows = await sql`
          UPDATE comandos_estufa
          SET entregue = TRUE
          WHERE id = (
            SELECT id FROM comandos_estufa
            WHERE entregue = FALSE
            ORDER BY created_at ASC
            LIMIT 1
          )
          RETURNING payload
        `;
        if (cmdRows.length > 0) {
          pendingCmd = cmdRows[0].payload;
        }
      }

      if (!pendingCmd && filaComandosMemoria.length > 0) {
        pendingCmd = filaComandosMemoria.shift();
      }

      return res.status(201).json({
        success: true,
        data: insertedRow,
        cmd: pendingCmd
      });
    }

    return res.status(405).json({ error: `Método ${req.method} não suportado.` });
  } catch (error) {
    console.error('Erro na API /api/historico:', error);
    return res.status(500).json({
      error: 'Erro interno na API de histórico.',
      details: error.message
    });
  }
}
