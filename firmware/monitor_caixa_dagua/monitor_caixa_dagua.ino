/**
 * Monitor de Caixa d'Água (5.000L Fortlev) com Energia Solar + Home Assistant (ZimaOS)
 *
 * Lista exata de hardware utilizado:
 * - Microcontrolador: ESP32-S3 com antena externa
 * - Sensor de Nível: Ultrassônico à prova d'água JSN-SR04T
 * - Proteção de Sinal: Conversor de nível lógico bidirecional (5V <-> 3.3V)
 * - Gestão de Carga: Módulo carregador TP4056 (entrada solar / bateria)
 * - Regulação de Tensão: Módulo elevador Step-Up MT3608 (ajustado para saída de 5V)
 * - Alimentação: Bateria de lítio (2x 18650 em paralelo) + Painel solar 6V 6W
 * - Proteção Física: Caixa hermética IP65
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

  // Ordena as amostras para extrair a mediana (elimina picos e ruídos)
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

// ============================================================================
// ENVIO DIRETO PARA O HOME ASSISTANT (REST API - HTTP LOCAL OU HTTPS REMOTO)
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
    secureClient.setInsecure(); // Aceita certificado SSL de Cloudflare Tunnel / DuckDNS / Nabu Casa
    http.begin(secureClient, url);
  } else {
    http.begin(url);
  }

  http.addHeader("Authorization", String("Bearer ") + HA_TOKEN);
  http.addHeader("Content-Type", "application/json");

  int code = http.POST(payload);
  http.end();

  if (code == 200 || code == 201) {
    return true;
  } else {
    Serial.printf("[Home Assistant] Erro HTTP %d ao publicar %s\n", code, entityId);
    return false;
  }
}

void enviarHomeAssistant(const Medicao &m) {
  if (!USE_HOME_ASSISTANT || String(HA_TOKEN) == "SEU_LONG_LIVED_ACCESS_TOKEN_AQUI") return;

  Serial.println("[Home Assistant] Enviando leituras...");

  if (m.distanciaM > 0) {
    publicarEntidadeHA("sensor.caixa_dagua_nivel",
                       String(m.nivelPct, 1),
                       "Caixa d'Água - Nível", "%", "", "mdi:storage-tank");

    publicarEntidadeHA("sensor.caixa_dagua_volume",
                       String(m.volumeLitros, 0),
                       "Caixa d'Água - Volume", "L", "volume_storage", "mdi:water");

    publicarEntidadeHA("sensor.caixa_dagua_altura_agua",
                       String(m.alturaAguaM * 100.0f, 1),
                       "Caixa d'Água - Altura da Água", "cm", "distance", "mdi:arrow-expand-vertical");

    publicarEntidadeHA("sensor.caixa_dagua_distancia_sensor",
                       String(m.distanciaM * 100.0f, 1),
                       "Caixa d'Água - Distância Sensor", "cm", "distance", "mdi:ruler");
  } else {
    publicarEntidadeHA("sensor.caixa_dagua_nivel",
                       "unavailable",
                       "Caixa d'Água - Nível", "%", "", "mdi:alert-circle");
  }

  publicarEntidadeHA("sensor.caixa_dagua_temp_esp32",
                     String(m.tempInternaEspC, 1),
                     "Caixa d'Água - Temp. ESP32-S3", "°C", "temperature", "mdi:thermometer");

  publicarEntidadeHA("sensor.caixa_dagua_wifi_rssi",
                     String(m.rssi),
                     "Caixa d'Água - Sinal Wi-Fi", "dBm", "signal_strength", "mdi:wifi");

  Serial.println("[Home Assistant] Leituras atualizadas com sucesso!");
}

// ============================================================================
// ENVIO PARA THINGSPEAK (OPCIONAL)
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
// ALERTAS E STATUS PERIÓDICO VIA TELEGRAM (OPCIONAL)
// ============================================================================
void processarTelegram(const Medicao &m) {
  if (!USE_TELEGRAM || String(TELEGRAM_BOT_TOKEN).startsWith("0000")) return;

  String msg = "";

  if (m.distanciaM < 0) {
    msg += "⚠️ *Falha na leitura do sensor JSN-SR04T* (verifique conexões ou zona cega).\n";
  } else {
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

  bool enviarPeriodico = (bootCount == 1) ||
                         (TELEGRAM_STATUS_A_CADA_N_CICLOS > 0 && (bootCount % TELEGRAM_STATUS_A_CADA_N_CICLOS == 0));

  if (msg.length() > 0 || enviarPeriodico) {
    if (msg.length() == 0) {
      msg = "📊 *Status da Caixa d'Água*\n";
    } else {
      msg += "\n📊 *Leitura atual:*\n";
    }
    msg += "• Nível: *" + String(m.nivelPct, 1) + "%*\n";
    msg += "• Volume estimado: *" + String(m.volumeLitros, 0) + " L*\n";
    msg += "• Altura da água: " + String(m.alturaAguaM * 100.0f, 0) + " cm\n";
    msg += "• Distância sensor: " + String(m.distanciaM * 100.0f, 1) + " cm\n";
    msg += "• Temp. chip ESP32-S3: " + String(m.tempInternaEspC, 1) + " °C\n";
    msg += "• Sinal Wi-Fi: " + String(m.rssi) + " dBm\n";

    WiFiClientSecure client;
    client.setInsecure();
    UniversalTelegramBot bot(TELEGRAM_BOT_TOKEN, client);
    bot.sendMessage(TELEGRAM_CHAT_ID, msg, "Markdown");
  }
}

// ============================================================================
// CICLO PRINCIPAL: ACORDA -> MEDE -> ENVIA -> DEEP SLEEP
// ============================================================================
void setup() {
  Serial.begin(115200);
  delay(200);
  bootCount++;

  pinMode(PIN_JSN_TRIG, OUTPUT);
  pinMode(PIN_JSN_ECHO, INPUT);

  Medicao m = {};
  m.distanciaM      = medirDistanciaMetros();
  m.tempInternaEspC = temperatureRead(); // Sensor de temperatura nativo do ESP32-S3

  const float volumeMaxUtil = calcularVolumeLitros(H_MAX_UTIL_M);
  if (m.distanciaM > 0) {
    m.alturaAguaM  = max(0.0f, H_SENSOR_M - m.distanciaM);
    m.volumeLitros = calcularVolumeLitros(m.alturaAguaM);
    m.nivelPct     = (m.volumeLitros / volumeMaxUtil) * 100.0f;
  } else {
    m.alturaAguaM = m.volumeLitros = m.nivelPct = -1.0f;
  }

  // Intervalo de sono adaptativo: 1 min se estiver enchendo/esvaziando rápido, 5 min se estável
  uint32_t proximoSleepSeg = SLEEP_NORMAL_SEC;
  if (ultimoNivelPct >= 0.0f && m.nivelPct >= 0.0f && fabs(m.nivelPct - ultimoNivelPct) >= 1.5f) {
    proximoSleepSeg = SLEEP_RAPIDO_SEC;
  }
  if (m.nivelPct >= 0.0f) ultimoNivelPct = m.nivelPct;

  Serial.printf("[Ciclo #%lu] Dist: %.2fm | Agua: %.2fm | Vol: %.0fL (%.1f%%) | TempESP: %.1fC\n",
                bootCount, m.distanciaM, m.alturaAguaM, m.volumeLitros, m.nivelPct, m.tempInternaEspC);

  if (String(WIFI_SSID) != "SEU_WIFI_AQUI") {
    WiFi.mode(WIFI_STA);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    unsigned long inicio = millis();
    while (WiFi.status() != WL_CONNECTED && (millis() - inicio) < WIFI_TIMEOUT_MS) {
      delay(100);
    }

    if (WiFi.status() == WL_CONNECTED) {
      m.rssi = WiFi.RSSI();
      enviarHomeAssistant(m);
      enviarThingSpeak(m);
      processarTelegram(m);
      WiFi.disconnect(true);
    } else {
      Serial.println("[WiFi] Timeout ao conectar.");
    }
  }

  Serial.printf("Entrando em Deep Sleep por %lu segundos...\n", proximoSleepSeg);
  esp_sleep_enable_timer_wakeup((uint64_t)proximoSleepSeg * 1000000ULL);
  esp_deep_sleep_start();
}

void loop() {}
