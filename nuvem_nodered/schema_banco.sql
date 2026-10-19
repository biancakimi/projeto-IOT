-- ======================================================================================
-- PROJETO FINAL IoT (UNIFESP) — SCHEMA DO BANCO DE DADOS HISTÓRICO (schema_banco.sql)
-- ======================================================================================
-- Descrição:
--   Estrutura da tabela de séries temporais para armazenar o histórico de leituras
--   da Estufa Inteligente (Desafio Extra: Implementação de Banco de Dados para Histórico).
--   Compatível com PostgreSQL (Supabase) e SQLite.
-- ======================================================================================

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
);

-- Índice para acelerar consultas de gráficos por janela de tempo no site/Node-RED
CREATE INDEX IF NOT EXISTS idx_historico_estufa_created_at ON historico_estufa (created_at DESC);
