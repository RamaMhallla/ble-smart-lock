/*******************************************************
 * ESP32 BLE + Nonce + HMAC + MQTT -> CSP AAA
 * Stable version (MQTT plaintext on port 1883)
 *******************************************************/

#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>

#include "mbedtls/md.h"
#include <WiFi.h>
#include <WiFiClient.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include <time.h>

// ===================== TIME =====================
const char* NTP_SERVER = "pool.ntp.org";
const long  GMT_OFFSET_SEC = 0;
const int   DAYLIGHT_OFFSET_SEC = 0;

// ===================== SECURITY =====================
const char* SHARED_SECRET = "SUPER_SECRET_KEY";
const char* CORRECT_PIN   = "1234";

// ===================== WIFI + MQTT ==================
const char* WIFI_SSID = "RAMA_WIFI_MOBILE";
const char* WIFI_PASS = "rr99rr99rr";

const char* MQTT_BROKER = "10.146.61.134";
const int   MQTT_PORT   = 1883;

const char* TOPIC_REQUEST = "door_access/request";
const char* TOPIC_RESULT  = "door_access/result";

const char* DEVICE_ID = "ESP32_GARAGE_01";
const char* USER_ID   = "mobile_user_01";

// ===================== BLE UUIDs =====================
#define SERVICE_UUID           "6e400001-b5a3-f393-e0a9-e50e24dcca9e"
#define CHARACTERISTIC_RX_UUID "6e400002-b5a3-f393-e0a9-e50e24dcca9e"
#define CHARACTERISTIC_TX_UUID "6e400003-b5a3-f393-e0a9-e50e24dcca9e"

// ===================== GLOBALS ======================
BLECharacteristic* pTxCharacteristic = nullptr;
BLECharacteristic* pRxCharacteristic = nullptr;

WiFiClient espClient;
PubSubClient mqttClient(espClient);

String currentNonce = "";

// ===================== UTILS ========================
String generateNonce() {
  return String((uint32_t)esp_random(), HEX);
}

String computeHMAC(const String& msg) {
  byte hmacResult[32];
  mbedtls_md_context_t ctx;

  mbedtls_md_init(&ctx);
  mbedtls_md_setup(&ctx, mbedtls_md_info_from_type(MBEDTLS_MD_SHA256), 1);

  mbedtls_md_hmac_starts(
    &ctx,
    (const unsigned char*)SHARED_SECRET,
    strlen(SHARED_SECRET)
  );

  mbedtls_md_hmac_update(
    &ctx,
    (const unsigned char*)msg.c_str(),
    msg.length()
  );

  mbedtls_md_hmac_finish(&ctx, hmacResult);
  mbedtls_md_free(&ctx);

  char hex[65];
  for (int i = 0; i < 32; i++) {
    sprintf(hex + i * 2, "%02x", hmacResult[i]);
  }
  hex[64] = '\0';

  return String(hex);
}

String getISOTimestamp() {
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo)) {
    return "1970-01-01T00:00:00Z";
  }

  char buf[30];
  strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", &timeinfo);
  return String(buf);
}

void notifyBLE(const String& msg) {
  if (!pTxCharacteristic) return;
  pTxCharacteristic->setValue(msg.c_str());
  pTxCharacteristic->notify();
}

// ===================== MQTT =========================
void ensureMQTT() {
  while (!mqttClient.connected()) {
    String clientId = "ESP32_" + String((uint32_t)esp_random(), HEX);
    Serial.print("Connecting to MQTT... ");

    if (mqttClient.connect(clientId.c_str())) {
      Serial.println("connected");
      mqttClient.subscribe(TOPIC_RESULT);
    } else {
      Serial.print("failed, rc=");
      Serial.println(mqttClient.state());
      delay(2000);
    }
  }
}

void publishAccessRequest(const String& hmac) {
  ensureMQTT();

  String payload =
    "{"
    "\"device_id\":\"" + String(DEVICE_ID) + "\","
    "\"user_id\":\""   + String(USER_ID)   + "\","
    "\"event\":\"door_access\","
    "\"otp\":\""       + hmac              + "\","
    "\"timestamp\":\"" + getISOTimestamp() + "\""
    "}";

  Serial.println("MQTT OUT -> " + payload);
  mqttClient.publish(TOPIC_REQUEST, payload.c_str());
}

// ===================== MQTT CALLBACK =================
void mqttCallback(char* topic, byte* payload, unsigned int length) {
  if (String(topic) != TOPIC_RESULT) return;

  String msg;
  for (unsigned int i = 0; i < length; i++) {
    msg += (char)payload[i];
  }

  Serial.println("MQTT IN -> " + msg);

  StaticJsonDocument<256> doc;
  if (deserializeJson(doc, msg)) return;

  const char* decision = doc["decision"];
  if (decision && String(decision) == "ALLOW") {
    notifyBLE("OK");
  } else {
    notifyBLE("WRONG");
  }
}

// ===================== BLE RX CALLBACK =================
class MyRXCallbacks : public BLECharacteristicCallbacks {
  void onWrite(BLECharacteristic* c) override {
    String received = c->getValue();
    received.trim();

    Serial.println("BLE RX -> " + received);

    if (currentNonce == "") {
      currentNonce = generateNonce();
      notifyBLE(currentNonce);
      return;
    }

    String expected = computeHMAC(String(CORRECT_PIN) + currentNonce);

    if (received.equalsIgnoreCase(expected)) {
      notifyBLE("PENDING");
      publishAccessRequest(received);
    } else {
      notifyBLE("WRONG");
    }

    currentNonce = "";
  }
};

// ===================== SETUP ========================
void setup() {
  Serial.begin(115200);

  WiFi.begin(WIFI_SSID, WIFI_PASS);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
  }

  Serial.println("WiFi connected");
  Serial.println(WiFi.localIP());

  configTime(GMT_OFFSET_SEC, DAYLIGHT_OFFSET_SEC, NTP_SERVER);

  mqttClient.setServer(MQTT_BROKER, MQTT_PORT);
  mqttClient.setCallback(mqttCallback);

  BLEDevice::init(DEVICE_ID);
  BLEServer* server = BLEDevice::createServer();
  BLEService* service = server->createService(SERVICE_UUID);

  pTxCharacteristic = service->createCharacteristic(
    CHARACTERISTIC_TX_UUID,
    BLECharacteristic::PROPERTY_NOTIFY
  );
  pTxCharacteristic->addDescriptor(new BLE2902());

  pRxCharacteristic = service->createCharacteristic(
    CHARACTERISTIC_RX_UUID,
    BLECharacteristic::PROPERTY_WRITE
  );
  pRxCharacteristic->setCallbacks(new MyRXCallbacks());

  service->start();

  BLEAdvertising* advertising = BLEDevice::getAdvertising();
  advertising->addServiceUUID(SERVICE_UUID);
  advertising->setScanResponse(true);
  advertising->setMinPreferred(0x06);
  advertising->setMinPreferred(0x12);

  BLEDevice::startAdvertising();

  Serial.println("BLE ready, advertising started");
}

// ===================== LOOP =========================
void loop() {
  if (!mqttClient.connected()) {
    ensureMQTT();
  }
  mqttClient.loop();
}
