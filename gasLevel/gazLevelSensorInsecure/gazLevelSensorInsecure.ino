#include <WiFi.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include <time.h>

// ===================== TIME =====================
const char* NTP_SERVER = "pool.ntp.org";
const long  GMT_OFFSET_SEC = 0;
const int   DAYLIGHT_OFFSET_SEC = 0;

// ===================== WIFI + MQTT ==================
const char* WIFI_SSID = "Vincenzo's Galaxy S21 5G";
const char* WIFI_PASS = "pbys2426767";

// Changed to standard MQTT Port
const char* MQTT_BROKER = "10.253.152.211";
const int   MQTT_PORT   = 1883; 

const char* TOPIC_REQUEST = "home/sensor/gas";
const char* TOPIC_EMERGENCY = "door_access/result";
const char* DEVICE_ID     = "GARAGE02";

WiFiClient espClient; // Standard Client (No SSL)
PubSubClient mqttClient(espClient);

#define GAS_PIN 32
#define THRESHOLD 800 

// ===================== HELPERS =====================

String getISOTimestamp() {
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo)) return "1970-01-01T00:00:00Z";
  char buf[30];
  strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", &timeinfo);
  return String(buf);
}

void ensureMQTT() {
  while (!mqttClient.connected()) {
    Serial.print("Attempting MQTT connection...");
    
    // Connect with just ID (No User/Pass, No Certs)
    if (mqttClient.connect(DEVICE_ID)) { 
      Serial.println("connected");
    } else {
      Serial.print("failed, rc=");
      Serial.print(mqttClient.state());
      Serial.println(" try again in 2 seconds");
      delay(2000);
    }
  }
}

void publishAccessRequest(const String& msg) {
  if (!mqttClient.connected()) {
    ensureMQTT();
  }
  
  // Create PLAIN JSON
  StaticJsonDocument<256> doc;
  doc["device_id"] = DEVICE_ID;
  doc["event"]     = "home/sensor/gas";
  doc["gas_level"] = msg;
  doc["timestamp"] = getISOTimestamp();

  String jsonString;
  serializeJson(doc, jsonString);

  Serial.println("Send MQTT -> " + jsonString);
  mqttClient.publish(TOPIC_REQUEST, jsonString.c_str());
}

void publishEmergency(const String& msg) {
  if (!mqttClient.connected()) {
    ensureMQTT();
  }
  
  // Create PLAIN JSON Response structure
  StaticJsonDocument<256> doc;
  doc["authorized"] = true;
  doc["reason"]     = "home/sensor/gas";
  doc["value"]      = msg; 

  String jsonString;
  serializeJson(doc, jsonString);
  
  // Send directly (no wrapping in "responce")
  Serial.println("Send Emergency -> " + jsonString);
  mqttClient.publish(TOPIC_EMERGENCY, jsonString.c_str());
}

void setup() {
  Serial.begin(115200);

  // 1. WiFi
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  while (WiFi.status() != WL_CONNECTED) { delay(500); Serial.print("."); }
  Serial.println("\nWiFi Connected");
  
  configTime(GMT_OFFSET_SEC, DAYLIGHT_OFFSET_SEC, NTP_SERVER);
  
  // 2. MQTT (Standard)
  mqttClient.setServer(MQTT_BROKER, MQTT_PORT);
  // Buffer size can be smaller now since we aren't encrypting
  mqttClient.setBufferSize(512); 
}

void loop() {
  if (!mqttClient.connected()) {
    ensureMQTT();
  }
  mqttClient.loop(); 

  int gasValue = int(random(300,1000));//analogRead(GAS_PIN);

  if (gasValue > THRESHOLD) {
    Serial.println("\n!!! GAS LEAK DETECTED !!!");
    publishEmergency(String(gasValue));
  }
  
  publishAccessRequest(String(gasValue));
  
  delay(2000); 
}