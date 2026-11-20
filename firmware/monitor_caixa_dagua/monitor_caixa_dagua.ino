/**
 * Monitor de Caixa d'Água (Padrão 1.000L Fortlev + Seletor de Modelos/Formatos) com Energia Solar
 * Integrações: ThingSpeak + Bot Interativo do Telegram + Home Assistant (ZimaOS)
 *
 * Comandos disponíveis no Bot do Telegram:
 *   /nivel ou /status -> Mede na hora e responde com Nível (%), Litros, Distância, Temp e Wi-Fi
 *   /modelo <litros>  -> Alterna o modelo da caixa (310, 500, 750, 1000, 1500, 2000, 3000, 5000)
 *   /continuo         -> Mantém o ESP32-S3 online respondendo em tempo real (2s)
 *   /economia         -> Ativa o modo Deep Sleep (responde comandos pendentes a cada ciclo de 5 min)
 *   /ajuda ou /start  -> Exibe a lista de comandos e o seu Chat ID
 */

#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <UniversalTelegramBot.h>

#if __has_include("config.h")
  #include "config.h"
#else
  #include "config.example.h"
#endif

#ifndef FORMATO_CAIXA
  #define FORMATO_CAIXA 1
#endif
#ifndef NOME_CAIXA
  #define NOME_CAIXA "1.000 L"
#endif
#ifndef COMPRIMENTO_M
  #define COMPRIMENTO_M 1.400f
#endif
#ifndef LARGURA_M
  #define LARGURA_M 1.100f
#endif

// ============================================================================
// PINAGEM DO ESP32-S3 (Apenas ESP32-S3 <-> Level Shifter <-> JSN-SR04T)
// ============================================================================
constexpr int PIN_JSN_TRIG = 4;  // ESP32-S3 GPIO4 -> Level Shifter LV1 -> HV1 -> JSN-SR04T TRIG
constexpr int PIN_JSN_ECHO = 5;  // JSN-SR04T ECHO -> Level Shifter HV2 -> LV2 -> ESP32-S3 GPIO5

// ============================================================================
// ESTADO EM MEMÓRIA RTC (persiste durante o Deep Sleep)
// ============================================================================
RTC_DATA_ATTR uint32_t bootCount           = 0;
RTC_DATA_ATTR float    ultimoNivelPct      = -1.0f;
RTC_DATA_ATTR bool     alertaNivelBaixoEnv = false;
RTC_DATA_ATTR bool     alertaNivelAltoEnv  = false;
RTC_DATA_ATTR bool     alertaTempAltaEnv   = false;
RTC_DATA_ATTR bool     modoContinuoAtivo   = TELEGRAM_SEMPRE_ONLINE_PADRAO;
RTC_DATA_ATTR long     lastTelegramUpdate  = 0;

// Geometria ativa persistente em RTC (inicializada com os valores do config.h)
RTC_DATA_ATTR int      formatoAtivo        = FORMATO_CAIXA; // 1=Tronco de Cone, 2=Cilíndrica, 3=Retangular
RTC_DATA_ATTR float    hSensorAtivoM       = H_SENSOR_M;
RTC_DATA_ATTR float    hMaxUtilAtivoM      = H_MAX_UTIL_M;
RTC_DATA_ATTR float    hBordaAtivoM        = H_BORDA_M;
RTC_DATA_ATTR float    rBaseAtivoM         = R_BASE_M;
RTC_DATA_ATTR float    rTopoAtivoM         = R_TOPO_M;
RTC_DATA_ATTR float    compAtivoM          = COMPRIMENTO_M;
RTC_DATA_ATTR float    largAtivoM          = LARGURA_M;
RTC_DATA_ATTR char     nomeCaixaAtiva[24]  = NOME_CAIXA;

WiFiClientSecure telegramClient;
UniversalTelegramBot bot(TELEGRAM_BOT_TOKEN, telegramClient);

unsigned long ultimoPollTelegramMs = 0;
unsigned long ultimoEnvioCloudMs   = 0;

struct Medicao {
  float distanciaM;
  float alturaAguaM;
  float volumeLitros;
  float nivelPct;
  float tempInternaEspC;
  int   rssi;
};

// ============================================================================
// CÁLCULO DE VOLUME (Suporta Tronco de Cone, Cilíndrica e Retangular)
// ============================================================================
float calcularVolumeLitros(float h) {
  if (h <= 0.0f) return 0.0f;
  if (h > hBordaAtivoM) h = hBordaAtivoM;

  if (formatoAtivo == 2) {
    // Cilíndrica Vertical
    return PI * rBaseAtivoM * rBaseAtivoM * h * 1000.0f;
  } else if (formatoAtivo == 3) {
    // Retangular / Quadrada
    return compAtivoM * largAtivoM * h * 1000.0f;
  } else {
    // Tronco de Cone (padrão Fortlev/Tigre)
    const float k = (rTopoAtivoM - rBaseAtivoM) / hBordaAtivoM;
    const float r = rBaseAtivoM + k * h;
    return (PI * h / 3.0f) * (rBaseAtivoM * rBaseAtivoM + rBaseAtivoM * r + r * r) * 1000.0f;
  }
}

bool aplicarPresetPorLitros(int litros) {
  formatoAtivo = 1;
  switch (litros) {
    case 310:
      hSensorAtivoM = 0.690f; hMaxUtilAtivoM = 0.500f; hBordaAtivoM = 0.540f;
      rBaseAtivoM   = 0.375f; rTopoAtivoM    = 0.515f;
      snprintf(nomeCaixaAtiva, sizeof(nomeCaixaAtiva), "310 L");
      return true;
    case 500:
      hSensorAtivoM = 0.720f; hMaxUtilAtivoM = 0.540f; hBordaAtivoM = 0.580f;
      rBaseAtivoM   = 0.475f; rTopoAtivoM    = 0.610f;
      snprintf(nomeCaixaAtiva, sizeof(nomeCaixaAtiva), "500 L");
      return true;
    case 750:
      hSensorAtivoM = 0.860f; hMaxUtilAtivoM = 0.690f; hBordaAtivoM = 0.730f;
      rBaseAtivoM   = 0.500f; rTopoAtivoM    = 0.675f;
      snprintf(nomeCaixaAtiva, sizeof(nomeCaixaAtiva), "750 L");
      return true;
    case 1000:
      hSensorAtivoM = 0.970f; hMaxUtilAtivoM = 0.720f; hBordaAtivoM = 0.760f;
      rBaseAtivoM   = 0.580f; rTopoAtivoM    = 0.755f;
      snprintf(nomeCaixaAtiva, sizeof(nomeCaixaAtiva), "1.000 L");
      return true;
    case 1500:
      hSensorAtivoM = 1.050f; hMaxUtilAtivoM = 0.770f; hBordaAtivoM = 0.830f;
      rBaseAtivoM   = 0.715f; rTopoAtivoM    = 0.875f;
      snprintf(nomeCaixaAtiva, sizeof(nomeCaixaAtiva), "1.500 L");
      return true;
    case 2000:
      hSensorAtivoM = 1.120f; hMaxUtilAtivoM = 0.780f; hBordaAtivoM = 0.900f;
      rBaseAtivoM   = 0.830f; rTopoAtivoM    = 0.995f;
      snprintf(nomeCaixaAtiva, sizeof(nomeCaixaAtiva), "2.000 L");
      return true;
    case 3000:
      hSensorAtivoM = 1.490f; hMaxUtilAtivoM = 1.060f; hBordaAtivoM = 1.210f;
      rBaseAtivoM   = 0.850f; rTopoAtivoM    = 1.065f;
      snprintf(nomeCaixaAtiva, sizeof(nomeCaixaAtiva), "3.000 L");
      return true;
    case 5000:
      hSensorAtivoM = 1.970f; hMaxUtilAtivoM = 1.450f; hBordaAtivoM = 1.630f;
      rBaseAtivoM   = 0.925f; rTopoAtivoM    = 1.185f;
      snprintf(nomeCaixaAtiva, sizeof(nomeCaixaAtiva), "5.000 L");
      return true;
    default:
      return false;
  }
}

// ============================================================================
// LEITURA DO JSN-SR04T COM FILTRO DE MEDIANA (10 AMOSTRAS VÁLIDAS)
// ============================================================================
float medirDistanciaMetros() {
  float leituras[10];
  int validas = 0;

  for (int i = 0; i < 12 && validas < 10; i++) {
    digitalWrite(PIN_JSN_TRIG, LOW);
    delayMicroseconds(5);
    digitalWrite(PIN_JSN_TRIG, HIGH);
    delayMicroseconds(20);
    digitalWrite(PIN_JSN_TRIG, LOW);

    unsigned long duracaoUs = pulseIn(PIN_JSN_ECHO, HIGH, 35000UL);
    if (duracaoUs > 0) {
      float d = (duracaoUs * 0.000343f) / 2.0f;
      // Descarta leituras na zona cega (< 0,22 m) ou maiores que a caixa (> 2,50 m)
      if (d >= 0.22f && d <= 2.50f) {
        leituras[validas++] = d;
      }
    }
    delay(60);
  }

  if (validas == 0) return -1.0f;

  for (int i = 0; i < validas - 1; i++) {
    for (int j = i + 1; j < validas; j++) {
      if (leituras[j] < leituras[i]) {
        float tmp = leituras[i];
        leituras[i] = leituras[j];
        leituras[j] = tmp;
      }
    }
  }
  return leituras[validas / 2];
}

Medicao realizarMedicaoCompleta() {
  Medicao m = {};
  m.distanciaM      = medirDistanciaMetros();
  m.tempInternaEspC = temperatureRead();
  m.rssi            = (WiFi.status() == WL_CONNECTED) ? WiFi.RSSI() : 0;

  const float volumeMaxUtil = max(1.0f, calcularVolumeLitros(hMaxUtilAtivoM));
  if (m.distanciaM > 0) {
    m.alturaAguaM  = max(0.0f, hSensorAtivoM - m.distanciaM);
    m.volumeLitros = calcularVolumeLitros(m.alturaAguaM);
    m.nivelPct     = (m.volumeLitros / volumeMaxUtil) * 100.0f;
  } else {
    m.alturaAguaM = m.volumeLitros = m.nivelPct = -1.0f;
  }
  return m;
}

String formatarMensagemStatus(const Medicao &m) {
  String msg = "📊 *Status da Caixa d'Água (" + String(nomeCaixaAtiva) + ")*\n\n";
  if (m.distanciaM > 0) {
    msg += "💧 *Nível:* " + String(m.nivelPct, 1) + "%\n";
    msg += "🪣 *Volume:* " + String(m.volumeLitros, 0) + " L\n";
    msg += "📏 *Altura da água:* " + String(m.alturaAguaM * 100.0f, 0) + " cm\n";
    msg += "📡 *Distância sensor:* " + String(m.distanciaM * 100.0f, 1) + " cm\n";
  } else {
    msg += "⚠️ *Sensor JSN-SR04T:* Sem leitura válida (sensor desconectado ou na zona cega)\n";
  }
  msg += "🌡️ *Temp. ESP32-S3:* " + String(m.tempInternaEspC, 1) + " °C\n";
  msg += "📶 *Sinal Wi-Fi:* " + String(m.rssi) + " dBm\n";
  msg += "🔋 *Modo atual:* " + String(modoContinuoAtivo ? "Online Contínuo (tempo real)" : "Economia (Deep Sleep)");
  return msg;
}

// ============================================================================
// ENVIO DIRETO PARA O HOME ASSISTANT (REST API - OPCIONAL)
// ============================================================================
bool publicarEntidadeHA(const char* entityId, const String& stateValue,
                        const char* friendlyName, const char* unit,
                        const char* deviceClass, const char* icon) {
  String url = String(HA_BASE_URL) + "/api/states/" + entityId;

  String payload = "{\"state\":\"" + stateValue + "\",\"attributes\":{";
  payload += "\"friendly_name\":\"" + String(friendlyName) + "\",";
  payload += "\"unit_of_measurement\":\"" + String(unit) + "\",";
  payload += "\"state_class\":\"measurement\",";
  if (deviceClass && strlen(deviceClass) > 0) {
    payload += "\"device_class\":\"" + String(deviceClass) + "\",";
  }
  payload += "\"icon\":\"" + String(icon) + "\"}}";

  HTTPClient http;
  WiFiClientSecure secureClient;

  if (url.startsWith("https://")) {
    secureClient.setInsecure();
    http.begin(secureClient, url);
  } else {
    http.begin(url);
  }

  http.addHeader("Authorization", String("Bearer ") + HA_TOKEN);
  http.addHeader("Content-Type", "application/json");

  int code = http.POST(payload);
  http.end();
  return (code == 200 || code == 201);
}

void enviarHomeAssistant(const Medicao &m) {
  if (!USE_HOME_ASSISTANT || String(HA_TOKEN) == "SEU_LONG_LIVED_ACCESS_TOKEN_AQUI") return;

  if (m.distanciaM > 0) {
    publicarEntidadeHA("sensor.caixa_dagua_nivel", String(m.nivelPct, 1), "Caixa d'Água - Nível", "%", "", "mdi:storage-tank");
    publicarEntidadeHA("sensor.caixa_dagua_volume", String(m.volumeLitros, 0), "Caixa d'Água - Volume", "L", "volume_storage", "mdi:water");
    publicarEntidadeHA("sensor.caixa_dagua_altura_agua", String(m.alturaAguaM * 100.0f, 1), "Caixa d'Água - Altura da Água", "cm", "distance", "mdi:arrow-expand-vertical");
    publicarEntidadeHA("sensor.caixa_dagua_distancia_sensor", String(m.distanciaM * 100.0f, 1), "Caixa d'Água - Distância Sensor", "cm", "distance", "mdi:ruler");
  } else {
    publicarEntidadeHA("sensor.caixa_dagua_nivel", "unavailable", "Caixa d'Água - Nível", "%", "", "mdi:alert-circle");
  }
  publicarEntidadeHA("sensor.caixa_dagua_temp_esp32", String(m.tempInternaEspC, 1), "Caixa d'Água - Temp. ESP32-S3", "°C", "temperature", "mdi:thermometer");
  publicarEntidadeHA("sensor.caixa_dagua_wifi_rssi", String(m.rssi), "Caixa d'Água - Sinal Wi-Fi", "dBm", "signal_strength", "mdi:wifi");
}

// ============================================================================
// ENVIO PARA THINGSPEAK
// ============================================================================
void enviarThingSpeak(const Medicao &m) {
  if (!USE_THINGSPEAK || String(THINGSPEAK_API_KEY) == "SUA_WRITE_API_KEY") return;

  HTTPClient http;
  String url = String("http://api.thingspeak.com/update?api_key=") + THINGSPEAK_API_KEY +
               "&field1=" + String(m.nivelPct, 1) +
               "&field2=" + String(m.volumeLitros, 0) +
               "&field3=" + String(m.distanciaM * 100.0f, 1) +
               "&field4=" + String(m.alturaAguaM * 100.0f, 1) +
               "&field5=" + String(m.tempInternaEspC, 1) +
               "&field6=" + String(m.rssi);

  http.begin(url);
  int httpCode = http.GET();
  Serial.printf("[ThingSpeak] HTTP %d\n", httpCode);
  http.end();
}

// ============================================================================
// TELEGRAM: LEITURA DE COMANDOS INTERATIVOS + ALERTAS AUTOMÁTICOS
// ============================================================================
void responderComandosTelegram() {
  if (!USE_TELEGRAM || String(TELEGRAM_BOT_TOKEN).startsWith("0000")) return;

  int numNovas = bot.getUpdates(lastTelegramUpdate + 1);

  while (numNovas > 0) {
    for (int i = 0; i < numNovas; i++) {
      String chatId = String(bot.messages[i].chat_id);
      String texto  = bot.messages[i].text;
      String nome   = bot.messages[i].from_name;
      lastTelegramUpdate = bot.messages[i].update_id;

      int arrobaIdx = texto.indexOf('@');
      if (arrobaIdx > 0) texto = texto.substring(0, arrobaIdx);
      texto.trim();
      texto.toLowerCase();

      Serial.printf("[Telegram] Comando '%s' recebido de %s (chat_id: %s)\n",
                    texto.c_str(), nome.c_str(), chatId.c_str());

      if (texto == "/nivel" || texto == "/status" || texto == "/ler") {
        Medicao mAtual = realizarMedicaoCompleta();
        bot.sendMessage(chatId, formatarMensagemStatus(mAtual), "Markdown");
      }
      else if (texto.startsWith("/modelo") || texto.startsWith("/caixa")) {
        int espacoIdx = texto.indexOf(' ');
        int litrosArg = (espacoIdx > 0) ? texto.substring(espacoIdx + 1).toInt() : 0;
        if (aplicarPresetPorLitros(litrosArg)) {
          Medicao mAtual = realizarMedicaoCompleta();
          enviarThingSpeak(mAtual);
          enviarHomeAssistant(mAtual);
          bot.sendMessage(chatId,
            "✅ *Modelo da Caixa alterado para Fortlev " + String(nomeCaixaAtiva) + " (Tronco de Cone)!*\n\n" +
            formatarMensagemStatus(mAtual),
            "Markdown");
        } else {
          bot.sendMessage(chatId,
            "🪣 *Modelo atual:* `" + String(nomeCaixaAtiva) + "`\n\n"
            "Para alterar o tamanho pelo Telegram, envie:\n"
            "• `/modelo 310`\n"
            "• `/modelo 500`\n"
            "• `/modelo 750`\n"
            "• `/modelo 1000` *(Padrão)*\n"
            "• `/modelo 1500`\n"
            "• `/modelo 2000`\n"
            "• `/modelo 3000`\n"
            "• `/modelo 5000`",
            "Markdown");
        }
      }
      else if (texto == "/continuo") {
        modoContinuoAtivo = true;
        bot.sendMessage(chatId,
          "⚡ *Modo Online Contínuo ativado!*\n"
          "O ESP32-S3 permanecerá acordado respondendo aos comandos `/nivel` em tempo real (a cada 2s).\n"
          "Envie `/economia` quando quiser ativar o Deep Sleep.",
          "Markdown");
      }
      else if (texto == "/economia") {
        modoContinuoAtivo = false;
        bot.sendMessage(chatId,
          "🌙 *Modo Economia (Deep Sleep) ativado!*\n"
          "O ESP32-S3 vai dormir entre as leituras (1 a 5 min) e responderá aos seus comandos assim que acordar no próximo ciclo.",
          "Markdown");
      }
      else {
        String boasVindas = "👋 Olá, *" + nome + "*!\n"
                            "Sou o monitor da sua *Caixa d'Água Fortlev " + String(nomeCaixaAtiva) + "*.\n\n"
                            "📋 *Comandos disponíveis:*\n"
                            "• `/nivel` ou `/status` — Mede o nível, volume em litros e temperatura agora\n"
                            "• `/modelo 1000` — Alterna o tamanho da caixa (310, 500, 750, 1000, 1500, 2000, 3000, 5000)\n"
                            "• `/continuo` — Fica online respondendo na hora (sem dormir)\n"
                            "• `/economia` — Ativa o modo Deep Sleep (economia solar)\n"
                            "• `/ajuda` — Mostra esta mensagem\n\n"
                            "🆔 *Seu Chat ID:* `" + chatId + "`";
        bot.sendMessage(chatId, boasVindas, "Markdown");
      }
    }
    numNovas = bot.getUpdates(lastTelegramUpdate + 1);
  }
}

void processarTelegram(const Medicao &m) {
  if (!USE_TELEGRAM || String(TELEGRAM_BOT_TOKEN).startsWith("0000")) return;

  // 1. Verifica e responde comandos enviados pelo usuário (/nivel, /status, /continuo, etc.)
  responderComandosTelegram();

  // 2. Verifica alertas automáticos e mensagem de boot (se TELEGRAM_CHAT_ID estiver configurado)
  if (String(TELEGRAM_CHAT_ID) == "000000000" || String(TELEGRAM_CHAT_ID).length() == 0) return;

  String msg = "";

  if (bootCount == 1) {
    msg += "🟢 *ESP32-S3 Online e Conectado ao Wi-Fi!*\n"
           "Envie `/nivel` para consultar agora ou `/ajuda` para ver os comandos.\n";
  }

  if (m.distanciaM > 0) {
    if (m.nivelPct <= ALERTA_NIVEL_BAIXO_PCT && !alertaNivelBaixoEnv) {
      msg += "🚰 *ALERTA DE NÍVEL BAIXO:* " + String(m.nivelPct, 1) + "% (" + String(m.volumeLitros, 0) + " L)\n";
      alertaNivelBaixoEnv = true;
    } else if (m.nivelPct > ALERTA_NIVEL_BAIXO_PCT + 5.0f) {
      alertaNivelBaixoEnv = false;
    }

    if (m.nivelPct >= ALERTA_NIVEL_ALTO_PCT && !alertaNivelAltoEnv) {
      msg += "🌊 *ALERTA DE NÍVEL ACIMA DO MÁXIMO (verifique a boia):* " +
             String(m.nivelPct, 1) + "% (" + String(m.volumeLitros, 0) + " L)\n";
      alertaNivelAltoEnv = true;
    } else if (m.nivelPct < 98.0f) {
      alertaNivelAltoEnv = false;
    }
  }

  if (m.tempInternaEspC >= ALERTA_TEMP_CHIP_C && !alertaTempAltaEnv) {
    msg += "🌡️ *Temperatura interna do ESP32-S3 elevada:* " + String(m.tempInternaEspC, 1) + " °C\n";
    alertaTempAltaEnv = true;
  } else if (m.tempInternaEspC <= ALERTA_TEMP_CHIP_C - 5.0f) {
    alertaTempAltaEnv = false;
  }

  bool enviarPeriodico = (TELEGRAM_STATUS_A_CADA_N_CICLOS > 0 && (bootCount % TELEGRAM_STATUS_A_CADA_N_CICLOS == 0));

  if (msg.length() > 0 || enviarPeriodico) {
    if (msg.length() > 0) {
      msg += "\n" + formatarMensagemStatus(m);
    } else {
      msg = formatarMensagemStatus(m);
    }
    bot.sendMessage(TELEGRAM_CHAT_ID, msg, "Markdown");
  }
}

void entrarEmDeepSleep(const Medicao &m) {
  uint32_t proximoSleepSeg = SLEEP_NORMAL_SEC;
  if (ultimoNivelPct >= 0.0f && m.nivelPct >= 0.0f && fabs(m.nivelPct - ultimoNivelPct) >= 1.5f) {
    proximoSleepSeg = SLEEP_RAPIDO_SEC;
  }
  if (m.nivelPct >= 0.0f) ultimoNivelPct = m.nivelPct;

  WiFi.disconnect(true);
  Serial.printf("Entrando em Deep Sleep por %lu segundos...\n", proximoSleepSeg);
  esp_sleep_enable_timer_wakeup((uint64_t)proximoSleepSeg * 1000000ULL);
  esp_deep_sleep_start();
}

// ============================================================================
// SETUP & LOOP
// ============================================================================
void setup() {
  Serial.begin(115200);
  delay(200);
  bootCount++;

  pinMode(PIN_JSN_TRIG, OUTPUT);
  pinMode(PIN_JSN_ECHO, INPUT);
  telegramClient.setInsecure();

  if (String(WIFI_SSID) != "SEU_WIFI_AQUI") {
    WiFi.mode(WIFI_STA);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    unsigned long inicio = millis();
    while (WiFi.status() != WL_CONNECTED && (millis() - inicio) < WIFI_TIMEOUT_MS) {
      delay(100);
    }
  }

  Medicao m = realizarMedicaoCompleta();
  Serial.printf("[Ciclo #%lu] Dist: %.2fm | Agua: %.2fm | Vol: %.0fL (%.1f%%) | TempESP: %.1fC\n",
                bootCount, m.distanciaM, m.alturaAguaM, m.volumeLitros, m.nivelPct, m.tempInternaEspC);

  if (WiFi.status() == WL_CONNECTED) {
    enviarHomeAssistant(m);
    enviarThingSpeak(m);
    processarTelegram(m);
    ultimoEnvioCloudMs = millis();
  } else {
    Serial.println("[WiFi] Sem conexão Wi-Fi.");
  }

  if (modoContinuoAtivo && WiFi.status() == WL_CONNECTED) {
    Serial.println("[Telegram] Modo Online Contínuo ativo (escutando comandos a cada 2s)...");
  } else {
    entrarEmDeepSleep(m);
  }
}

void loop() {
  if (!modoContinuoAtivo) {
    Medicao m = realizarMedicaoCompleta();
    entrarEmDeepSleep(m);
  }

  if (WiFi.status() == WL_CONNECTED && (millis() - ultimoPollTelegramMs >= 2000)) {
    ultimoPollTelegramMs = millis();
    responderComandosTelegram();
  }

  // No modo online contínuo, envia batimento (heartbeat) ao ThingSpeak a cada 20 segundos
  // para o painel web detectar online/offline em tempo real
  if (WiFi.status() == WL_CONNECTED && (millis() - ultimoEnvioCloudMs >= 20000UL)) {
    ultimoEnvioCloudMs = millis();
    Medicao m = realizarMedicaoCompleta();
    enviarHomeAssistant(m);
    enviarThingSpeak(m);
  }
}
