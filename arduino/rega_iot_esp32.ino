#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <time.h>
#include <WiFi.h>           
#include <HTTPClient.h>
#include <ArduinoJson.h>

// --- Configuração da API Firebase ---
const char* FIREBASE_API_URL = "https://rega-iot-default-rtdb.firebaseio.com/api/sensor"; 

// --- Credenciais Wi-Fi (ATUALIZE) ---
const char* ssid = "Wokwi-GUEST"; 
const char* password = ""; 

// Endereço padrão do LCD I2C
LiquidCrystal_I2C lcd(0x27, 16, 2);

// Definição de pinos
const int pinoSensorUmidade = 34;
const int pinoRele = 23;

// Variáveis de controle (Lógica de rega: < 25%)
const int UMIDADE_MINIMA = 25; 
const int UMIDADE_MAXIMA = 60; 
const unsigned long INTERVALO_MINIMO_REGA = 3600000; // 1 hora
const int DURACAO_REGA_MS = 10000; // 10 segundos
const int NUM_CICLOS_REGA = 5;

int umidadeSolo = 0;
int umidadePercentual = 0;
unsigned long tempoUltimaRega = 0;

// Configuração de fuso horário e servidor NTP
const char* ntpServer = "a.st1.ntp.br";
// 3 horas * 3600 segundos/hora = -10800 segundos (para GMT-3)
const long gmtOffset_sec = -10800; 
const int daylightOffset_sec = 0;

void regar() {
    Serial.println("Iniciando rega...");
    lcd.clear();
    lcd.setCursor(0,0);
    lcd.print("Rega Ativa...");
    for (int i = 0; i < NUM_CICLOS_REGA; i++) {
        digitalWrite(pinoRele, HIGH); 
        lcd.setCursor(0,1);
        lcd.print("Ciclo: ");
        lcd.print(i + 1);
        lcd.print("/");
        lcd.print(NUM_CICLOS_REGA);
        lcd.print("   ");
        delay(DURACAO_REGA_MS); 
        digitalWrite(pinoRele, LOW); 
        delay(1000); 
    }
    lcd.clear();
    lcd.setCursor(0,0);
    lcd.print("Rega concluida");
    Serial.println("Rega concluída.");
    delay(2000);
    lcd.clear();
}

bool estaEmPeriodoProibido() {
    if (WiFi.status() != WL_CONNECTED) {
        return true; 
    }
    struct tm timeinfo;
    if (!getLocalTime(&timeinfo, 100)) { 
        return true; 
    }
    int horaAtual = timeinfo.tm_hour;
    int minutoAtual = timeinfo.tm_min;
    // Lógica: Proibido entre 11:00 e 14:29:59 (14:30 já é liberado)
    if (horaAtual >= 11 && horaAtual <= 14) {
        if (horaAtual == 14 && minutoAtual >= 30) {
            return false; 
        }
        return true; 
    }
    return false; 
}

void enviaDadosParaApi(int umidade, bool regando) {
    if (WiFi.status() == WL_CONNECTED) {
        HTTPClient http;
        http.begin(FIREBASE_API_URL);
        http.addHeader("Content-Type", "application/json");
        
        StaticJsonDocument<100> doc;
        doc["umidade"] = umidade;
        doc["regando"] = regando;
        // CORREÇÃO: Usa time(NULL) para obter o timestamp Unix
        doc["timestamp"] = time(NULL); 
        
        String jsonOutput;
        serializeJson(doc, jsonOutput);
        
        int httpResponseCode = http.POST(jsonOutput);
        if (httpResponseCode > 0) {
            Serial.printf("[HTTP] POST OK, Code: %d\n", httpResponseCode);
        } else {
            Serial.printf("[HTTP] POST failed, Error: %s\n", http.errorToString(httpResponseCode).c_str());
        }
        http.end();
    }
}

void setup() {
    Serial.begin(115200);
    lcd.init();
    lcd.backlight();
    lcd.clear();
    pinMode(pinoSensorUmidade, INPUT);
    pinMode(pinoRele, OUTPUT);
    digitalWrite(pinoRele, LOW); 

    // --- Conexão Wi-Fi Direta ---
    lcd.setCursor(0,0);
    lcd.print("Conectando Wi-Fi");
    WiFi.begin(ssid, password);
    
    int maxTries = 30;
    while (WiFi.status() != WL_CONNECTED && maxTries > 0) {
        delay(1000);
        Serial.print(".");
        lcd.setCursor(0,1);
        lcd.print("Aguardando: ");
        lcd.print(maxTries);
        maxTries--;
    }

    if (WiFi.status() != WL_CONNECTED) {
        Serial.println("\nFalha ao conectar Wi-Fi. Reiniciando...");
        lcd.clear();
        lcd.setCursor(0,0);
        lcd.print("ERRO Wi-Fi");
        delay(5000);
        ESP.restart(); 
    }
    
    Serial.println("\nWi-Fi Conectado!");
    Serial.print("IP: ");
    Serial.println(WiFi.localIP());

    // Configuração do horário (NTP)
    configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);

    lcd.clear();
    lcd.setCursor(0,0);
    lcd.print("Wi-Fi Conectado!");
    delay(2000);
    lcd.clear();
}

void loop() {
    if (WiFi.status() != WL_CONNECTED) {
        // Tenta reconectar a cada loop se cair
        WiFi.reconnect();
        return;
    }

    umidadeSolo = analogRead(pinoSensorUmidade);
    // Mapeamento mantido para 0-4095 do ADC para 0-100%
    umidadePercentual = map(umidadeSolo, 0, 4095, 0, 100); 

    bool regaNecessaria = umidadePercentual < UMIDADE_MINIMA;
    bool regaOcorreu = false; 
    
    // Atualiza o display com a umidade
    lcd.setCursor(0,0);
    lcd.print("Umidade:");
    lcd.setCursor(9,0);
    lcd.print(umidadePercentual);
    lcd.print("%   ");

    // Lógica de rega
    if (regaNecessaria) {
        if (estaEmPeriodoProibido()) {
            lcd.setCursor(0,1);
            lcd.print("Periodo Proibido");
        } else if (millis() - tempoUltimaRega < INTERVALO_MINIMO_REGA) {
            lcd.setCursor(0,1);
            lcd.print("Aguardando Rec. ");
        } else {
            // Rega liberada
            lcd.setCursor(0,1);
            lcd.print("Solo Seco! Rega!");
            regar();
            tempoUltimaRega = millis();
            regaOcorreu = true;
        }
    } else if (umidadePercentual > UMIDADE_MAXIMA) { 
        lcd.setCursor(0,1);
        lcd.print("Solo Encharcado!");
    } else {
        lcd.setCursor(0,1);
        lcd.print("Umidade OK.     ");
    }

    enviaDadosParaApi(umidadePercentual, regaOcorreu); 

    delay(10000); 
}
