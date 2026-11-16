import { neon } from '@neondatabase/serverless';

export default async function handler(req, res) {
  // Configuração de CORS para permitir requisições do front-end
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

  if (!databaseUrl) {
    return res.status(500).json({
      error: 'Variável de ambiente DATABASE_URL não configurada na Vercel.'
    });
  }

  const sql = neon(databaseUrl);

  try {
    if (req.method === 'GET') {
      const limit = parseInt(req.query.limit || '50', 10);
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

    if (req.method === 'POST') {
      const {
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
      } = req.body || {};

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
          ${modo_operacao || 'AUTO'},
          ${lora_rssi || null},
          ${lora_snr || null}
        )
        RETURNING id, created_at
      `;

      return res.status(201).json({
        success: true,
        data: inserted[0]
      });
    }

    return res.status(405).json({ error: `Método ${req.method} não suportado.` });
  } catch (error) {
    console.error('Erro ao consultar banco Neon:', error);
    return res.status(500).json({
      error: 'Erro interno ao consultar o banco de dados Neon.',
      details: error.message
    });
  }
}
