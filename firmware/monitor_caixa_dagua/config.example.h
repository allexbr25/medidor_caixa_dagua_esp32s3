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
#define TELEGRAM_SEMPRE_ONLINE_PADRAO    true

// ============================================================================
// 4. THINGSPEAK (Histórico e Gráficos Web)
// ============================================================================
#define USE_THINGSPEAK      true
#define THINGSPEAK_API_KEY  "SUA_WRITE_API_KEY"

// ============================================================================
// 5. GEOMETRIA DA CAIXA D'ÁGUA (Padrão: Fortlev 1.000L - Tronco de Cone)
// ============================================================================
// FORMATO_CAIXA: 1 = Tronco de Cone (Redonda Fortlev/Tigre)
//                2 = Cilíndrica Vertical (Tanque reto)
//                3 = Retangular / Quadrada (Alvenaria / Cisterna)
#define FORMATO_CAIXA       1
#define NOME_CAIXA          "1.000 L"

// Medidas padrão configuradas para Caixa Fortlev 1.000 L (Polietileno):
// (Para outros tamanhos como 310L, 500L, 750L, 1500L, 2000L, 3000L ou 5000L,
//  use o botão "Código ESP32 (config.h)" no painel web ou o comando /modelo no Telegram)
#define H_SENSOR_M          0.970f  // Altura do sensor até o fundo (97 cm)
#define H_MAX_UTIL_M        0.720f  // Nível máximo útil / boia (72 cm = ~1.000 L)
#define H_BORDA_M           0.760f  // Altura da borda sem tampa (76 cm)
#define R_BASE_M            0.580f  // Raio da base (diâmetro 1,16 m / 2)
#define R_TOPO_M            0.755f  // Raio do topo sem tampa (diâmetro 1,51 m / 2)
#define COMPRIMENTO_M       1.400f  // Comprimento interno (usado se FORMATO_CAIXA == 3)
#define LARGURA_M           1.100f  // Largura interna (usado se FORMATO_CAIXA == 3)

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
