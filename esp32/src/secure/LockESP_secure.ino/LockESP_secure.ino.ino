/*******************************************************
 * ESP32 BLE + Nonce + HMAC + MQTTS (Secure) -> CSP AAA
 * Secure version (MQTT over TLS on port 8883)
 *******************************************************/

#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>

#include "mbedtls/md.h"
#include <WiFi.h>
#include <WiFiClientSecure.h> // <--- CHANGED: Added Secure Client
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

// ===================== CERTIFICATES =================

// 1. The CA Certificate (The server's public authority)
const char* root_ca = R"EOF(
-----BEGIN CERTIFICATE-----
MIIEBTCCAu2gAwIBAgIUGzedoEDTi0biAqaF5pM75KBeHSYwDQYJKoZIhvcNAQEL
BQAwgZExCzAJBgNVBAYTAklUMREwDwYDVQQIDAhDYWxhYnJpYTEQMA4GA1UEBwwH
Q29zZW56YTEUMBIGA1UECgwLSW90U2VjdXJpdHkxDDAKBgNVBAsMA2NzcDETMBEG
A1UEAwwKY3NwLXNlcnZlcjEkMCIGCSqGSIb3DQEJARYVdmRhbWljb3dvcmtAZ21h
aWwuY29tMB4XDTI2MDEwNDExNDczNloXDTMxMDEwMzExNDczNlowgZExCzAJBgNV
BAYTAklUMREwDwYDVQQIDAhDYWxhYnJpYTEQMA4GA1UEBwwHQ29zZW56YTEUMBIG
A1UECgwLSW90U2VjdXJpdHkxDDAKBgNVBAsMA2NzcDETMBEGA1UEAwwKY3NwLXNl
cnZlcjEkMCIGCSqGSIb3DQEJARYVdmRhbWljb3dvcmtAZ21haWwuY29tMIIBIjAN
BgkqhkiG9w0BAQEFAAOCAQ8AMIIBCgKCAQEAq2XXRC/ZHmlsC3tSkBBkKSYajRmR
QYfy47WFTsS6cy0IDnFyCRsoR3FxX0hQZv5yHbPrqeexPlhC9ozUyB4m0sJ48IvU
xTBiwJsUfqUYPkKU3GKIghxSFJdk4PLTlW8VhleMheVbanU197Z1WsP8/JSom3+B
jctqgOTPxc5yqamAqTG89NhtxAl7a7OJNen8/YfgrtGWEs5UhJzcYUgwjCdrK29D
PyJLu+AJYrOm2igH2yy2ERrLe6IiZ/md/SKf+rMDOuHtkNcn10aENLOvaRe+sntx
6kHzmgsHh5PhSg9zUo/PVpZsVbGvDOblgfbAF3IP/abue5NN5lSFcIF4qQIDAQAB
o1MwUTAdBgNVHQ4EFgQUR8VObiwy9iDgsuMZNsplzx8hdwMwHwYDVR0jBBgwFoAU
R8VObiwy9iDgsuMZNsplzx8hdwMwDwYDVR0TAQH/BAUwAwEB/zANBgkqhkiG9w0B
AQsFAAOCAQEAQtU92CobwSmpkBgkk29rG3DK/nT5NKh34bwgmR7ZfzNssxDsxCaF
qgCvYvoL1BwrIk+rUUu7M+quQEee6OYqToHdRqpAMG4w5VGX+OKjAuS6P+TOmhT/
2XblNXgMtKmvYnhkYqgu+tGRM4P+8/fYXoKiIaVQhUKtFFLas8ubpvqQHzxGDXa2
6pomTYyLMaLBLZtbmKdHgcesrYhVogiepK13+KVhlROKNBe5iVwrWBLmMynCadVq
WrdMj3NXcWEYYEaPtdHn7LFYJ5HezpwxIiBWuu0bhMhu1NyrzCtN8BS2vVdj+vdq
JPHGicBDtdH/d4x29cpyUZQntNl3cPBpQw==
-----END CERTIFICATE-----
)EOF";

// 2. The Client Certificate (Your device's public ID)
const char* client_cert = R"EOF(
-----BEGIN CERTIFICATE-----
MIID9zCCAt+gAwIBAgIUNZpJEPmyE5KriRUmNQkfV6B9CX4wDQYJKoZIhvcNAQEL
BQAwgZExCzAJBgNVBAYTAklUMREwDwYDVQQIDAhDYWxhYnJpYTEQMA4GA1UEBwwH
Q29zZW56YTEUMBIGA1UECgwLSW90U2VjdXJpdHkxDDAKBgNVBAsMA2NzcDETMBEG
A1UEAwwKY3NwLXNlcnZlcjEkMCIGCSqGSIb3DQEJARYVdmRhbWljb3dvcmtAZ21h
aWwuY29tMB4XDTI2MDEwOTE2Mjc0NVoXDTI3MDEwOTE2Mjc0NVowgZQxCzAJBgNV
BAYTAklUMREwDwYDVQQIDAhDYWxhYnJpYTEQMA4GA1UEBwwHQ29zZW56YTEUMBIG
A1UECgwLSW90U2VjdXJpdHkxDzANBgNVBAsMBkNsaWVudDETMBEGA1UEAwwKSW90
LUNsaWVudDEkMCIGCSqGSIb3DQEJARYVdmRhbWljb3dvcmtAZ21haWwuY29tMIIB
IjANBgkqhkiG9w0BAQEFAAOCAQ8AMIIBCgKCAQEAxL7D7bj6MkacYbzItt/GM88A
QwQ7Tf/9RbJtoD1F2Gw7GGQ/Kym0jHKSI1SMWYe214AfThOHD6sW+A2yBDfu/Ft2
GufCGU3igoQgcid7bXQk51DNGscqQwnEz8Lhtt0cc87JlofEeT7/xgPsXU1aWaGv
bdbfY+xun+hgWfHeGXzq4GJNcoVIMGVtOjJ+C7n5qOOmsiKqANH5gfrA2U+ZKPp8
xQsoIwryfq6zi+5kenVkQFCNed5ALKAivOFLHdSndJVzpizIpw/9Xb/oOTNoXorD
00yh2diOMDIWz9vCLDSFe+w3rEW77BXP3tAPvTsv/H9FUEA4xUWlUGKIPIHJEwID
AQABo0IwQDAdBgNVHQ4EFgQU8IOeTOZCESzd9ysE4M1hJHAcV+kwHwYDVR0jBBgw
FoAUR8VObiwy9iDgsuMZNsplzx8hdwMwDQYJKoZIhvcNAQELBQADggEBACe1KOfE
gG6U/GNuUKVbgwdK7lNBT8Uw8huv+CLFDAZh+HDMUK9P/2GnflWjInLB4MAIN4JV
tZbZv/bkPnsVtzimR0VQtn/T/taD+UY14sKT9qsFSzrUMLzvQ+yZA0ZSH/y7AI2I
ekW1EjS+hXjs/bsLJoMmu+XeEZRJV9Wpr/xpEbipUY5R81wMWuuLHHSdTbTSKJ5W
NpCkw/0PfDawoffH0jhT7j7Jnvuz4UmKNGWDjT/lLybyMDYACILMrKqysKm+J9P0
4FFxUa5VEgX0YexMEF7gbCI1lXPQCGzp9JO03v3ABvG/mrvXIfxfnQqRGCz6rF/L
gtpBGKozlgIJxB8=
-----END CERTIFICATE-----
)EOF";

// 3. The Client Private Key (Your device's secret key)
const char* client_key = R"EOF(
-----BEGIN PRIVATE KEY-----
MIIEugIBADANBgkqhkiG9w0BAQEFAASCBKQwggSgAgEAAoIBAQDEvsPtuPoyRpxh
vMi238YzzwBDBDtN//1Fsm2gPUXYbDsYZD8rKbSMcpIjVIxZh7bXgB9OE4cPqxb4
DbIEN+78W3Ya58IZTeKChCByJ3ttdCTnUM0axypDCcTPwuG23RxzzsmWh8R5Pv/G
A+xdTVpZoa9t1t9j7G6f6GBZ8d4ZfOrgYk1yhUgwZW06Mn4Lufmo46ayIqoA0fmB
+sDZT5ko+nzFCygjCvJ+rrOL7mR6dWRAUI153kAsoCK84Usd1Kd0lXOmLMinD/1d
v+g5M2heisPTTKHZ2I4wMhbP28IsNIV77DesRbvsFc/e0A+9Oy/8f0VQQDjFRaVQ
Yog8gckTAgMBAAECggEAUY257aqFm52FaUY19QghQoyF0UHJy3VXaTKjGo8LisCi
ZmP3g07QVn+PcDG1087bzcyALX8Ot0H2TXBv4CvHVrga5uA2pwKP6AYY5PjUwvQn
7/Kgcn8oV42PFYf0xDY3exG2oj05BgFFSLGLoslTsF/DNkahZuw0lvheKCqIJAvX
x0uvW3vgDh0VaBTiPPQo5SvgGxVA1Ks1RfXYBEv444tDZO1WJ+8mq+DGtoveHuyu
1S1bWgoVzHvH+lbi2gg/DgH9uQxt0B7NhrFGuk1+R3c9Z94uA3+B8NWpiNHsF+Fa
gkl+nz8LzeUeSd6fy7Z7KXs9+hbu6nYUb/U6+BJq4QKBgQDivYlTL/M0Sr7yYDyg
VHHJ6ThyGtowjbd1QnUx7sQ715j0K1HRGZlcuKm+1cTvzFVHkkdPXng3A/JRVGon
PU2TZF8AcCX0+gvuKslNujBsQjIqPiva4j4gn8jz1bvvMFoKiyWPA9SoYa/S4XsL
n14qIT50gqfoSkrjk9ppI6wAmwKBgQDeIlMtCrviiWIRyWjxgcV26+7uCYpKDMhJ
jBsSiv1g0/iVi/CbuPkgqrcS/IM8rZJAlbi5DWGz1TC5vCg8KYy9uPDZrEDGBn77
wO5MIkgqv4wtG2rdwaEzCmlgz08QsOdeM71bz7jfs8ckrBew2Ne8yWHg56IRVFo6
SDXrq3106QKBgByFoySnv3wwetyaZoX0mWvAvqz7276H1TAW8A8b7etpL4Bngp8/
DR+wywmKcn+HwKKEMBw30f95q523dLMC7yM/WQQBF4U9fwqyryfr5/N2UEEoGPQr
yYzpDKo/lKh9+JWi81KONM4Jm8h3PLc1kO7Tx7t4RA7gaZM/IhZful9JAn9dth1g
4yZga5Tz7ARZ3mVvxhkGUwAEPWBBptnE+N3r+4DjliXrjB2NqneRivXSo2cP2BoV
949ATrA/qyFOQDkf0OXK7uBkqljn3HyrocrQPf7lCKwM4aMf5USPkuXIJNl25Fz+
XqOfvDhHQFK+SLy66Dpip1W3+d4WuGAHDFHhAoGAcRXQq3RLiIFCy0+KQrG9TOlN
INVubO9cj7cUiNT+uP+Tvdgas7TyGmpfnTwRoZ6IjCIT3opM+AVZ2xZE07+jrHtS
cQqeh+8fZqUpKKY6RcbNi5DP/DmFKspxPvG4JbmbNqG26pwASRZIcdOAVbsTjBYA
R4jxmJrEZLZwvAJ9rqs=
-----END PRIVATE KEY-----
)EOF";

// ===================== WIFI + MQTT ==================
const char* WIFI_SSID = "RAMA_WIFI_MOBILE";
const char* WIFI_PASS = "rr99rr99rr";

const char* MQTT_BROKER = "10.146.61.134";
const int   MQTT_PORT   = 8883; // <--- SECURE PORT

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

WiFiClientSecure espClient; // <--- CHANGED: Secure Client
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
    Serial.print("Connecting to MQTTS (Secure)... ");

    // Connect without username/password as requested
    if (mqttClient.connect(clientId.c_str())) {
      Serial.println("connected");
      mqttClient.subscribe(TOPIC_RESULT);
    } else {
      Serial.print("failed, rc=");
      Serial.print(mqttClient.state());
      
      // Print SSL specific errors to help debugging
      char buf[256];
      espClient.lastError(buf, 256);
      Serial.print(" SSL Error: ");
      Serial.println(buf);
      
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

  // Correct System Time is CRITICAL for SSL Certificate Validation
  configTime(GMT_OFFSET_SEC, DAYLIGHT_OFFSET_SEC, NTP_SERVER);
  
  // Wait for time to be set
  Serial.print("Waiting for NTP time sync: ");
  time_t now = time(nullptr);
  while (now < 8 * 3600 * 2) {
    delay(500);
    Serial.print(".");
    now = time(nullptr);
  }
  Serial.println("");
  Serial.print("Current time: ");
  Serial.println(ctime(&now));

  // Load Certificates
  espClient.setCACert(root_ca);
  espClient.setCertificate(client_cert);
  espClient.setPrivateKey(client_key);

  mqttClient.setServer(MQTT_BROKER, MQTT_PORT);
  mqttClient.setCallback(mqttCallback);
  // Increase buffer size if your SSL handshake or messages are large
  mqttClient.setBufferSize(512); 

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