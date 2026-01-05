/*******************************************************
 * ESP32 BLE + HMAC Auth + MQTT (AAA via CSP)
 *******************************************************/

#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
// for notify to work correctly on mobile (Client Characteristic Configuration).
#include <BLE2902.h>

//ESP32 encryption library for HMAC-SHA256 extraction
#include "mbedtls/md.h"

#include <WiFi.h>
//MQTT client
#include <PubSubClient.h>

// ===================== SECURITY =====================
//A secret key shared between Flutter and ESP32 (must not be revealed).
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
//This is a “UART over BLE” model:
// RX: From mobile → ESP32 (Write)
// TX: From ESP32 → Mobile (Notify)
#define SERVICE_UUID           "6e400001-b5a3-f393-e0a9-e50e24dcca9e"
#define CHARACTERISTIC_RX_UUID "6e400002-b5a3-f393-e0a9-e50e24dcca9e"
#define CHARACTERISTIC_TX_UUID "6e400003-b5a3-f393-e0a9-e50e24dcca9e"

// ===================== GLOBALS ======================
BLEServer* pServer = nullptr;
BLECharacteristic* pTxCharacteristic = nullptr;//To send a notification to the mobile device.
BLECharacteristic* pRxCharacteristic = nullptr;//To receive a write request from the mobile device.

WiFiClient espClient;
PubSubClient mqttClient(espClient);//An MQTT client using espClient for network connectivity.

//waitingCSP: If true, it means "I sent an MQTT request and I'm waiting for a CSP response."
bool waitingCSP = false;

// ===================== UTILS ========================
//esp_random() generates a random number from the hardware.
It is converted to a HEX string.
String generateNonce() {
  return String(esp_random(), HEX);
}
//
String computeHMAC(const String& msg) {
  byte hmacResult[32];//SHA256 output is 32 bytes
  mbedtls_md_context_t ctx;//The context for mbedTLS encryption.

  mbedtls_md_init(&ctx);//Initialization
  mbedtls_md_setup(&ctx, mbedtls_md_info_from_type(MBEDTLS_MD_SHA256), 1);//Selects SHA256 and activates HMAC (1).
  mbedtls_md_hmac_starts(
    &ctx,
    (const unsigned char*)SHARED_SECRET,
    strlen(SHARED_SECRET)
  );//Starts HMAC with the SHARED_SECRET key.
  
  mbedtls_md_hmac_update(
    &ctx,
    (const unsigned char*)msg.c_str(),
    msg.length()
  );//Adds the msg message data (e.g., "1234" + nonce)

  mbedtls_md_hmac_finish(&ctx, hmacResult);//Displays the result and stores it in hmacResult.
  mbedtls_md_free(&ctx);//Cleans ups

//Then it converts the bytes into a hex string of 64 characters long
  char hex[65];
  for (int i = 0; i < 32; i++) {
    sprintf(hex + i * 2, "%02x", hmacResult[i]);
  }
  hex[64] = '\0';
  return String(hex);
}
//so the final output is HMAC_HEX = HMAC_SHA256(secret, msg)

void notifyBLE(const String& msg) {
  //If TX exists:
    // Sets the value
    // Sends it to the mobile device via notify
  if (pTxCharacteristic) {
    pTxCharacteristic->setValue(msg.c_str());
    pTxCharacteristic->notify();
  }
}

// ===================== MQTT =========================
void ensureMQTT() {
  //While not connected:
      // Generates a random clientId
      // Attempts to connect
      // If successful: Subscribes to door_access/result
      // If unsuccessful: Waits a second and re-enters
  while (!mqttClient.connected()) {
    String clientId = "ESP32_" + String(esp_random(), HEX);
    Serial.print("Connecting to MQTT...");
    if (mqttClient.connect(clientId.c_str())) {
      Serial.println(" connected ");
      mqttClient.subscribe(TOPIC_RESULT);
    } else {
      Serial.println(" failed, retrying...");
      delay(1000);
    }
  }
}
//MQTT is checked to ensure it's working.
// It generates a JSON payload containing:
      // device_id
      // user_id
      // otp
// It prints the payload
// It publishes it to door_access/request
// In a CSP service, you usually also have an event and timestamp... but here, ESP32 is only sending 3 fields.
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

//Receiving an MQTT callback
void mqttCallback(char* topic, byte* payload, unsigned int length) {
  if (!waitingCSP) return; //ignore any MQTT messages (because it's not waiting for anything).

  String msg;
  //Converts the payload from bytes -> String.
  for (unsigned int i = 0; i < length; i++) {
    msg += (char)payload[i];
  }

  Serial.println("MQTT IN -> " + msg);
  //If the message contains "result":"OK":
    // Sends OK to the mobile device.
  // Otherwise:
    // Sends WRONG.
  // Finally, waitingCSP = false means we're done.
  if (msg.indexOf("\"result\":\"OK\"") >= 0) {
    notifyBLE("OK");
  } else {
    notifyBLE("WRONG");
  }

  waitingCSP = false;
}

// ===================== BLE CALLBACKS ================
// Server callbacks:
class MyServerCallbacks : public BLEServerCallbacks {
  //Upon connection:
        // Prints log
        // Resets nonce
  // Upon disconnection:
        // Same thing
  void onConnect(BLEServer* pServer) override {
    Serial.println("Client connected");
    currentNonce = "";   // only reset
  }

  void onDisconnect(BLEServer* pServer) override {
    Serial.println("Client disconnected");
    currentNonce = "";
  }
};


//  RX characteristic (receive HMAC)
//1- Received from Flutter (the characteristic RX)
//2-  If currentNonce is empty:
          // This means: “You haven’t given a nonce yet.”
          // Generates a nonce
          // Sends it to the mobile device via notify
          // Return → Does not complete

//3- If a nonce exists:
          // Calculates expected = HMAC(CORRECT_PIN + currentNonce)
          // Compares received with expected (case-insensitive)

//4-If true:
  // Sends 'PENDING' to the mobile device
  // waitingCSP = true
  // Sends an MQTT request (OTP) to the CSP

//5- If false:
  // Sends `WRONG`

// Finally:
// currentNonce = "" because the nonce is one-time to prevent replay.

// Expected message sequence from Flutter:
// First message: Anything (even “HELLO”) just to request a nonce
// Second message: HMAC(pin + nonce)
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
  //Trying to connect
  // Keeps waiting until it becomes Connected
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nWiFi connected");

  //MQTT configuration:
  // Specifies broker/port
  //Specifies message callback
  mqttClient.setServer(MQTT_BROKER, MQTT_PORT);
  mqttClient.setCallback(mqttCallback);

  // BLE init
  BLEDevice::init("ESP32_GARAGE_01");//The device's name is BLE scanning

  //Create a BLE server and set up callbacks for connection/disconnection.
  pServer = BLEDevice::createServer();
  pServer->setCallbacks(new MyServerCallbacks());

  //Create a service with the specified UUID
  BLEService* service = pServer->createService(SERVICE_UUID);


  //TX characteristic (notify):
  //TX only notify.
  // Adding BLE2902 is important for the mobile phone to be able to enable notifications.
  pTxCharacteristic = service->createCharacteristic(
    CHARACTERISTIC_TX_UUID,
    BLECharacteristic::PROPERTY_NOTIFY
  );
  pTxCharacteristic->addDescriptor(new BLE2902());


  //RX characteristic (write):
  //RX is used for writing from Flutter.
  //It connects to the onWrite callbacks .
  pRxCharacteristic = service->createCharacteristic(
    CHARACTERISTIC_RX_UUID,
    BLECharacteristic::PROPERTY_WRITE
  );
  pRxCharacteristic->setCallbacks(new MyRXCallbacks());

 //Activate the service
  service->start();
  //The advertisement is designed to be displayed on mobile devices.
  //A service UUID is added to the advertisement to make it easier to discover.
  BLEAdvertising* adv = BLEDevice::getAdvertising();
  adv->setScanResponse(true);
  adv->addServiceUUID(SERVICE_UUID);
  adv->start();

  Serial.println("BLE Ready");
}

// ===================== LOOP =========================
//If Wi-Fi is on:
    // If MQTT is disconnected: Reconnect it
    // mqttClient.loop() is required to receive MQTT messages and run callback.
void loop() {
  if (WiFi.status() == WL_CONNECTED) {
    if (!mqttClient.connected()) ensureMQTT();
    mqttClient.loop();
  }
}

//TO DO:
// HMAC here provides protection against MITM + replay (because it's a one-time occurrence).
// However, the OTP is static, meaning the CSP will always accept if it's verifying "123456".
// In OUR service (Flask), the verification is actually otp_code == "123456".

// MQTT on port 1883 means without TLS → anyone on the network can see the messages. MQTTS 8883 and certificates are better.
// There needs to be a timeout for waiting for the CSP (if the CSP doesn't respond, it shouldn't remain "pending" indefinitely).
