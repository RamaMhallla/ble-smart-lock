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
#include "WiFiClientSecure.h"
#include <WiFi.h>
//MQTT client
#include <PubSubClient.h>
#include <time.h>
const char* NTP_SERVER = "pool.ntp.org";
const long  GMT_OFFSET_SEC = 0;
const int   DAYLIGHT_OFFSET_SEC = 0;


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
const int   MQTT_PORT   = 8883;

const char* TOPIC_REQUEST = "door_access/request";
const char* TOPIC_RESULT  = "door_access/result";

const char* DEVICE_ID = "ESP32_GARAGE_01";
const char* USER_ID   = "mobile_user_01";

const char* CA_cert = \
"-----BEGIN CERTIFICATE-----\n" \
"MIIEBTCCAu2gAwIBAgIUGzedoEDTi0biAqaF5pM75KBeHSYwDQYJKoZIhvcNAQEL\n" \
"BQAwgZExCzAJBgNVBAYTAklUMREwDwYDVQQIDAhDYWxhYnJpYTEQMA4GA1UEBwwH\n" \
"Q29zZW56YTEUMBIGA1UECgwLSW90U2VjdXJpdHkxDDAKBgNVBAsMA2NzcDETMBEG\n" \
"A1UEAwwKY3NwLXNlcnZlcjEkMCIGCSqGSIb3DQEJARYVdmRhbWljb3dvcmtAZ21h\n" \
"aWwuY29tMB4XDTI2MDEwNDExNDczNloXDTMxMDEwMzExNDczNlowgZExCzAJBgNV\n" \
"BAYTAklUMREwDwYDVQQIDAhDYWxhYnJpYTEQMA4GA1UEBwwHQ29zZW56YTEUMBIG\n" \
"A1UECgwLSW90U2VjdXJpdHkxDDAKBgNVBAsMA2NzcDETMBEGA1UEAwwKY3NwLXNl\n" \
"cnZlcjEkMCIGCSqGSIb3DQEJARYVdmRhbWljb3dvcmtAZ21haWwuY29tMIIBIjAN\n" \
"BgkqhkiG9w0BAQEFAAOCAQ8AMIIBCgKCAQEAq2XXRC/ZHmlsC3tSkBBkKSYajRmR\n" \
"QYfy47WFTsS6cy0IDnFyCRsoR3FxX0hQZv5yHbPrqeexPlhC9ozUyB4m0sJ48IvU\n" \
"xTBiwJsUfqUYPkKU3GKIghxSFJdk4PLTlW8VhleMheVbanU197Z1WsP8/JSom3+B\n" \
"jctqgOTPxc5yqamAqTG89NhtxAl7a7OJNen8/YfgrtGWEs5UhJzcYUgwjCdrK29D\n" \
"PyJLu+AJYrOm2igH2yy2ERrLe6IiZ/md/SKf+rMDOuHtkNcn10aENLOvaRe+sntx\n" \
"6kHzmgsHh5PhSg9zUo/PVpZsVbGvDOblgfbAF3IP/abue5NN5lSFcIF4qQIDAQAB\n" \
"o1MwUTAdBgNVHQ4EFgQUR8VObiwy9iDgsuMZNsplzx8hdwMwHwYDVR0jBBgwFoAU\n" \
"R8VObiwy9iDgsuMZNsplzx8hdwMwDwYDVR0TAQH/BAUwAwEB/zANBgkqhkiG9w0B\n" \
"AQsFAAOCAQEAQtU92CobwSmpkBgkk29rG3DK/nT5NKh34bwgmR7ZfzNssxDsxCaF\n" \
"qgCvYvoL1BwrIk+rUUu7M+quQEee6OYqToHdRqpAMG4w5VGX+OKjAuS6P+TOmhT/\n" \
"2XblNXgMtKmvYnhkYqgu+tGRM4P+8/fYXoKiIaVQhUKtFFLas8ubpvqQHzxGDXa2\n" \
"6pomTYyLMaLBLZtbmKdHgcesrYhVogiepK13+KVhlROKNBe5iVwrWBLmMynCadVq\n" \
"WrdMj3NXcWEYYEaPtdHn7LFYJ5HezpwxIiBWuu0bhMhu1NyrzCtN8BS2vVdj+vdq\n" \
"JPHGicBDtdH/d4x29cpyUZQntNl3cPBpQw==\n" \
"-----END CERTIFICATE-----";

const char* ESP_CA_cert = \
"-----BEGIN CERTIFICATE-----\n" \
"MIID9zCCAt+gAwIBAgIUNZpJEPmyE5KriRUmNQkfV6B9CX4wDQYJKoZIhvcNAQEL\n" \
"BQAwgZExCzAJBgNVBAYTAklUMREwDwYDVQQIDAhDYWxhYnJpYTEQMA4GA1UEBwwH\n" \
"Q29zZW56YTEUMBIGA1UECgwLSW90U2VjdXJpdHkxDDAKBgNVBAsMA2NzcDETMBEG\n" \
"A1UEAwwKY3NwLXNlcnZlcjEkMCIGCSqGSIb3DQEJARYVdmRhbWljb3dvcmtAZ21h\n" \
"aWwuY29tMB4XDTI2MDEwOTE2Mjc0NVoXDTI3MDEwOTE2Mjc0NVowgZQxCzAJBgNV\n" \
"BAYTAklUMREwDwYDVQQIDAhDYWxhYnJpYTEQMA4GA1UEBwwHQ29zZW56YTEUMBIG\n" \
"A1UECgwLSW90U2VjdXJpdHkxDzANBgNVBAsMBkNsaWVudDETMBEGA1UEAwwKSW90\n" \
"LUNsaWVudDEkMCIGCSqGSIb3DQEJARYVdmRhbWljb3dvcmtAZ21haWwuY29tMIIB\n" \
"IjANBgkqhkiG9w0BAQEFAAOCAQ8AMIIBCgKCAQEAxL7D7bj6MkacYbzItt/GM88A\n" \
"QwQ7Tf/9RbJtoD1F2Gw7GGQ/Kym0jHKSI1SMWYe214AfThOHD6sW+A2yBDfu/Ft2\n" \
"GufCGU3igoQgcid7bXQk51DNGscqQwnEz8Lhtt0cc87JlofEeT7/xgPsXU1aWaGv\n" \
"bdbfY+xun+hgWfHeGXzq4GJNcoVIMGVtOjJ+C7n5qOOmsiKqANH5gfrA2U+ZKPp8\n" \
"xQsoIwryfq6zi+5kenVkQFCNed5ALKAivOFLHdSndJVzpizIpw/9Xb/oOTNoXorD\n" \
"00yh2diOMDIWz9vCLDSFe+w3rEW77BXP3tAPvTsv/H9FUEA4xUWlUGKIPIHJEwID\n" \
"AQABo0IwQDAdBgNVHQ4EFgQU8IOeTOZCESzd9ysE4M1hJHAcV+kwHwYDVR0jBBgw\n" \
"FoAUR8VObiwy9iDgsuMZNsplzx8hdwMwDQYJKoZIhvcNAQELBQADggEBACe1KOfE\n" \
"gG6U/GNuUKVbgwdK7lNBT8Uw8huv+CLFDAZh+HDMUK9P/2GnflWjInLB4MAIN4JV\n" \
"tZbZv/bkPnsVtzimR0VQtn/T/taD+UY14sKT9qsFSzrUMLzvQ+yZA0ZSH/y7AI2I\n" \
"ekW1EjS+hXjs/bsLJoMmu+XeEZRJV9Wpr/xpEbipUY5R81wMWuuLHHSdTbTSKJ5W\n" \
"NpCkw/0PfDawoffH0jhT7j7Jnvuz4UmKNGWDjT/lLybyMDYACILMrKqysKm+J9P0\n" \
"4FFxUa5VEgX0YexMEF7gbCI1lXPQCGzp9JO03v3ABvG/mrvXIfxfnQqRGCz6rF/L\n" \
"gtpBGKozlgIJxB8=\n" \
"-----END CERTIFICATE-----";

const char* ESP_RSA_key= \
"-----BEGIN PRIVATE KEY-----\n" \
"MIIEugIBADANBgkqhkiG9w0BAQEFAASCBKQwggSgAgEAAoIBAQDEvsPtuPoyRpxh\n" \
"vMi238YzzwBDBDtN//1Fsm2gPUXYbDsYZD8rKbSMcpIjVIxZh7bXgB9OE4cPqxb4\n" \
"DbIEN+78W3Ya58IZTeKChCByJ3ttdCTnUM0axypDCcTPwuG23RxzzsmWh8R5Pv/G\n" \
"A+xdTVpZoa9t1t9j7G6f6GBZ8d4ZfOrgYk1yhUgwZW06Mn4Lufmo46ayIqoA0fmB\n" \
"+sDZT5ko+nzFCygjCvJ+rrOL7mR6dWRAUI153kAsoCK84Usd1Kd0lXOmLMinD/1d\n" \
"v+g5M2heisPTTKHZ2I4wMhbP28IsNIV77DesRbvsFc/e0A+9Oy/8f0VQQDjFRaVQ\n" \
"Yog8gckTAgMBAAECggEAUY257aqFm52FaUY19QghQoyF0UHJy3VXaTKjGo8LisCi\n" \
"ZmP3g07QVn+PcDG1087bzcyALX8Ot0H2TXBv4CvHVrga5uA2pwKP6AYY5PjUwvQn\n" \
"7/Kgcn8oV42PFYf0xDY3exG2oj05BgFFSLGLoslTsF/DNkahZuw0lvheKCqIJAvX\n" \
"x0uvW3vgDh0VaBTiPPQo5SvgGxVA1Ks1RfXYBEv444tDZO1WJ+8mq+DGtoveHuyu\n" \
"1S1bWgoVzHvH+lbi2gg/DgH9uQxt0B7NhrFGuk1+R3c9Z94uA3+B8NWpiNHsF+Fa\n" \
"gkl+nz8LzeUeSd6fy7Z7KXs9+hbu6nYUb/U6+BJq4QKBgQDivYlTL/M0Sr7yYDyg\n" \
"VHHJ6ThyGtowjbd1QnUx7sQ715j0K1HRGZlcuKm+1cTvzFVHkkdPXng3A/JRVGon\n" \
"PU2TZF8AcCX0+gvuKslNujBsQjIqPiva4j4gn8jz1bvvMFoKiyWPA9SoYa/S4XsL\n" \
"n14qIT50gqfoSkrjk9ppI6wAmwKBgQDeIlMtCrviiWIRyWjxgcV26+7uCYpKDMhJ\n" \
"jBsSiv1g0/iVi/CbuPkgqrcS/IM8rZJAlbi5DWGz1TC5vCg8KYy9uPDZrEDGBn77\n" \
"wO5MIkgqv4wtG2rdwaEzCmlgz08QsOdeM71bz7jfs8ckrBew2Ne8yWHg56IRVFo6\n" \
"SDXrq3106QKBgByFoySnv3wwetyaZoX0mWvAvqz7276H1TAW8A8b7etpL4Bngp8/\n" \
"DR+wywmKcn+HwKKEMBw30f95q523dLMC7yM/WQQBF4U9fwqyryfr5/N2UEEoGPQr\n" \
"yYzpDKo/lKh9+JWi81KONM4Jm8h3PLc1kO7Tx7t4RA7gaZM/IhZful9JAn9dth1g\n" \
"4yZga5Tz7ARZ3mVvxhkGUwAEPWBBptnE+N3r+4DjliXrjB2NqneRivXSo2cP2BoV\n" \
"949ATrA/qyFOQDkf0OXK7uBkqljn3HyrocrQPf7lCKwM4aMf5USPkuXIJNl25Fz+\n" \
"XqOfvDhHQFK+SLy66Dpip1W3+d4WuGAHDFHhAoGAcRXQq3RLiIFCy0+KQrG9TOlN\n" \
"INVubO9cj7cUiNT+uP+Tvdgas7TyGmpfnTwRoZ6IjCIT3opM+AVZ2xZE07+jrHtS\n" \
"cQqeh+8fZqUpKKY6RcbNi5DP/DmFKspxPvG4JbmbNqG26pwASRZIcdOAVbsTjBYA\n" \
"R4jxmJrEZLZwvAJ9rqs=\n" \
"-----END PRIVATE KEY-----";
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

WiFiClientSecure espClient;
PubSubClient mqttClient(espClient);//An MQTT client using espClient for network connectivity.

//waitingCSP: If true, it means "I sent an MQTT request and I'm waiting for a CSP response."
bool waitingCSP = false;

// ===================== UTILS ========================
//esp_random() generates a random number from the hardware.
//It is converted to a HEX string.
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

  // ✅ added: generate ISO timestamp for Flask
  String timestamp = getISOTimestamp();

  String payload =
    "{"
      "\"device_id\":\"" + String(DEVICE_ID) + "\","
      "\"user_id\":\""   + String(USER_ID)   + "\","
      "\"event\":\"door_access/request\","
      "\"otp\":\""       + String(OTP_CODE)  + "\","
      "\"timestamp\":\"" + timestamp         + "\""
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
   if (msg.indexOf("\"result\":\"OK\"") >= 0 ||
      msg.indexOf("\"authorized\":true") >= 0) {
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

  // ✅ added: init NTP so getLocalTime() works
  configTime(GMT_OFFSET_SEC, DAYLIGHT_OFFSET_SEC, NTP_SERVER);

  //MQTT configuration:
  // Specifies broker/port
  //Specifies message callback
  espClient.setCACert(CA_cert);          //Root CA certificate
  espClient.setCertificate(ESP_CA_cert); //for client verification if the require_certificate is set to true in the mosquitto broker config
  espClient.setPrivateKey(ESP_RSA_key);  //for client verification if the require_certificate is set to true in the mosquitto broker config
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
