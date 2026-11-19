#pragma once

// ============================================================================
// 1. CREDENCIAIS WI-FI
// ============================================================================
#define WIFI_SSID           "SEU_WIFI_AQUI"
#define WIFI_PASSWORD       "SUA_SENHA_AQUI"
#define WIFI_TIMEOUT_MS     15000

// ============================================================================
// 2. HOME ASSISTANT (ZimaOS - Acesso Local HTTP ou Remoto HTTPS)
// ============================================================================
#define USE_HOME_ASSISTANT  true
// Exemplo remoto (do trabalho): "https://seu-ha.seudominio.com"
// Exemplo local (em casa):      "http://192.168.1.100:8123"
// (Não coloque barra '/' no final da URL)
#define HA_BASE_URL         "http://192.168.1.100:8123"
// Token gerado no Home Assistant em: Perfil -> Segurança -> Tokens de Acesso de Longa Duração
#define HA_TOKEN            "SEU_LONG_LIVED_ACCESS_TOKEN_AQUI"

// ============================================================================
// 3. TELEGRAM BOT (Opcional - Alertas Diretos no Celular)
// ============================================================================
#define USE_TELEGRAM        false
#define TELEGRAM_BOT_TOKEN  "0000000000:AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA"
#define TELEGRAM_CHAT_ID    "000000000"
#define TELEGRAM_STATUS_A_CADA_N_CICLOS  12

// ============================================================================
// 4. THINGSPEAK (Opcional - Desativado por padrão pois o Home Assistant já faz)
// ============================================================================
#define USE_THINGSPEAK      false
#define THINGSPEAK_API_KEY  "SUA_WRITE_API_KEY"

// ============================================================================
// 5. GEOMETRIA DA CAIXA D'ÁGUA (Fortlev 5.000L Polietileno - Tronco de Cone)
// ============================================================================
// Meça a distância real da face do sensor no topo da tampa até o fundo da caixa
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
