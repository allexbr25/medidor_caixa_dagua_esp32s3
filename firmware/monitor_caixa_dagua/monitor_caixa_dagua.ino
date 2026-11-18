/**
 * Monitor Inteligente de Caixa d'Água (5.000L Fortlev) com Energia Solar
 * Microcontrolador: ESP32-S3 (com antena externa)
 * Sensor de Nível: Ultrassônico à prova d'água JSN-SR04T + Level Shifter 5V <-> 3.3V
 * Telemetria Solar/Bateria: 2x INA219 (0x40 Solar, 0x41 Bateria 2x18650 EVE 6400mAh)
 * Proteção Térmica: DS18B20 no pack externo de baterias + MOSFET de corte a 45 °C
 */

#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <Wire.h>
#include <Adafruit_INA219.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include <UniversalTelegramBot.h>
#include "driver/rtc_io.h"

#if __has_include("config.h")
  #include "config.h"
#else
  #include "config.example.h"
#endif

// ============================================================================
// PINAGEM DO ESP32-S3
// ============================================================================
constexpr int PIN_ADC_BAT       = 1;   // Fallback: Divisor 100k/100k caso não use INA219 #2
constexpr int PIN_JSN_TRIG      = 4;   // Via Level Shifter (LV1 -> HV1)
constexpr int PIN_JSN_ECHO      = 5;   // Via Level Shifter (HV2 -> LV2)
constexpr int PIN_JSN_PWR       = 6;   // Liga/desliga alimentação do JSN-SR04T via MOSFET
constexpr int PIN_I2C_SDA       = 8;   // Barramento I2C (INA219 Solar + INA219 Bateria)
constexpr int PIN_I2C_SCL       = 9;
constexpr int PIN_DS18B20       = 10;  // Sensor de temperatura colado no pack 2x18650
constexpr int PIN_CUTOFF_SOLAR  = 11;  // HIGH = corta o painel solar (proteção > 45 °C)

// ============================================================================
// OBJETOS E ESTADO EM MEMÓRIA RTC (persiste durante o Deep Sleep)
// ============================================================================
Adafruit_INA219 inaSolar(0x40);
Adafruit_INA219 inaBateria(0x41);
OneWire oneWire(PIN_DS18B20);
DallasTemperature sensorTemp(&oneWire);

RTC_DATA_ATTR uint32_t bootCount          = 0;
RTC_DATA_ATTR float    ultimoNivelPct     = -1.0f;
RTC_DATA_ATTR bool     cargaCortadaTemp   = false;
RTC_DATA_ATTR bool     alertaNivelBaixoEnv = false;
RTC_DATA_ATTR bool     alertaNivelAltoEnv  = false;
RTC_DATA_ATTR bool     alertaBatBaixaEnv   = false;
RTC_DATA_ATTR bool     alertaTempAltaEnv   = false;
RTC_DATA_ATTR float    energiaSolarWhDia   = 0.0f;

struct Telemetria {
  float distanciaM;
  float alturaAguaM;
  float volumeLitros;
  float nivelPct;
  float batTensaoV;
  float batCorrenteMA;
  float batPct;
  float solarTensaoV;
  float solarCorrenteMA;
  float solarPotenciaMW;
  float tempBateriaC;
  bool  inaSolarOk;
  bool  inaBatOk;
  bool  tempOk;
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

float estimarPorcentagemBateria(float v) {
  if (v >= 4.15f) return 100.0f;
  if (v <= 3.20f) return 0.0f;
  return constrain((v - 3.20f) / (4.15f - 3.20f) * 100.0f, 0.0f, 100.0f);
}

// ============================================================================
// LEITURA DO JSN-SR04T COM FILTRO DE MEDIANA (10 AMOSTRAS)
// ============================================================================
float medirDistanciaMetros() {
  digitalWrite(PIN_JSN_PWR, HIGH);
  delay(300); // Estabilização do módulo JSN-SR04T

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
      // Descarta leituras dentro da zona cega (< 0,22 m) ou maiores que a caixa (> 2,30 m)
      if (d >= 0.22f && d <= 2.30f) {
        leituras[validas++] = d;
      }
    }
    delay(60);
  }

  digitalWrite(PIN_JSN_PWR, LOW);

  if (validas == 0) return -1.0f;

  // Ordena para pegar a mediana (elimina ruídos e respingos)
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
// LEITURA DE ENERGIA E PROTEÇÃO TÉRMICA DO PACK DE BATERIAS
// ============================================================================
void lerEnergiaETemperatura(Telemetria &t, uint32_t intervaloSeg) {
  Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL);

  t.inaSolarOk = inaSolar.begin();
  if (t.inaSolarOk) {
    t.solarTensaoV    = inaSolar.getBusVoltage_V();
    t.solarCorrenteMA = inaSolar.getCurrent_mA();
    t.solarPotenciaMW = inaSolar.getPower_mW();
    if (t.solarPotenciaMW > 0) {
      energiaSolarWhDia += (t.solarPotenciaMW / 1000.0f) * (intervaloSeg / 3600.0f);
    }
  } else {
    t.solarTensaoV = t.solarCorrenteMA = t.solarPotenciaMW = 0.0f;
  }

  t.inaBatOk = inaBateria.begin();
  if (t.inaBatOk) {
    t.batTensaoV    = inaBateria.getBusVoltage_V();
    t.batCorrenteMA = inaBateria.getCurrent_mA();
  } else {
    // Fallback pelo divisor resistivo 100k/100k no GPIO1 (ADC1)
    uint32_t mv = analogReadMilliVolts(PIN_ADC_BAT);
    t.batTensaoV    = (mv * 2.0f) / 1000.0f;
    t.batCorrenteMA = 0.0f;
  }
  t.batPct = estimarPorcentagemBateria(t.batTensaoV);

  // Temperatura do pack externo 2x18650 (DS18B20)
  sensorTemp.begin();
  sensorTemp.requestTemperatures();
  float tempC = sensorTemp.getTempCByIndex(0);
  if (tempC != DEVICE_DISCONNECTED_C && tempC > -40.0f && tempC < 125.0f) {
    t.tempOk = true;
    t.tempBateriaC = tempC;

    // Lógica de corte térmico com histerese (45 °C corta, 40 °C retoma)
    if (tempC >= TEMP_CORTE_CARGA_C || tempC <= TEMP_MIN_CARGA_C) {
      cargaCortadaTemp = true;
    } else if (cargaCortadaTemp && tempC <= TEMP_RETOMA_CARGA_C && tempC > TEMP_MIN_CARGA_C) {
      cargaCortadaTemp = false;
    }
  } else {
    t.tempOk = false;
    t.tempBateriaC = NAN;
    cargaCortadaTemp = false; // Fail-safe: mantém o carregamento habilitado se o sensor não estiver conectado
  }

  // Aplica e trava o pino de corte durante o Deep Sleep
  gpio_hold_dis((gpio_num_t)PIN_CUTOFF_SOLAR);
  digitalWrite(PIN_CUTOFF_SOLAR, cargaCortadaTemp ? HIGH : LOW);
  gpio_hold_en((gpio_num_t)PIN_CUTOFF_SOLAR);
}

// ============================================================================
// ENVIO PARA THINGSPEAK E ALERTAS TELEGRAM
// ============================================================================
void enviarThingSpeak(const Telemetria &t) {
  if (!USE_THINGSPEAK || String(THINGSPEAK_API_KEY) == "SUA_WRITE_API_KEY") return;

  HTTPClient http;
  String url = String("http://api.thingspeak.com/update?api_key=") + THINGSPEAK_API_KEY +
               "&field1=" + String(t.nivelPct, 1) +
               "&field2=" + String(t.volumeLitros, 0) +
               "&field3=" + String(t.distanciaM * 100.0f, 1) +
               "&field4=" + String(t.batTensaoV, 2) +
               "&field5=" + String(t.batCorrenteMA, 1) +
               "&field6=" + String(t.solarPotenciaMW / 1000.0f, 2) +
               "&field7=" + String(energiaSolarWhDia, 2) +
               "&field8=" + String(t.tempOk ? t.tempBateriaC : 0.0f, 1);

  http.begin(url);
  int httpCode = http.GET();
  Serial.printf("[ThingSpeak] HTTP %d\n", httpCode);
  http.end();
}

void verificarEEnviarAlertasTelegram(const Telemetria &t) {
  if (!USE_TELEGRAM || String(TELEGRAM_BOT_TOKEN).startsWith("0000")) return;

  String msg = "";

  if (t.distanciaM < 0) {
    msg += "⚠️ *Falha na leitura do sensor JSN-SR04T* (verifique o cabo ou obstrução).\n";
  } else {
    if (t.nivelPct <= ALERTA_NIVEL_BAIXO_PCT && !alertaNivelBaixoEnv) {
      msg += "🚰 *ALERTA NÍVEL BAIXO:* " + String(t.nivelPct, 1) + "% (" + String(t.volumeLitros, 0) + " L)\n";
      alertaNivelBaixoEnv = true;
    } else if (t.nivelPct > ALERTA_NIVEL_BAIXO_PCT + 5.0f) {
      alertaNivelBaixoEnv = false;
    }

    if (t.nivelPct >= ALERTA_NIVEL_ALTO_PCT && !alertaNivelAltoEnv) {
      msg += "🌊 *ALERTA NÍVEL ACIMA DO MÁXIMO (possível falha na boia):* " +
             String(t.nivelPct, 1) + "% (" + String(t.volumeLitros, 0) + " L)\n";
      alertaNivelAltoEnv = true;
    } else if (t.nivelPct < 98.0f) {
      alertaNivelAltoEnv = false;
    }
  }

  if (t.batTensaoV <= ALERTA_BAT_BAIXA_V && !alertaBatBaixaEnv) {
    msg += "🪫 *Bateria baixa:* " + String(t.batTensaoV, 2) + " V (" + String(t.batPct, 0) + "%)\n";
    alertaBatBaixaEnv = true;
  } else if (t.batTensaoV >= ALERTA_BAT_BAIXA_V + 0.15f) {
    alertaBatBaixaEnv = false;
  }

  if (t.tempOk && cargaCortadaTemp && !alertaTempAltaEnv) {
    msg += "🌡️ *Carregamento solar pausado por temperatura:* " + String(t.tempBateriaC, 1) + " °C\n";
    alertaTempAltaEnv = true;
  } else if (!cargaCortadaTemp) {
    alertaTempAltaEnv = false;
  }

  if (msg.length() > 0) {
    msg += "\n📊 *Resumo atual:*\n";
    msg += "• Nível: " + String(t.nivelPct, 1) + "% (" + String(t.volumeLitros, 0) + " L)\n";
    msg += "• Bateria: " + String(t.batTensaoV, 2) + " V (" + String(t.batCorrenteMA, 0) + " mA)\n";
    msg += "• Solar: " + String(t.solarPotenciaMW / 1000.0f, 2) + " W (" + String(energiaSolarWhDia, 2) + " Wh acum.)\n";
    if (t.tempOk) msg += "• Temp. Pack: " + String(t.tempBateriaC, 1) + " °C\n";

    WiFiClientSecure client;
    client.setInsecure();
    UniversalTelegramBot bot(TELEGRAM_BOT_TOKEN, client);
    bot.sendMessage(TELEGRAM_CHAT_ID, msg, "Markdown");
  }
}

// ============================================================================
// SETUP (CICLO DE ACORDAR -> MEDIR -> ENVIAR -> DEEP SLEEP)
// ============================================================================
void setup() {
  Serial.begin(115200);
  delay(200);
  bootCount++;

  pinMode(PIN_JSN_PWR, OUTPUT);
  pinMode(PIN_JSN_TRIG, OUTPUT);
  pinMode(PIN_JSN_ECHO, INPUT);
  pinMode(PIN_CUTOFF_SOLAR, OUTPUT);

  Telemetria t = {};
  t.distanciaM = medirDistanciaMetros();

  const float volumeMaxUtil = calcularVolumeLitros(H_MAX_UTIL_M);
  if (t.distanciaM > 0) {
    t.alturaAguaM  = max(0.0f, H_SENSOR_M - t.distanciaM);
    t.volumeLitros = calcularVolumeLitros(t.alturaAguaM);
    t.nivelPct     = (t.volumeLitros / volumeMaxUtil) * 100.0f;
  } else {
    t.alturaAguaM = t.volumeLitros = t.nivelPct = -1.0f;
  }

  // Define o próximo intervalo de sono de forma adaptativa
  uint32_t proximoSleepSeg = SLEEP_NORMAL_SEC;
  if (ultimoNivelPct >= 0.0f && t.nivelPct >= 0.0f && fabs(t.nivelPct - ultimoNivelPct) >= 1.5f) {
    proximoSleepSeg = SLEEP_RAPIDO_SEC; // Caixa enchendo ou esvaziando rápido
  }
  if (t.nivelPct >= 0.0f) ultimoNivelPct = t.nivelPct;

  lerEnergiaETemperatura(t, proximoSleepSeg);
  if (t.batTensaoV < ALERTA_BAT_BAIXA_V) {
    proximoSleepSeg = SLEEP_ECONOMIA_SEC;
  }

  Serial.printf("[Ciclo #%lu] Dist: %.2fm | Agua: %.2fm | Vol: %.0fL (%.1f%%) | Bat: %.2fV (%.0f%%) | Solar: %.2fW | Temp: %.1fC\n",
                bootCount, t.distanciaM, t.alturaAguaM, t.volumeLitros, t.nivelPct,
                t.batTensaoV, t.batPct, t.solarPotenciaMW / 1000.0f, t.tempBateriaC);

  // Conecta ao Wi-Fi apenas se configurado
  if (String(WIFI_SSID) != "SEU_WIFI_AQUI") {
    WiFi.mode(WIFI_STA);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    unsigned long inicio = millis();
    while (WiFi.status() != WL_CONNECTED && (millis() - inicio) < WIFI_TIMEOUT_MS) {
      delay(100);
    }

    if (WiFi.status() == WL_CONNECTED) {
      t.rssi = WiFi.RSSI();
      enviarThingSpeak(t);
      verificarEEnviarAlertasTelegram(t);
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
