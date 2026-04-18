/*
 * Automatic Pet Feeder with Firebase Integration
 *
 * Firebase Features:
 *  - /commands/feed (bool) → set to true from Firebase to trigger feeding remotely
 *  - /status/online (bool) → heartbeat so you know the device is alive
 *  - /status/lastSeen (string) → uptime timestamp (ms since boot)
 *  - /feedLog/push → each feeding is logged with a timestamp
 *
 * Libraries needed (install via Library Manager):
 *  - Firebase ESP Client (by Mobizt)
 *  - ArduinoJson
 *  - Adafruit SSD1306
 *  - Adafruit GFX
 *  - ESP32Servo
 */

#include <WiFi.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <WebServer.h>
#include <ESP32Servo.h>
#include <Firebase_ESP_Client.h>
#include "addons/TokenHelper.h"
#include "addons/RTDBHelper.h"

// ─── Wi-Fi ────────────────────────────────────────────────────────────────────
const char* ssid     = "SM NOVA";
const char* password = "nixs0416";

// ─── Firebase ─────────────────────────────────────────────────────────────────
#define API_KEY      "AIzaSyAip0LK8fhTmLdobewi-hCX-meyLpZOnUg"
#define DATABASE_URL "https://petfeeder-fdce3-default-rtdb.asia-southeast1.firebasedatabase.app/"
#define USER_EMAIL    ""
#define USER_PASSWORD ""

FirebaseData   fbdo;
FirebaseData   fbdoStream;
FirebaseAuth   auth;
FirebaseConfig firebaseConfig;

bool firebaseReady      = false;
bool feedCommandPending = false;

// ─── Hardware pins ────────────────────────────────────────────────────────────
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET    -1
#define SDA_PIN 21
#define SCL_PIN 22
#define GREEN_LED 25
#define RED_LED   26

const int servoPin = 13;

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);
WebServer        server(80);
Servo            myServo;

unsigned long lastHeartbeat = 0;
const unsigned long HEARTBEAT_INTERVAL = 15000;

void setLED(bool isActive) {
  digitalWrite(GREEN_LED, isActive ? HIGH : LOW);
  digitalWrite(RED_LED,   isActive ? LOW  : HIGH);
}

void showOLED(const char* line1, const char* line2 = "") {
  display.clearDisplay();
  display.setTextSize(2);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 10);
  display.println(line1);
  if (strlen(line2) > 0) display.println(line2);
  display.display();
}

void runServo() {
  myServo.attach(servoPin, 544, 2400);
  delay(200);
  myServo.write(120); delay(700);
  myServo.write(90);  delay(300);
  myServo.write(60);  delay(700);
  myServo.write(90);  delay(300);
  myServo.detach();
}

void doFeed() {
  setLED(true);
  showOLED("Feeding...");
  runServo();
  setLED(false);
  showOLED("Automatic", "Pet Feeder");

  if (firebaseReady) {
    FirebaseJson logEntry;
    logEntry.set("timestamp", (int)millis());
    logEntry.set("source", "device");

    if (Firebase.RTDB.pushJSON(&fbdo, "/feedLog", &logEntry)) {
      Serial.println("[Firebase] Feed event logged.");
    } else {
      Serial.printf("[Firebase] Log error: %s\n", fbdo.errorReason().c_str());
    }

    Firebase.RTDB.setBool(&fbdo, "/commands/feed", false);
  }
}

void streamCallback(FirebaseStream data) {
  Serial.printf("[Stream] path: %s, type: %s\n",
                data.dataPath().c_str(), data.dataType().c_str());

  if (data.dataPath() == "/feed" && data.dataType() == "boolean") {
    if (data.boolData() == true) {
      Serial.println("[Firebase] Remote feed command received!");
      feedCommandPending = true;
    }
  }
}

void streamTimeoutCallback(bool timeout) {
  if (timeout) Serial.println("[Stream] Timeout, resuming...");
}

void handleLED() {
  String state = server.arg("state");
  if (state == "on") {
    digitalWrite(2, HIGH);
    server.send(200, "text/plain", "LED turned ON");
  } else {
    digitalWrite(2, LOW);
    server.send(200, "text/plain", "LED turned OFF");
  }
}

void handleData() {
  String json = "{\"sensor\": 100, \"status\": \"online\"}";
  server.send(200, "application/json", json);
}

void handleFeed() {
  server.send(200, "text/plain", "Feeding started!");
  doFeed();
}

void setup() {
  Serial.begin(115200);

  pinMode(2,         OUTPUT);
  pinMode(GREEN_LED, OUTPUT);
  pinMode(RED_LED,   OUTPUT);
  digitalWrite(GREEN_LED, LOW);
  digitalWrite(RED_LED,   LOW);
  delay(200);

  for (int i = 0; i < 3; i++) {
    digitalWrite(GREEN_LED, HIGH); digitalWrite(RED_LED, HIGH); delay(300);
    digitalWrite(GREEN_LED, LOW);  digitalWrite(RED_LED, LOW);  delay(300);
  }
  setLED(false);
  delay(500);

  setLED(true);
  runServo();
  setLED(false);

  Wire.begin(SDA_PIN, SCL_PIN);
  delay(50);
  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println("OLED not found at 0x3C!");
    for (;;);
  }
  showOLED("Connecting...");

  btStop();
  WiFi.disconnect(true);
  delay(1000);
  WiFi.begin(ssid, password);
  Serial.printf("Connecting to %s", ssid);

  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 60) {
    setLED(false);
    delay(500);
    Serial.print(".");
    attempts++;
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.printf("\nConnected! IP: %s\n", WiFi.localIP().toString().c_str());
    for (int i = 0; i < 3; i++) {
      digitalWrite(GREEN_LED, HIGH); delay(200);
      digitalWrite(GREEN_LED, LOW);  delay(200);
    }
  } else {
    Serial.println("\nFailed to connect to WiFi!");
  }
  setLED(false);

  firebaseConfig.api_key      = API_KEY;
  firebaseConfig.database_url = DATABASE_URL;
  firebaseConfig.token_status_callback = tokenStatusCallback;

  auth.user.email    = USER_EMAIL;
  auth.user.password = USER_PASSWORD;

  Firebase.begin(&firebaseConfig, &auth);
  Firebase.reconnectWiFi(true);

  Serial.print("Waiting for Firebase token");
  unsigned long tokenWait = millis();
  while (!Firebase.ready() && millis() - tokenWait < 10000) {
    Serial.print(".");
    delay(500);
  }
  Serial.println();

  if (Firebase.ready()) {
    firebaseReady = true;
    Serial.println("[Firebase] Connected!");

    Firebase.RTDB.setBool(&fbdo, "/status/online", true);
    Firebase.RTDB.setString(&fbdo, "/status/ip", WiFi.localIP().toString().c_str());

    if (!Firebase.RTDB.beginStream(&fbdoStream, "/commands")) {
      Serial.printf("[Stream] Begin error: %s\n", fbdoStream.errorReason().c_str());
    }
    Firebase.RTDB.setStreamCallback(&fbdoStream, streamCallback, streamTimeoutCallback);

  } else {
    Serial.println("[Firebase] Not ready – continuing without Firebase.");
  }

  showOLED("Automatic", "Pet Feeder");

  server.on("/led",  handleLED);
  server.on("/data", handleData);
  server.on("/feed", handleFeed);
  server.begin();
  Serial.println("HTTP server started!");
}

void loop() {
  server.handleClient();

  if (feedCommandPending) {
    feedCommandPending = false;
    doFeed();
  }

  if (firebaseReady && millis() - lastHeartbeat > HEARTBEAT_INTERVAL) {
    lastHeartbeat = millis();
    Firebase.RTDB.setInt(&fbdo, "/status/lastSeen", (int)millis());
    Firebase.RTDB.setBool(&fbdo, "/status/online",  true);
    Serial.println("[Firebase] Heartbeat sent.");
  }
}