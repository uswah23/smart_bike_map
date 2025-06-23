#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <SPIFFS.h>
#include <TinyGPSPlus.h>
#include <HardwareSerial.h>
#include <ESPAsyncWebServer.h>
#include <AsyncTCP.h>
#include <UniversalTelegramBot.h>

// ================== WiFi Configuration ==================
const char* ssid = "haruki";
const char* password = "1sampai6";

// ================== Telegram Bot Configuration ==================
#define BOT_TOKEN "8116140986:AAHNgjhbXCgcawcRrzGJOVNOPSrbMoFtG7o"
#define CHAT_ID   "1032611418"

WiFiClientSecure secured_client;
UniversalTelegramBot bot(BOT_TOKEN, secured_client);

// ================== GPS Configuration ==================
TinyGPSPlus gps;
HardwareSerial GPS(1);
#define RXD2 16
#define TXD2 17

// ================== Buzzer and Geofence ==================
#define BUZZER_PIN 22
float uthmLat = 1.8500;
float uthmLon = 103.0830;
float geofenceRadius = 0.005;

bool buzzerEnabled = true;
bool buzzerManuallyStopped = false;
bool buzzerCurrentlyOn = false;

unsigned long lastBuzzerStopTime = 0;
unsigned long buzzerOnTime = 0;
unsigned long lastBuzzerCycleTime = 0;

const unsigned long buzzerCooldown = 300000;         // 5 min cooldown after STOP
const unsigned long buzzerCycleInterval = 300000;    // Repeat every 5 minutes
const unsigned long buzzerActiveDuration = 60000;    // Buzzer ON for 1 minute

// ================== Web Server ==================
AsyncWebServer server(80);
AsyncWebSocket ws("/gps");

// ================== Utility: Calculate Distance ==================
float getDistance(float lat1, float lon1, float lat2, float lon2) {
  float dx = lat1 - lat2;
  float dy = lon1 - lon2;
  return sqrt(dx * dx + dy * dy);
}

// ================== Notify Web Clients ==================
void notifyClients(float lat, float lon) {
  String gpsData = "{";
  gpsData += "\"lat\":" + String(lat, 6) + ",\"lon\":" + String(lon, 6);
  gpsData += ",\"buzzer\":" + String(buzzerEnabled ? 1 : 0);
  gpsData += "}";
  ws.textAll(gpsData);
}

// ================== Telegram Command Handler ==================
void handleTelegram() {
  int newMsgCount = bot.getUpdates(bot.last_message_received + 1);
  while (newMsgCount) {
    for (int i = 0; i < newMsgCount; i++) {
      String msg = bot.messages[i].text;
      if (msg == "/stopbuzzer") {
        buzzerEnabled = false;
        buzzerManuallyStopped = true;
        lastBuzzerStopTime = millis();
        digitalWrite(BUZZER_PIN, LOW);
        buzzerCurrentlyOn = false;
        bot.sendMessage(CHAT_ID, "🛑 Buzzer manually stopped. Cooldown for 5 minutes.");
      }
    }
    newMsgCount = bot.getUpdates(bot.last_message_received + 1);
  }
}

// ================== Setup ==================
void setup() {
  Serial.begin(115200);
  GPS.begin(9600, SERIAL_8N1, RXD2, TXD2);

  pinMode(BUZZER_PIN, OUTPUT);
  digitalWrite(BUZZER_PIN, LOW);

  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500); Serial.print(".");
  }
  Serial.println("\nWiFi connected!");
  Serial.println(WiFi.localIP());

  secured_client.setInsecure();

  if (!SPIFFS.begin(true)) {
    Serial.println("SPIFFS Mount Failed");
    return;
  }

  ws.onEvent([](AsyncWebSocket *server, AsyncWebSocketClient *client,
                AwsEventType type, void *arg, uint8_t *data, size_t len) {
    if (type == WS_EVT_DATA) {
      AwsFrameInfo *info = (AwsFrameInfo *)arg;
      if (info->final && info->index == 0 && info->len == len && info->opcode == WS_TEXT) {
        String msg = "";
        for (size_t i = 0; i < len; i++) msg += (char)data[i];
        Serial.println("WebSocket: " + msg);
        if (msg == "STOP_BUZZER") {
          buzzerEnabled = false;
          buzzerManuallyStopped = true;
          lastBuzzerStopTime = millis();
          digitalWrite(BUZZER_PIN, LOW);
          buzzerCurrentlyOn = false;
          bot.sendMessage(CHAT_ID, "🛑 Buzzer stopped via web. Cooldown for 5 minutes.");
        }
      }
    }
  });

  server.addHandler(&ws);
  server.serveStatic("/", SPIFFS, "/").setDefaultFile("index.html");
  server.begin();
}

// ================== Main Loop ==================
void loop() {
  while (GPS.available()) {
    gps.encode(GPS.read());

    if (gps.location.isUpdated()) {
      float lat = gps.location.lat();
      float lon = gps.location.lng();
      notifyClients(lat, lon);

      float dist = getDistance(lat, lon, uthmLat, uthmLon);

      // ✅ Inside UTHM – reset system state
      if (dist <= geofenceRadius) {
        digitalWrite(BUZZER_PIN, LOW);
        buzzerEnabled = true;
        buzzerManuallyStopped = false;
        buzzerCurrentlyOn = false;
        lastBuzzerCycleTime = 0;
        return;
      }

      // 🟥 Outside UTHM – apply buzzer logic

      // 1. If in cooldown period
      if (buzzerManuallyStopped && (millis() - lastBuzzerStopTime < buzzerCooldown)) {
        digitalWrite(BUZZER_PIN, LOW);
        buzzerCurrentlyOn = false;
        return;
      }

      // 2. Cooldown expired – re-enable buzzer
      if (buzzerManuallyStopped && (millis() - lastBuzzerStopTime >= buzzerCooldown)) {
        buzzerEnabled = true;
        buzzerManuallyStopped = false;
        digitalWrite(BUZZER_PIN, LOW);
        buzzerCurrentlyOn = false;
        return;
      }

      // 3. Buzzer is ON – turn OFF after 1 minute
      if (buzzerCurrentlyOn) {
        if (millis() - buzzerOnTime >= buzzerActiveDuration) {
          digitalWrite(BUZZER_PIN, LOW);
          buzzerCurrentlyOn = false;
          Serial.println("🔕 Buzzer OFF after 1 minute.");
        }
        return;
      }

      // 4. First detection outside – start cycle timer
      if (lastBuzzerCycleTime == 0) {
        lastBuzzerCycleTime = millis();
      }

      // 5. If cycle interval passed, activate buzzer
      if (!buzzerCurrentlyOn && (millis() - lastBuzzerCycleTime >= buzzerCycleInterval)) {
        digitalWrite(BUZZER_PIN, HIGH);
        buzzerCurrentlyOn = true;
        buzzerOnTime = millis();
        lastBuzzerCycleTime = millis();
        Serial.println("🔔 Buzzer ON for 1 minute.");
        bot.sendMessage(CHAT_ID, "🚨 Still outside UTHM! Buzzer ON for 1 minute.\nLat: " + String(lat, 6) + "\nLon: " + String(lon, 6), "");
      }
    }
  }

  handleTelegram();
  delay(1000);
}
