/*******************************************************
 * ESP32 BLE (NimBLE) + HMAC + MQTTS (Secure)
 * FIXED: Uses NimBLE to save RAM for SSL Handshake
 *******************************************************/

// 1. CHANGED: Include NimBLE instead of standard BLE
#include <NimBLEDevice.h> 

#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include "mbedtls/md.h"
#include <mbedtls/aes.h>
#include <mbedtls/base64.h>
#include <time.h>

// ===================== TIME =====================
const char* NTP_SERVER = "pool.ntp.org";
const long  GMT_OFFSET_SEC = 0;
const int   DAYLIGHT_OFFSET_SEC = 0;

// ===================== SECURITY =====================
const char* SHARED_SECRET = "SUPER_SECRET_KEY";
const char* CORRECT_PIN   = "1234";
const char* AES_KEY = "1234567890123456"; 
const char* AES_IV  = "abcdefghijklmnop";

// ===================== CERTIFICATES =================
// Keep your existing certificates exactly as they are
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
const char* WIFI_SSID = "Vincenzo's Galaxy S21 5G";
const char* WIFI_PASS = "pbys2426767";

const char* MQTT_BROKER = "10.253.152.211";
const int   MQTT_PORT   = 8883;

const char* TOPIC_REQUEST = "door_access/request";
const char* TOPIC_RESULT  = "door_access/result";
const char* DEVICE_ID     = "GARAGE01";

// ===================== BLE UUIDs =====================
#define SERVICE_UUID           "6e400001-b5a3-f393-e0a9-e50e24dcca9e"
#define CHARACTERISTIC_RX_UUID "6e400002-b5a3-f393-e0a9-e50e24dcca9e"
#define CHARACTERISTIC_TX_UUID "6e400003-b5a3-f393-e0a9-e50e24dcca9e"

// ===================== GLOBALS ======================
// 2. CHANGED: Use NimBLE classes
NimBLECharacteristic* pTxCharacteristic = nullptr;
NimBLECharacteristic* pRxCharacteristic = nullptr;

WiFiClientSecure espClient; 
PubSubClient mqttClient(espClient);

String currentNonce = "";

// ===================== UTILS ========================
String generateNonce() {
  return String((uint32_t)esp_random(), HEX);
}
String decryptAES(String encryptedBase64) {
  // 1. Decode Base64 to raw bytes
  size_t outputLength;
  unsigned char encryptedBytes[256]; // Ensure this is large enough for your max message
  
  int ret = mbedtls_base64_decode(encryptedBytes, sizeof(encryptedBytes), &outputLength, 
                        (const unsigned char*)encryptedBase64.c_str(), encryptedBase64.length());

  if (ret != 0) {
    Serial.println("Base64 Decode Failed");
    return "";
  }

  // 2. Prepare AES
  mbedtls_aes_context aes;
  mbedtls_aes_init(&aes);
  // Ensure AES_KEY is cast correctly to const unsigned char*
  mbedtls_aes_setkey_dec(&aes, (const unsigned char*)AES_KEY, 128);

  // 3. Decrypt (CBC Mode)
  unsigned char decryptedBytes[256];
  unsigned char iv[16];
  memcpy(iv, AES_IV, 16); // Important: Keep original IV clean

  // Note: 'outputLength' from base64 decode MUST be a multiple of 16 for AES
  mbedtls_aes_crypt_cbc(&aes, MBEDTLS_AES_DECRYPT, outputLength, iv, encryptedBytes, decryptedBytes);
  mbedtls_aes_free(&aes);

  // 4. Remove PKCS7 Padding (The Logic Fix)
  // In PKCS7, the last byte tells us exactly how many bytes are padding.
  // Example: If 4 bytes of padding are needed, they are [0x04, 0x04, 0x04, 0x04]
  
  int padValue = (int)decryptedBytes[outputLength - 1];

  // Sanity check: Padding must be between 1 and 16 for AES-128
  if (padValue < 1 || padValue > 16) {
    Serial.println("Invalid Padding Value!");
    return ""; // Decryption failed or wrong key
  }

  // Calculate actual data length
  int plainTextLength = outputLength - padValue;

  // 5. Build String
  String decrypted = "";
  for (int i = 0; i < plainTextLength; i++) {
    decrypted += (char)decryptedBytes[i];
  }

  return decrypted;
}
String encryptAES(String plainText) {
  // 1. Init AES
  mbedtls_aes_context aes;
  mbedtls_aes_init(&aes);
  mbedtls_aes_setkey_enc(&aes, (const unsigned char*)AES_KEY, 128);

  // 2. Calculate PKCS7 Padding
  // AES requires data to be in blocks of 16 bytes.
  int inputLen = plainText.length();
  int remainder = inputLen % 16;
  int padding = 16 - remainder; // Value between 1 and 16
  int totalLen = inputLen + padding;

  // Create a buffer for the padded message
  unsigned char inputBuffer[totalLen];
  // Copy original text
  memcpy(inputBuffer, plainText.c_str(), inputLen);
  // Add padding bytes (e.g., if we need 4 bytes, add 0x04 four times)
  for (int i = inputLen; i < totalLen; i++) {
    inputBuffer[i] = (unsigned char)padding;
  }

  // 3. Encrypt (CBC Mode)
  unsigned char outputBuffer[totalLen];
  unsigned char iv[16];
  memcpy(iv, AES_IV, 16); // Use a fresh copy of IV (lib modifies it)

  // Encrypt block by block
  for (int i = 0; i < totalLen; i += 16) {
    mbedtls_aes_crypt_cbc(&aes, MBEDTLS_AES_ENCRYPT, 16, iv, inputBuffer + i, outputBuffer + i);
  }
  
  mbedtls_aes_free(&aes);

  // 4. Encode to Base64
  // We need to calculate the output size for Base64 first
  size_t base64Len = 0;
  mbedtls_base64_encode(NULL, 0, &base64Len, outputBuffer, totalLen);
  
  unsigned char base64Buffer[base64Len + 1];
  mbedtls_base64_encode(base64Buffer, base64Len + 1, &base64Len, outputBuffer, totalLen);
  base64Buffer[base64Len] = '\0'; // Null terminate string

  return String((char*)base64Buffer);
}
String computeHMAC(const String& msg) {
  byte hmacResult[32];
  mbedtls_md_context_t ctx;
  mbedtls_md_init(&ctx);
  mbedtls_md_setup(&ctx, mbedtls_md_info_from_type(MBEDTLS_MD_SHA256), 1);
  mbedtls_md_hmac_starts(&ctx, (const unsigned char*)SHARED_SECRET, strlen(SHARED_SECRET));
  mbedtls_md_hmac_update(&ctx, (const unsigned char*)msg.c_str(), msg.length());
  mbedtls_md_hmac_finish(&ctx, hmacResult);
  mbedtls_md_free(&ctx);

  char hex[65];
  for (int i = 0; i < 32; i++) sprintf(hex + i * 2, "%02x", hmacResult[i]);
  hex[64] = '\0';
  return String(hex);
}

String getISOTimestamp() {
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo)) return "1970-01-01T00:00:00Z";
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
    String clientId = "ESP32_SECURE_" + String((uint32_t)esp_random(), HEX);
    Serial.print("Connecting to MQTTS (Secure)... ");
    
    // We can verify free heap here to confirm we are safe
    // Serial.print("Heap: "); Serial.print(ESP.getFreeHeap());
    
    if (mqttClient.connect(clientId.c_str())) {
      Serial.println("connected");
      mqttClient.subscribe(TOPIC_RESULT);
    } else {
      Serial.print("\nFailed, rc=");
      Serial.print(mqttClient.state());
      char buf[256];
      espClient.lastError(buf, 256);
      Serial.print("\nSSL Error: ");
      Serial.println(buf);
      delay(2000);
    }
  }
}

void publishAccessRequest(const String& msg) {
  if (!mqttClient.connected()) {
    ensureMQTT();
  }
  StaticJsonDocument<256> doc;
  doc["device_id"] = DEVICE_ID;
  doc["event"]     = "door_access/request";
  doc["data"]       = msg;
  doc["method"]    = "BLE";
  doc["timestamp"] = getISOTimestamp();

  String jsonString;
  serializeJson(doc, jsonString);
  String securePayload = createSecurePacket(jsonString);

  Serial.println("Send MQTTS -> " + securePayload);
  mqttClient.publish(TOPIC_REQUEST, securePayload.c_str());
}

String createSecurePacket(String plainText) {
    String ciphertext = encryptAES(plainText);  
    String signature = computeHMAC(ciphertext); 
    return signature + ":" + ciphertext;
}

// ===================== MQTT CALLBACK =================
void mqttCallback(char* topic, byte* payload, unsigned int length) {
  if (String(topic) != TOPIC_RESULT) return;
  String raw_msg;
  for (unsigned int i = 0; i < length; i++) raw_msg += (char)payload[i];
  raw_msg.trim();
  StaticJsonDocument<256> doc1;

  if (deserializeJson(doc1, raw_msg)) {
    notifyBLE(createSecurePacket("WRONG"));
    return;
  }

  String msg=doc1["response"];
  Serial.println("MQTT IN -> " + msg);
  int separatorIndex = msg.indexOf(':');
  if (separatorIndex == -1) {
        notifyBLE(createSecurePacket("ERROR_FORMAT"));
        return;
  }
  String clientSignature = msg.substring(0, separatorIndex);
  String ciphertext      = msg.substring(separatorIndex + 1);
  String expectedSignature = computeHMAC(ciphertext);

  if (!clientSignature.equals(expectedSignature)) {
        Serial.println("Security Alert: Packet Tampered!");
        notifyBLE(createSecurePacket("WRONG")); // Reject immediately. Do NOT decrypt.
        return;
  }
  String packet=decryptAES(ciphertext);

  StaticJsonDocument<256> doc2;
  if (deserializeJson(doc2, packet)) {
    notifyBLE(createSecurePacket("WRONG"));
    return;
  }

  bool decision = doc2["authorized"];
  String reason= doc2["reason"];
  Serial.println("raeson: "+reason);
  if (decision) {
    notifyBLE(createSecurePacket("OK"));
  } else {
    notifyBLE(createSecurePacket("WRONG"));
  }
}

// ===================== BLE RX CALLBACK (NimBLE) =================
// 3. CHANGED: Inherit from NimBLECharacteristicCallbacks
// ===================== BLE RX CALLBACK (NimBLE v2.x Compatible) =================
class MyRXCallbacks : public NimBLECharacteristicCallbacks {
  // CHANGED: New signature requires 'NimBLEConnInfo&'
  void onWrite(NimBLECharacteristic* c, NimBLEConnInfo& connInfo) override {
    String received = c->getValue().c_str(); 
    received.trim();

    Serial.println("BLE RX -> " + received);
    int separatorIndex = received.indexOf(':');
    if (separatorIndex == -1) {
        notifyBLE(createSecurePacket("ERROR_FORMAT"));
        return;
    }
    String clientSignature = received.substring(0, separatorIndex);
    String ciphertext      = received.substring(separatorIndex + 1);
    String expectedSignature = computeHMAC(ciphertext);

    if (!clientSignature.equals(expectedSignature)) {
        Serial.println("Security Alert: Packet Tampered!");
        notifyBLE(createSecurePacket("WRONG")); // Reject immediately. Do NOT decrypt.
        return;
    }

    if (ciphertext == encryptAES("HELLO")) {
      currentNonce = generateNonce();
      String packet=createSecurePacket("NONCE-"+currentNonce);
      notifyBLE(packet);
      return;
    }


    notifyBLE(createSecurePacket("PENDING"));
    Serial.println("Send to mqqts");
    publishAccessRequest(received);
    currentNonce="";

  }
  String createSecurePacket(String plainText) {
    String ciphertext = encryptAES(plainText);  
    String signature = computeHMAC(ciphertext); 
    return signature + ":" + ciphertext;
}
};
class MyServerCallbacks : public NimBLEServerCallbacks {
  void onConnect(NimBLEServer* pServer, NimBLEConnInfo& connInfo) override {
    Serial.println("Device Connected!");
  }

  void onDisconnect(NimBLEServer* pServer, NimBLEConnInfo& connInfo, int reason) override {
    Serial.println("Device Disconnected. Restarting Advertising...");
    
    // CRITICAL: Restart advertising immediately so new devices can find it
    NimBLEDevice::startAdvertising();
  }
};
// ===================== SETUP ========================
void setup() {
  Serial.begin(115200);

  // 1. WiFi & Time
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  while (WiFi.status() != WL_CONNECTED) { delay(500); Serial.print("."); }
  Serial.println("\nWiFi Connected");
  
  configTime(GMT_OFFSET_SEC, DAYLIGHT_OFFSET_SEC, NTP_SERVER);
  while (time(nullptr) < 1000000000l) { delay(500); }

  // 2. SSL Certs
  espClient.setCACert(root_ca);
  espClient.setCertificate(client_cert);
  espClient.setPrivateKey(client_key);

  mqttClient.setServer(MQTT_BROKER, MQTT_PORT);
  mqttClient.setBufferSize(1024);
  mqttClient.setCallback(mqttCallback);

  // 3. BLE Init
  NimBLEDevice::init(DEVICE_ID); 
  NimBLEDevice::setPower(ESP_PWR_LVL_P9); // Max Power

  NimBLEServer* server = NimBLEDevice::createServer();
  server->setCallbacks(new MyServerCallbacks());
  NimBLEService* service = server->createService(SERVICE_UUID);

  pTxCharacteristic = service->createCharacteristic(
    CHARACTERISTIC_TX_UUID,
    NIMBLE_PROPERTY::NOTIFY
  );

  pRxCharacteristic = service->createCharacteristic(
    CHARACTERISTIC_RX_UUID,
    NIMBLE_PROPERTY::WRITE
  );
  pRxCharacteristic->setCallbacks(new MyRXCallbacks());

  service->start();

  // ==============================================================
  // 4. FIXED ADVERTISING (Compiles on NimBLE v2.x)
  // ==============================================================
  NimBLEAdvertising* pAdvertising = NimBLEDevice::getAdvertising();

  // PACKAGE A: The "Searchable" Packet (Main)
  // We put the UUID here so your phone filters find it immediately.
  NimBLEAdvertisementData mainAd;
  mainAd.setFlags(0x06); // General Discovery Mode
  mainAd.setCompleteServices(NimBLEUUID(SERVICE_UUID));
  mainAd.setName(DEVICE_ID);
  // PACKAGE B: The "Info" Packet (Scan Response)
  // We put the Name here. The phone asks for this after finding Package A.
  NimBLEAdvertisementData scanResponse;
  scanResponse.setName(DEVICE_ID);

  // Apply both packages
  pAdvertising->setAdvertisementData(mainAd);
  pAdvertising->setScanResponseData(scanResponse); // <--- This fixes the error!
  
  pAdvertising->start();

  Serial.println("BLE Started. Name: GARAGE01");
}
// ===================== LOOP =========================
void loop() {
  if (!mqttClient.connected()) {
    ensureMQTT();
  }
  mqttClient.loop();
}