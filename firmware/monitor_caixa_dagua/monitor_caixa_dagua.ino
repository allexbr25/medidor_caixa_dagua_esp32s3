/**
 * Monitor de Caixa d'Água (5.000L Fortlev) com Energia Solar
 * Integrações: ThingSpeak + Bot Interativo do Telegram + Home Assistant (ZimaOS)
 *
 * Comandos disponíveis no Bot do Telegram:
 *   /nivel ou /status -> Mede na hora e responde com Nível (%), Litros, Distância, Temp e Wi-Fi
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
// CÁLCULO DE VOLUME (Tronco de Cone - Caixa Fortlev 5.000L)
// ============================================================================
float calcularVolumeLitros(float h) {
  if (h <= 0.0f) return 0.0f;
  if (h > H_BORDA_M) h = H_BORDA_M;
  const float k = (R_TOPO_M - R_BASE_M) / H_BORDA_M; // ~0.1595
  const float r = R_BASE_M + k * h;
  return (PI * h / 3.0f) * (R_BASE_M * R_BASE_M + R_BASE_M * r + r * r) * 1000.0f;
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
      // Descarta leituras na zona cega (< 0,22 m) ou maiores que a caixa (> 2,30 m)
      if (d >= 0.22f && d <= 2.30f) {
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

  const float volumeMaxUtil = calcularVolumeLitros(H_MAX_UTIL_M);
  if (m.distanciaM > 0) {
    m.alturaAguaM  = max(0.0f, H_SENSOR_M - m.distanciaM);
    m.volumeLitros = calcularVolumeLitros(m.alturaAguaM);
    m.nivelPct     = (m.volumeLitros / volumeMaxUtil) * 100.0f;
  } else {
    m.alturaAguaM = m.volumeLitros = m.nivelPct = -1.0f;
  }
  return m;
}

String formatarMensagemStatus(const Medicao &m) {
  String msg = "📊 *Status da Caixa d'Água (5.000 L)*\n\n";
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
                            "Sou o monitor da sua *Caixa d'Água Fortlev 5.000 L*.\n\n"
                            "📋 *Comandos disponíveis:*\n"
                            "• `/nivel` ou `/status` — Mede o nível, volume em litros e temperatura agora\n"
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

  // Continua atualizando o ThingSpeak a cada 5 minutos mesmo no modo contínuo
  if (WiFi.status() == WL_CONNECTED && (millis() - ultimoEnvioCloudMs >= (SLEEP_NORMAL_SEC * 1000UL))) {
    ultimoEnvioCloudMs = millis();
    Medicao m = realizarMedicaoCompleta();
    enviarHomeAssistant(m);
    enviarThingSpeak(m);
  }
}
