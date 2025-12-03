#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include "esp_task_wdt.h"
#include <time.h>

// ======================= CONFIGURAÇÕES =======================
// WiFi
const char* ssid = "Wokwi-GUEST"; 
const char* password = ""; 

// Firebase RTDB
const char* RTDB_URL = "https://rega-iot-default-rtdb.firebaseio.com";

// LCD
LiquidCrystal_I2C lcd(0x27, 16, 2);

// Pinos
const int pinoSensorUmidade = 34; // ADC GPIO34
const int pinoRele = 23;           // Bomba

// Lógica
int umidade_minima = 25;
long intervalo_minimo_rega_ms = 60 * 60 * 1000;
int ciclos = 3;
int duracao_rega_ms = 5000;
int intervalo_ciclos_ms = 3000;
String periodo_proibido_inicio = "11:00";
String periodo_proibido_fim = "14:00";

// Controle Manual
bool manualOverride = false;
bool pumpState = false;

// Estado
bool regando = false;
unsigned long ultima_rega = 0;
unsigned long ultima_config_sync = 0;
unsigned long inicio_rega = 0;
int ciclo_atual = 0;
unsigned long ultimo_envio_leitura = 0;

// NTP
const char* ntpServer = "a.st1.ntp.br";
const long gmtOffset_sec = -3 * 3600;
const long daylightOffset_sec = 0;

// Buffer anti-perda de pacotes
int lastUmidade = -1;
bool lastRegando = false;
bool lastManual = false;

// ======================= FUNÇÕES =======================

// Conecta WiFi com reconexão
void conectaWifi() {
  if (WiFi.status() == WL_CONNECTED) return;
  Serial.print("Conectando WiFi...");
  WiFi.begin(ssid, password);
  int tentativas = 0;
  while (WiFi.status() != WL_CONNECTED && tentativas < 30) {
    delay(500);
    Serial.print(".");
    tentativas++;
  }
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("Conectado!");
    Serial.println(WiFi.localIP());
  } else {
    Serial.println("Falha WiFi, reiniciando...");
    ESP.restart();
  }
}

// Sincroniza tempo NTP
void sincronizaTempo() {
  configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
  Serial.println("Tempo sincronizado.");
}

// Converte "HH:MM" para minutos
int toMinutes(String timeStr) {
  if (timeStr.length() != 5 || timeStr[2] != ':') return -1;
  int h = timeStr.substring(0, 2).toInt();
  int m = timeStr.substring(3, 5).toInt();
  return h * 60 + m;
}

// Verifica período proibido
bool estaEmPeriodoProibido() {
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo)) return false;
  char buffer[6]; 
  strftime(buffer, sizeof(buffer), "%H:%M", &timeinfo);
  int minAtual = toMinutes(buffer);
  int minInicio = toMinutes(periodo_proibido_inicio);
  int minFim = toMinutes(periodo_proibido_fim);
  if (minInicio > minFim) return (minAtual >= minInicio) || (minAtual < minFim);
  return (minAtual >= minInicio) && (minAtual < minFim);
}

// Liga/desliga bomba
void setarBomba(bool ligada) {
  digitalWrite(pinoRele, ligada ? LOW : HIGH);
  Serial.printf("Bomba: %s\n", ligada ? "LIGADA" : "DESLIGADA");
}

// ======================= Firebase =======================

void enviaLeituraFirebase(int umidade, bool regandoAtual, bool erroSensor) {
  if (WiFi.status() != WL_CONNECTED) return;
  if (umidade == lastUmidade && regandoAtual == lastRegando && manualOverride == lastManual) return; // buffer anti-perda

  HTTPClient http;
  String url = String(RTDB_URL) + "/sensor/lastReading.json";
  http.begin(url);

  StaticJsonDocument<256> doc;
  doc["umidade"] = umidade;
  doc["regando"] = regandoAtual;
  doc["manualOverride"] = manualOverride;
  doc["erroSensor"] = erroSensor;
  doc["timestamp"] = (long)millis();
  doc["ultimaRega"] = ultima_rega;

  String payload;
  serializeJson(doc, payload);
  int httpCode = http.PUT(payload);
  if (httpCode <= 0) Serial.printf("Erro HTTP: %s\n", http.errorToString(httpCode).c_str());
  http.end();

  lastUmidade = umidade;
  lastRegando = regandoAtual;
  lastManual = manualOverride;
}

// Busca configuração
void buscaConfiguracao() {
  if (WiFi.status() != WL_CONNECTED) return;
  HTTPClient http;
  String url = String(RTDB_URL) + "/config.json";
  http.begin(url);
  int httpCode = http.GET();
  if (httpCode == HTTP_CODE_OK) {
    String payload = http.getString();
    StaticJsonDocument<512> doc;
    DeserializationError error = deserializeJson(doc, payload);
    if (!error) {
      umidade_minima = doc["umidadeMin"] | 25;
      intervalo_minimo_rega_ms = (long)(doc["intervaloRegaMinutos"] | 60) * 60 * 1000;
      periodo_proibido_inicio = doc["periodoProibidoInicio"] | "11:00";
      periodo_proibido_fim = doc["periodoProibidoFim"] | "14:00";
      ciclos = doc["ciclos"] | 3;
      duracao_rega_ms = (doc["duracaoRegaSegundos"] | 5) * 1000;
      intervalo_ciclos_ms = (doc["intervaloCiclosSegundos"] | 3) * 1000;
      manualOverride = doc["manualOverride"] | false;
      pumpState = doc["pumpState"] | false;
      ultima_config_sync = millis();
      Serial.println("Config sincronizada.");
    }
  }
  http.end();
}

// ======================= REGA =======================
void executarRegaAutomatica() {
  regando = true;
  inicio_rega = millis();
  ciclo_atual = 1;
  Serial.println("Iniciando rega automática...");
  lcd.setCursor(0, 1);
  lcd.print("REGANDO... (Cyc 1)");
  setarBomba(true);
}

void lidaComRegaAutomatica() {
  if (!regando) return;
  unsigned long tempo = millis() - inicio_rega;
  if (ciclo_atual <= ciclos) {
    if (tempo < duracao_rega_ms) {
      lcd.setCursor(0,1);
      lcd.printf("REGANDO...(%ds)", (duracao_rega_ms - tempo)/1000);
    } else if (tempo < (duracao_rega_ms + intervalo_ciclos_ms)) {
      setarBomba(false);
      lcd.setCursor(0,1);
      lcd.printf("DRENO...(%ds)", (duracao_rega_ms + intervalo_ciclos_ms - tempo)/1000);
    } else {
      ciclo_atual++;
      if (ciclo_atual <= ciclos) {
        inicio_rega = millis();
        setarBomba(true);
        lcd.setCursor(0,1);
        lcd.printf("REGANDO...(Cyc %d)", ciclo_atual);
      } else {
        setarBomba(false);
        regando = false;
        ultima_rega = millis();
        int umidade_final = map(analogRead(pinoSensorUmidade), 0, 4095, 0, 100);
        enviaLeituraFirebase(umidade_final, regando, false);
        Serial.println("Rega concluída.");
      }
    }
  }
}

void lidaComRegaManual() {
  if (regando) { setarBomba(false); regando = false; }
  setarBomba(pumpState);
  lcd.setCursor(0,0);
  lcd.printf("MANUAL: %s", pumpState?"LIGADA":"DESLIGADA");
}

// ======================= SETUP / LOOP =======================
void setup() {
  Serial.begin(115200);
  lcd.init();
  lcd.backlight();
  lcd.print("Rega IoT Init");

  pinMode(pinoRele, OUTPUT);
  setarBomba(false);

  conectaWifi();
  sincronizaTempo();
  buscaConfiguracao();

  // Watchdog compatível ESP32 Core 3.x
  esp_task_wdt_config_t twdt_config = {
    .timeout_ms = 8000,
    .idle_core_mask = (1 << 0),
    .trigger_panic = true
  };
  esp_task_wdt_init(&twdt_config);
  esp_task_wdt_add(NULL);
}

void loop() {
  esp_task_wdt_reset();
  conectaWifi(); // garante reconexão

  int leitura = analogRead(pinoSensorUmidade);
  int umidade = map(leitura, 0, 4095, 0, 100);
  bool proibido = estaEmPeriodoProibido();
  bool erroSensor = leitura < 0 || leitura > 4095;

  if (manualOverride) lidaComRegaManual();
  else {
    if (regando) lidaComRegaAutomatica();
    else if (!proibido && umidade <= umidade_minima && millis() - ultima_rega >= intervalo_minimo_rega_ms) executarRegaAutomatica();
  }

  if (millis() - ultimo_envio_leitura >= 10000) {
    enviaLeituraFirebase(umidade, regando, erroSensor);
    ultimo_envio_leitura = millis();
  }

  if (millis() - ultima_config_sync > 5UL*60UL*1000UL) buscaConfiguracao();

  delay(500);
}
