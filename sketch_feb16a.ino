#include <ESP8266WiFi.h>
#include <WiFiClientSecure.h>
#include <UniversalTelegramBot.h>
#include <Servo.h>
#include <DHT.h>

// --- WiFi CREDENTIALS ---
#define WIFI_SSID "PROJECT.IO"
#define WIFI_PASSWORD "___"

// --- TELEGRAM BOT TOKEN ---
#define BOT_TOKEN "_"

// --- PIN DEFINITIONS ---
#define PUMP_PIN    D0      // L298N IN1
#define DHTPIN      D1      // DHT Sensor
#define BUZZER_PIN  D2      // Active Buzzer
#define TRIG_PIN    D5      // Ultrasonic Trig
#define ECHO_PIN    D6      // Ultrasonic Echo
#define SERVO1_PIN  D7      // Gate Servo 1
#define SERVO2_PIN  D8      // Gate Servo 2
#define SOIL_PIN    A0      // Soil Moisture Sensor

// --- CONFIGURATION & THRESHOLDS ---
#define DHTTYPE DHT11
const int SOIL_THRESHOLD = 700;
const int ANIMAL_DISTANCE = 30;

// Servo Angles
const int GATES_OPEN = 180;
const int GATES_CLOSED = 0;

// --- OBJECTS ---
DHT dht(DHTPIN, DHTTYPE);
Servo servo1;
Servo servo2;
WiFiClientSecure client;
UniversalTelegramBot bot(BOT_TOKEN, client);

// --- STATE VARIABLES ---
bool autoMode = true;
bool buzzerEnabled = true;
unsigned long lastIrrigationCheck = 0;
unsigned long lastBotCheck = 0;
const long irrigationInterval = 2000;
const long botCheckInterval = 1000; // Check for messages every second

void setup() {
  Serial.begin(115200);
  
  // Initialize Pins
  pinMode(PUMP_PIN, OUTPUT);
  pinMode(BUZZER_PIN, OUTPUT);
  pinMode(TRIG_PIN, OUTPUT);
  pinMode(ECHO_PIN, INPUT);

  // Initialize Components
  dht.begin();
  servo1.attach(SERVO1_PIN);
  servo2.attach(SERVO2_PIN);

  // STARTUP STATE
  stopWater();
  
  // Connect to WiFi
  Serial.println("\n--- SMART FARM SYSTEM STARTING ---");
  Serial.print("Connecting to WiFi");
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  
  Serial.println("\nWiFi Connected!");
  Serial.print("IP Address: ");
  Serial.println(WiFi.localIP());
  
  // Configure SSL client (required for Telegram)
  client.setInsecure(); // For ESP8266
  
  Serial.println("System Ready - Send /start to your Telegram bot");
}

void loop() {
  // 1. CHECK FOR USER INPUT (Serial)
  checkUserKeys();

  // 2. CHECK TELEGRAM MESSAGES
  if (millis() - lastBotCheck > botCheckInterval) {
    lastBotCheck = millis();
    checkTelegramMessages();
  }

  // 3. SECURITY CHECK
  checkSecurity();

  // 4. IRRIGATION CHECK (Auto Mode)
  if (autoMode) {
    unsigned long currentMillis = millis();
    if (currentMillis - lastIrrigationCheck >= irrigationInterval) {
      lastIrrigationCheck = currentMillis;
      checkIrrigation();
      printStatus(); 
    }
  }
}

// --- TELEGRAM MESSAGE HANDLER ---
void checkTelegramMessages() {
  int numNewMessages = bot.getUpdates(bot.last_message_received + 1);
  
  for (int i = 0; i < numNewMessages; i++) {
    String chat_id = String(bot.messages[i].chat_id);
    String text = bot.messages[i].text;
    String from_name = bot.messages[i].from_name;

    Serial.println("Telegram: " + text + " from " + from_name);

    if (text == "/start") {
      String welcome = "🌾 *Smart Farm Control Bot* 🌾\n\n";
      welcome += "Available Commands:\n";
      welcome += "/status - Get current status\n";
      welcome += "/wateron - Turn water ON\n";
      welcome += "/wateroff - Turn water OFF\n";
      welcome += "/auto - Enable Auto Mode\n";
      welcome += "/buzzeroff - Silence alarm\n";
      welcome += "/buzzeron - Enable alarm\n";
      welcome += "/opengates - Open gates only\n";
      welcome += "/help - Show this menu";
      bot.sendMessage(chat_id, welcome, "Markdown");
    }
    
    else if (text == "/status") {
      sendStatus(chat_id);
    }
    
    else if (text == "/wateron") {
      autoMode = false;
      startWater();
      bot.sendMessage(chat_id, "💧 Water turned ON (Manual Mode)", "");
    }
    
    else if (text == "/wateroff") {
      autoMode = false;
      stopWater();
      bot.sendMessage(chat_id, "🛑 Water turned OFF (Manual Mode)", "");
    }
    
    else if (text == "/auto") {
      autoMode = true;
      buzzerEnabled = true;
      bot.sendMessage(chat_id, "🤖 Auto Mode ENABLED", "");
    }
    
    else if (text == "/buzzeroff") {
      buzzerEnabled = false;
      digitalWrite(BUZZER_PIN, LOW);
      bot.sendMessage(chat_id, "🔇 Buzzer SILENCED", "");
    }
    
    else if (text == "/buzzeron") {
      buzzerEnabled = true;
      bot.sendMessage(chat_id, "🔔 Buzzer ENABLED", "");
    }
    
    else if (text == "/opengates") {
      autoMode = false;
      digitalWrite(PUMP_PIN, LOW);
      servo1.write(GATES_OPEN);
      servo2.write(GATES_OPEN);
      bot.sendMessage(chat_id, "🚪 Gates OPENED (Pump OFF)", "");
    }
    
    else if (text == "/help") {
      String help = "📱 *Command List*\n\n";
      help += "💧 Water Control:\n";
      help += "/wateron - Manual water ON\n";
      help += "/wateroff - Manual water OFF\n";
      help += "/opengates - Open gates only\n\n";
      help += "🤖 System Control:\n";
      help += "/auto - Auto irrigation mode\n";
      help += "/status - View sensor data\n\n";
      help += "🔔 Alarm Control:\n";
      help += "/buzzeroff - Silence alarm\n";
      help += "/buzzeron - Enable alarm";
      bot.sendMessage(chat_id, help, "Markdown");
    }
    
    else {
      bot.sendMessage(chat_id, "❓ Unknown command. Send /help for options.", "");
    }
  }
}

// --- SEND STATUS TO TELEGRAM ---
void sendStatus(String chat_id) {
  float h = dht.readHumidity();
  float t = dht.readTemperature();
  int m = analogRead(SOIL_PIN);
  
  String status = "📊 *Farm Status Report*\n\n";
  status += "🌡️ Temperature: " + String(t, 1) + "°C\n";
  status += "💧 Humidity: " + String(h, 1) + "%\n";
  status += "🌱 Soil Moisture: " + String(m) + "\n";
  status += "   Status: " + String(m > SOIL_THRESHOLD ? "DRY 🟤" : "WET 🟢") + "\n\n";
  status += "⚙️ Mode: " + String(autoMode ? "AUTO 🤖" : "MANUAL 👤") + "\n";
  status += "💧 Water: " + String(digitalRead(PUMP_PIN) ? "ON" : "OFF") + "\n";
  status += "🔔 Buzzer: " + String(buzzerEnabled ? "ENABLED" : "SILENCED");
  
  bot.sendMessage(chat_id, status, "Markdown");
}

// --- SERIAL KEY COMMANDS (Original functionality) ---
void checkUserKeys() {
  if (Serial.available() > 0) {
    char key = Serial.read();
    
    switch (key) {
      case 's':
        Serial.println(">> MANUAL: STOPPING WATER");
        autoMode = false;
        stopWater();
        break;

      case 'w':
        Serial.println(">> MANUAL: FORCING WATER ON");
        autoMode = false;
        startWater();
        break;
        
      case 'o':
        Serial.println(">> MANUAL: GATES OPEN (PUMP OFF)");
        autoMode = false;
        digitalWrite(PUMP_PIN, LOW);
        servo1.write(GATES_OPEN);
        servo2.write(GATES_OPEN);
        break;

      case 'b':
        Serial.println(">> MANUAL: BUZZER SILENCED");
        buzzerEnabled = false;
        digitalWrite(BUZZER_PIN, LOW);
        break;

      case 'a':
        Serial.println(">> SYSTEM: RESUMING AUTO MODE");
        autoMode = true;
        buzzerEnabled = true;
        break;
    }
  }
}

// --- SECURITY: DETECT ANIMALS ---
void checkSecurity() {
  if (!buzzerEnabled) return;

  long duration, distance;
  
  digitalWrite(TRIG_PIN, LOW);
  delayMicroseconds(2);
  digitalWrite(TRIG_PIN, HIGH);
  delayMicroseconds(10);
  digitalWrite(TRIG_PIN, LOW);
  
  duration = pulseIn(ECHO_PIN, HIGH);
  distance = duration * 0.034 / 2;

  if (distance > 0 && distance < ANIMAL_DISTANCE) {
    digitalWrite(BUZZER_PIN, HIGH);
    static unsigned long lastAlarmPrint = 0;
    if (millis() - lastAlarmPrint > 1000) {
      Serial.println("⚠️ ALARM: Animal Detected!");
      lastAlarmPrint = millis();
    }
  } else {
    digitalWrite(BUZZER_PIN, LOW);
  }
}

// --- AUTO IRRIGATION ---
void checkIrrigation() {
  int moistureValue = analogRead(SOIL_PIN);
  
  if (moistureValue > SOIL_THRESHOLD) {
    startWater();
  } else {
    stopWater();
  }
}

// --- HELPER FUNCTIONS ---
void startWater() {
  servo1.write(GATES_OPEN);
  servo2.write(GATES_OPEN);
  digitalWrite(PUMP_PIN, HIGH);
}

void stopWater() {
  digitalWrite(PUMP_PIN, LOW);
  servo1.write(GATES_CLOSED);
  servo2.write(GATES_CLOSED);
}

void printStatus() {
  float h = dht.readHumidity();
  float t = dht.readTemperature();
  int m = analogRead(SOIL_PIN);

  Serial.print("Temp: "); Serial.print(t);
  Serial.print("°C | Humidity: "); Serial.print(h);
  Serial.print("% | Soil: "); Serial.print(m);
  Serial.print(" | Mode: "); 
  Serial.println(autoMode ? "AUTO" : "MANUAL");
}
