#pragma once

// ============================================================================
// 1. CREDENCIAIS WI-FI
// ============================================================================
#define WIFI_SSID           "SEU_WIFI_AQUI"
#define WIFI_PASSWORD       "SUA_SENHA_AQUI"
#define WIFI_TIMEOUT_MS     15000

// ============================================================================
// 2. HOME ASSISTANT (ZimaOS - Opcional)
// ============================================================================
#define USE_HOME_ASSISTANT  false
#define HA_BASE_URL         "http://192.168.1.100:8123"
#define HA_TOKEN            "SEU_LONG_LIVED_ACCESS_TOKEN_AQUI"

// ============================================================================
// 3. TELEGRAM BOT INTERATIVO (Comandos /nivel, /status, /continuo, /economia)
// ============================================================================
#define USE_TELEGRAM        true
#define TELEGRAM_BOT_TOKEN  "0000000000:AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA"
// Seu Chat ID (opcional para comandos: o bot responde a quem mandar /nivel,
// mas obrigatório para receber alertas automáticos de nível baixo/alto)
#define TELEGRAM_CHAT_ID    "000000000"

// Envia um status periódico automático a cada N ciclos (0 = apenas alertas e respostas a comandos)
#define TELEGRAM_STATUS_A_CADA_N_CICLOS  0

// Se true, o ESP32 não entra em Deep Sleep e responde aos comandos do Telegram em 2 segundos.
// Você também pode ativar/desativar isso pelo celular enviando /continuo ou /economia no Telegram!
#define TELEGRAM_SEMPRE_ONLINE_PADRAO    false

// ============================================================================
// 4. THINGSPEAK (Histórico e Gráficos Web)
// ============================================================================
#define USE_THINGSPEAK      true
#define THINGSPEAK_API_KEY  "SUA_WRITE_API_KEY"

// ============================================================================
// 5. GEOMETRIA DA CAIXA D'ÁGUA (Fortlev 5.000L Polietileno - Tronco de Cone)
// ============================================================================
#define H_SENSOR_M          1.97f   // Altura do sensor até o fundo (m)
#define H_MAX_UTIL_M        1.45f   // Nível máximo útil (cano de entrada/boia) (m)
#define H_BORDA_M           1.63f   // Altura da borda sem tampa (m)
#define R_BASE_M            0.925f  // Raio da base (diâmetro 1,85 m / 2)
#define R_TOPO_M            1.185f  // Raio sem tampa (diâmetro 2,37 m / 2)

// ============================================================================
// 6. LIMITES DE ALERTA
// ============================================================================
#define ALERTA_NIVEL_BAIXO_PCT    25.0f   // Alerta quando cair abaixo de 25%
#define ALERTA_NIVEL_ALTO_PCT     102.0f  // Alerta de falha de boia / transbordo
#define ALERTA_TEMP_CHIP_C        65.0f   // Alerta usando o sensor interno do próprio ESP32-S3

// ============================================================================
// 7. INTERVALOS DE DEEP SLEEP
// ============================================================================
#define SLEEP_NORMAL_SEC          300     // 5 minutos quando nível estável
#define SLEEP_RAPIDO_SEC          60      // 1 minuto quando enchendo/esvaziando rápido
