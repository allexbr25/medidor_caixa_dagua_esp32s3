#pragma once

// ============================================================================
// 1. CREDENCIAIS WI-FI
// ============================================================================
#define WIFI_SSID           "SEU_WIFI_AQUI"
#define WIFI_PASSWORD       "SUA_SENHA_AQUI"
#define WIFI_TIMEOUT_MS     15000

// ============================================================================
// 2. TELEGRAM BOT (Alertas e Relatórios)
// ============================================================================
#define USE_TELEGRAM        true
#define TELEGRAM_BOT_TOKEN  "0000000000:AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA"
#define TELEGRAM_CHAT_ID    "000000000"

// ============================================================================
// 3. THINGSPEAK / DASHBOARD HTTP (Histórico e Gráficos)
// ============================================================================
#define USE_THINGSPEAK      true
#define THINGSPEAK_API_KEY  "SUA_WRITE_API_KEY"

// ============================================================================
// 4. GEOMETRIA DA CAIXA D'ÁGUA (Fortlev 5.000L Polietileno - Tronco de Cone)
// ============================================================================
// Meça a distância real da face do sensor no topo da tampa até o fundo da caixa
#define H_SENSOR_M          1.97f   // Altura do sensor até o fundo (m)
#define H_MAX_UTIL_M        1.45f   // Nível máximo útil (cano de entrada/boia) (m)
#define H_BORDA_M           1.63f   // Altura da borda sem tampa (m)
#define R_BASE_M            0.925f  // Raio da base (diâmetro 1,85 m / 2)
#define R_TOPO_M            1.185f  // Raio sem tampa (diâmetro 2,37 m / 2)

// ============================================================================
// 5. LIMITES DE ALERTA E PROTEÇÃO TÉRMICA
// ============================================================================
#define ALERTA_NIVEL_BAIXO_PCT    25.0f   // Alerta Telegram quando cair abaixo de 25%
#define ALERTA_NIVEL_ALTO_PCT     102.0f  // Alerta de falha de boia / transbordo
#define ALERTA_BAT_BAIXA_V        3.40f   // Alerta de bateria baixa (V)
#define BAT_CRITICA_V             3.20f   // Entra em sono longo para proteger as células

#define TEMP_CORTE_CARGA_C        45.0f   // Corta o carregamento solar acima de 45 °C
#define TEMP_RETOMA_CARGA_C       40.0f   // Retoma o carregamento abaixo de 40 °C
#define TEMP_MIN_CARGA_C          0.0f    // Corta carga abaixo de 0 °C

// ============================================================================
// 6. INTERVALOS DE DEEP SLEEP
// ============================================================================
#define SLEEP_NORMAL_SEC          300     // 5 minutos quando nível estável
#define SLEEP_RAPIDO_SEC          60      // 1 minuto quando enchendo/esvaziando rápido
#define SLEEP_ECONOMIA_SEC        1800    // 30 minutos se bateria < 3.4V
