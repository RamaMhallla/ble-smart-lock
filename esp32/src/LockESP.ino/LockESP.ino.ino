/*******************************************************
 * ESP32 BLE + HMAC Auth + MQTT (AAA via CSP)
 *******************************************************/

#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>
#include "mbedtls/md.h"

#include <WiFi.h>
#include <PubSubClient.h>

// ===================== SECURITY =====================
const char* SHARED_SECRET = "SUPER_SECRET_KEY";
const char* CORRECT_PIN   = "1234";
const char* OTP_CODE      = "123456";

String currentNonce = "";

// ===================== WIFI + MQTT ==================
const char* WIFI_SSID = "RAMA_WIFI_MOBILE";
const char* WIFI_PASS = "rr99rr99rr";

const char* MQTT_BROKER = "10.146.61.134";
const int   MQTT_PORT   = 1883;

const char* TOPIC_REQUEST = "door_access/request";
const char* TOPIC_RESULT  = "door_access/result";

const char* DEVICE_ID = "ESP32_GARAGE_01";
const char* USER_ID   = "mobile_user_01";

// ===================== BLE UUIDs ====================
#define SERVICE_UUID           "6e400001-b5a3-f393-e0a9-e50e24dcca9e"
#define CHARACTERISTIC_RX_UUID "6e400002-b5a3-f393-e0a9-e50e24dcca9e"
#define CHARACTERISTIC_TX_UUID "6e400003-b5a3-f393-e0a9-e50e24dcca9e"

// ===================== GLOBALS ======================
BLEServer* pServer = nullptr;
BLECharacteristic* pTxCharacteristic = nullptr;
BLECharacteristic* pRxCharacteristic = nullptr;

WiFiClient espClient;
PubSubClient mqttClient(espClient);

bool waitingCSP = false;

// ===================== UTILS ========================
String generateNonce() {
  return String(esp_random(), HEX);
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

void notifyBLE(const String& msg) {
  if (pTxCharacteristic) {
    pTxCharacteristic->setValue(msg.c_str());
    pTxCharacteristic->notify();
  }
}

// ===================== MQTT =========================
void ensureMQTT() {
  while (!mqttClient.connected()) {
    String clientId = "ESP32_" + String(esp_random(), HEX);
    Serial.print("Connecting to MQTT...");
    if (mqttClient.connect(clientId.c_str())) {
      Serial.println(" connected ✅");
      mqttClient.subscribe(TOPIC_RESULT);
    } else {
      Serial.println(" failed, retrying...");
      delay(1000);
    }
  }
}

void publishAccessRequest() {
  ensureMQTT();

  String payload =
    "{"
    "\"device_id\":\"" + String(DEVICE_ID) + "\","
    "\"user_id\":\"" + String(USER_ID) + "\","
    "\"otp\":\"" + String(OTP_CODE) + "\""
    "}";

  Serial.println("MQTT OUT -> " + payload);
  mqttClient.publish(TOPIC_REQUEST, payload.c_str());
}

void mqttCallback(char* topic, byte* payload, unsigned int length) {
  if (!waitingCSP) return;

  String msg;
  for (unsigned int i = 0; i < length; i++) {
    msg += (char)payload[i];
  }

  Serial.println("MQTT IN -> " + msg);

  if (msg.indexOf("\"result\":\"OK\"") >= 0) {
    notifyBLE("OK");
  } else {
    notifyBLE("WRONG");
  }

  waitingCSP = false;
}

// ===================== BLE CALLBACKS ================

// 🔹 Server callbacks (اتصال / فصل)
class MyServerCallbacks : public BLEServerCallbacks {
  void onConnect(BLEServer* pServer) override {
    Serial.println("Client connected");
    currentNonce = "";   // فقط reset
  }

  void onDisconnect(BLEServer* pServer) override {
    Serial.println("Client disconnected");
    currentNonce = "";
  }
};


// 🔹 RX characteristic (استقبال HMAC)
class MyRXCallbacks : public BLECharacteristicCallbacks {
  void onWrite(BLECharacteristic* c) override {
    String received = c->getValue();
    received.trim();

    Serial.println("BLE RX -> " + received);

    // 🟢 أول رسالة من Flutter → أرسل nonce
    if (currentNonce == "") {
      currentNonce = generateNonce();
      Serial.println("Nonce -> " + currentNonce);
      notifyBLE(currentNonce);
      return;
    }

    // 🟢 تحقق HMAC
    String expected = computeHMAC(String(CORRECT_PIN) + currentNonce);

    if (received.equalsIgnoreCase(expected)) {
      notifyBLE("PENDING");
      waitingCSP = true;
      publishAccessRequest();
    } else {
      notifyBLE("WRONG");
    }

    currentNonce = ""; // nonce one-time
  }
};


// ===================== SETUP ========================
void setup() {
  Serial.begin(115200);

  // WiFi
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nWiFi connected");

  mqttClient.setServer(MQTT_BROKER, MQTT_PORT);
  mqttClient.setCallback(mqttCallback);

  // BLE init
  BLEDevice::init("ESP32_GARAGE_01");

  pServer = BLEDevice::createServer();
  pServer->setCallbacks(new MyServerCallbacks());

  BLEService* service = pServer->createService(SERVICE_UUID);

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

  BLEAdvertising* adv = BLEDevice::getAdvertising();
  adv->setScanResponse(true);
  adv->addServiceUUID(SERVICE_UUID);
  adv->start();

  Serial.println("BLE Ready");
}

// ===================== LOOP =========================
void loop() {
  if (WiFi.status() == WL_CONNECTED) {
    if (!mqttClient.connected()) ensureMQTT();
    mqttClient.loop();
  }
}
