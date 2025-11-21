#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <time.h>

// ======================= CONFIGURAÇÕES GLOBAIS =======================

// --- WiFi ---
const char* ssid = "Wokwi-GUEST"; // Substitua pelo seu SSID
const char* password = ""; // Substitua pela sua senha

// --- Cloud Function API ---
// ⚠️ SUBSTITUA pela URL base da sua Cloud Function (Ex: https://us-central1-rega-iot.cloudfunctions.net/api)
const char* API_BASE_URL = "https://eur3-rega-iot.cloudfunctions.net/api"; 
// ⚠️ Deve ser a mesma chave secreta definida no index.ts!
const char* API_SECRET_KEY = "SUA_CHAVE_SECRETA_FORTE_AQUI"; 

// --- LCD ---
LiquidCrystal_I2C lcd(0x27, 16, 2);

// --- Pinos ---
const int pinoSensorUmidade = 34; // GPIO 34 (Sensor Analógico)
const int pinoRele = 23; // GPIO 23 (Bomba/Rele)

// --- Variáveis de Lógica (sincronizadas com o Backend) ---
// Valores iniciais (default)
int umidade_minima = 25;
int intervalo_minimo_rega_ms = 60 * 60 * 1000; // 60 minutos
int ciclos = 5;
int duracao_rega_ms = 10000; // 10 segundos
int intervalo_ciclos_ms = 5000; // 5 segundos

// Variáveis de Estado
bool regando = false;
unsigned long ultimo_envio = 0;
unsigned long ultima_rega = 0;
unsigned long ultima_config_sync = 0;

// --- NTP (Sincronização de Horário) ---
const char* ntpServer = "a.st1.ntp.br";
const long gmtOffset_sec = -10800; // GMT-3 (Horário de Brasília)


// ======================= FUNÇÕES AUXILIARES =======================

// Horário proibido para rega (hardcoded para o período 11:00 - 14:30)
bool estaEmPeriodoProibido() {
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo)) return false;

  int h = timeinfo.tm_hour;
  int m = timeinfo.tm_min;

  // Período proibido: [11:00] até [14:30[
  if (h < 11) return false;
  if (h > 14) return false;
  if (h == 14 && m >= 30) return false;

  return true;
}

// Rega física (executada pelo ESP32)
void executarRega() {
  regando = true;
  Serial.println("INICIANDO REGA...");
  
  for (int i = 0; i < ciclos; i++) {
    lcd.setCursor(0, 1);
    lcd.printf("REGANDO... (%d/%d)", i + 1, ciclos);
    
    digitalWrite(pinoRele, HIGH);
    delay(duracao_rega_ms);
    digitalWrite(pinoRele, LOW);
    
    if (i < ciclos - 1) delay(intervalo_ciclos_ms);
  }
  
  regando = false;
  ultima_rega = millis();
  Serial.println("REGA CONCLUÍDA.");
}

// ======================= SINCRONIZAÇÃO DE CONFIG =======================

// Sincroniza as variáveis de configuração do RTDB via Cloud Function GET
void buscaConfiguracao() {
  if (WiFi.status() != WL_CONNECTED) return;
  
  HTTPClient http;
  String url = String(API_BASE_URL) + "/config";
  http.begin(url);
  
  int httpResponseCode = http.GET();
  
  if (httpResponseCode == HTTP_CODE_OK) {
    String payload = http.getString();
    StaticJsonDocument<256> doc;
    deserializeJson(doc, payload);

    int novaUmidadeMin = doc["umidadeMin"] | 25;
    int novoIntervalo = doc["intervaloRegaMinutos"] | 60;
    
    if (novaUmidadeMin != umidade_minima || novoIntervalo * 60 * 1000 != intervalo_minimo_rega_ms) {
        umidade_minima = novaUmidadeMin;
        intervalo_minimo_rega_ms = novoIntervalo * 60 * 1000;
        Serial.printf("Config atualizada: Umi Min=%d%%, Intervalo=%d min\n", umidade_minima, novoIntervalo);
    }
  } else {
    Serial.printf("ERRO GET Config: %s, Code: %d\n", url.c_str(), httpResponseCode);
  }
  http.end();
  ultima_config_sync = millis();
}

// ======================= TELEMETRIA (POST PARA CLOUD FUNCTION) =======================

// Envia dados do sensor para a Cloud Function /api/sensor
void enviaDadosTelemetria(int umidade) {
  if (WiFi.status() != WL_CONNECTED) return;

  HTTPClient http;
  String url = String(API_BASE_URL) + "/sensor";
  http.begin(url);
  http.addHeader("Content-Type", "application/json");
  // Adiciona a chave secreta para autenticação com a Cloud Function
  http.addHeader("X-API-Key", API_SECRET_KEY); 

  StaticJsonDocument<256> doc;
  doc["umidade"] = umidade;
  doc["regando"] = regando;
  // Envia o timestamp Unix local (segundos)
  doc["timestamp"] = time(NULL); 

  String jsonOutput;
  serializeJson(doc, jsonOutput);

  int httpResponseCode = http.POST(jsonOutput);
  Serial.printf("POST para API: Code: %d. Umidade: %d%%\n", httpResponseCode, umidade);
  http.end();
}


// ======================= SETUP =======================
void setup() {
  Serial.begin(115200);
  
  // Inicialização do LCD
  lcd.init();
  lcd.backlight();
  lcd.setCursor(0, 0);
  lcd.print("Rega IoT ESP32");
  lcd.setCursor(0, 1);
  lcd.print("Inicializando...");
  
  pinMode(pinoRele, OUTPUT);
  digitalWrite(pinoRele, LOW); // Garante que a bomba está desligada

  // --- WiFi ---
  WiFi.begin(ssid, password);
  lcd.setCursor(0, 1);
  lcd.print("Conectando Wi-Fi");
  int cont = 0;
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    lcd.setCursor(15, 1);
    lcd.print((cont++ % 2 == 0) ? "." : " ");
    if (cont > 20) ESP.restart(); // Tenta reiniciar em caso de falha de conexão
  }

  lcd.clear();
  lcd.print("Wi-Fi Conectado!");

  // --- NTP ---
  configTime(gmtOffset_sec, 0, ntpServer);
  delay(1000);

  // Busca a configuração inicial no Backend
  buscaConfiguracao();
}

// ======================= LOOP =======================
void loop() {
  if (WiFi.status() != WL_CONNECTED) {
    // Tenta reconectar se perder o WiFi
    lcd.clear();
    lcd.print("WiFi Desconectado");
    WiFi.reconnect();
    delay(2000);
    return;
  }
  
  // --- Leitura sensor ---
  // Mapeia de 0-4095 (Leitura ADC) para 0-100 (Umidade %)
  int leitura = analogRead(pinoSensorUmidade);
  int umidade = map(leitura, 0, 4095, 0, 100);
  
  bool proibido = estaEmPeriodoProibido();

  // --- Lógica de rega (com base em parâmetros do Backend) ---
  if (!proibido && !regando) {
    if (umidade <= umidade_minima) {
      if (millis() - ultima_rega >= intervalo_minimo_rega_ms) {
        executarRega();
      } else {
        Serial.println("Rega necessária, mas em intervalo mínimo. Aguardando...");
      }
    }
  }

  // --- Sincronização de Configuração Periódica ---
  // Sincroniza as configurações a cada 5 minutos
  if (millis() - ultima_config_sync > 5UL * 60UL * 1000UL) {
      buscaConfiguracao();
  }

  // --- Atualiza LCD ---
  lcd.setCursor(0, 0);
  lcd.printf("Umi:%d%% %s", umidade, proibido ? "(PROIBIDO)" : " ");
  lcd.setCursor(0, 1);
  
  if (regando) {
    // Mensagem de regando é atualizada dentro da função executarRega()
  } else if (proibido) {
    struct tm timeinfo;
    getLocalTime(&timeinfo);
    lcd.printf("PROIBIDO! %02d:%02d", timeinfo.tm_hour, timeinfo.tm_min);
  } else if (umidade <= umidade_minima) {
    lcd.print("AGUARDANDO INTERVALO");
  } else {
    lcd.printf("Umidade OK! %02d:%02d", timeinfo.tm_hour, timeinfo.tm_min);
  }
  
  // --- Envio de dados (Telemetria) ---
  // Envia a cada 10 segundos
  if (millis() - ultimo_envio > 10000) {
    enviaDadosTelemetria(umidade);
    ultimo_envio = millis();
  }
  
  // --- Pequena espera ---
  delay(100);
}
