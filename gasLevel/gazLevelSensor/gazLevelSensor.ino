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
const char* AES_KEY = "1234567890123456"; 
const char* AES_IV  = "abcdefghijklmnop";

// ===================== CERTIFICATES (Truncated for brevity) =================
// KEEP YOUR CERTIFICATES EXACTLY AS THEY WERE IN YOUR CODE
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

const char* TOPIC_REQUEST = "home/sensor/gas";
const char* TOPIC_EMERGENCY = "door_access/result";
const char* DEVICE_ID     = "GARAGE02";

WiFiClientSecure espClient; 
PubSubClient mqttClient(espClient);

#define GAS_PIN 32
#define THRESHOLD 800 

// ===================== CRYPTO HELPERS (Same as before) =================
String encryptAES(String plainText) {
  mbedtls_aes_context aes;
  mbedtls_aes_init(&aes);
  mbedtls_aes_setkey_enc(&aes, (const unsigned char*)AES_KEY, 128);

  int inputLen = plainText.length();
  int padding = 16 - (inputLen % 16);
  int totalLen = inputLen + padding;

  unsigned char inputBuffer[totalLen];
  memcpy(inputBuffer, plainText.c_str(), inputLen);
  for (int i = inputLen; i < totalLen; i++) inputBuffer[i] = (unsigned char)padding;

  unsigned char outputBuffer[totalLen];
  unsigned char iv[16];
  memcpy(iv, AES_IV, 16); 

  for (int i = 0; i < totalLen; i += 16) {
    mbedtls_aes_crypt_cbc(&aes, MBEDTLS_AES_ENCRYPT, 16, iv, inputBuffer + i, outputBuffer + i);
  }
  mbedtls_aes_free(&aes);

  size_t base64Len = 0;
  mbedtls_base64_encode(NULL, 0, &base64Len, outputBuffer, totalLen);
  unsigned char base64Buffer[base64Len + 1];
  mbedtls_base64_encode(base64Buffer, base64Len + 1, &base64Len, outputBuffer, totalLen);
  base64Buffer[base64Len] = '\0'; 

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

String createSecurePacket(String plainText) {
    String ciphertext = encryptAES(plainText);  
    String signature = computeHMAC(ciphertext); 
    return signature + ":" + ciphertext;
}

String getISOTimestamp() {
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo)) return "1970-01-01T00:00:00Z";
  char buf[30];
  strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", &timeinfo);
  return String(buf);
}

// ===================== MISSING CONNECTION FUNCTION =====================
void ensureMQTT() {
  // Loop until we're reconnected
  while (!mqttClient.connected()) {
    Serial.print("Attempting MQTTS connection...");
    
    // Attempt to connect using the Client ID and Certificates loaded in setup()
    if (mqttClient.connect(DEVICE_ID)) { 
      Serial.println("connected");
    } else {
      Serial.print("failed, rc=");
      Serial.print(mqttClient.state());
      Serial.println(" try again in 2 seconds");
      
      // Print error details for debugging SSL
      char err_buf[100];
      espClient.lastError(err_buf, 100);
      Serial.print("SSL Error: ");
      Serial.println(err_buf);
      
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
  doc["event"]     = "home/sensor/gas";
  doc["gas_level"] = msg;
  doc["timestamp"] = getISOTimestamp();

  String jsonString;
  serializeJson(doc, jsonString);
  String securePayload = createSecurePacket(jsonString);

  Serial.println("Send MQTTS -> " + securePayload);
  mqttClient.publish(TOPIC_REQUEST, securePayload.c_str());
}

void publishEmergency(const String& msg) {
  if (!mqttClient.connected()) {
    ensureMQTT();
  }
  StaticJsonDocument<256> doc;
  doc["authorized"] = true;
  doc["reason"]     = "home/sensor/gas";

  String jsonString;
  serializeJson(doc, jsonString);
  String securePayload = createSecurePacket(jsonString);
  
  // Wrap in another JSON for NodeRED/Server compatibility if needed
  StaticJsonDocument<512> doc2;
  doc2["responce"] = securePayload; // Note: Typo 'responce' maintained from your snippet
  char buffer[512];
  serializeJson(doc2, buffer);

  Serial.println("Send Emergency -> " + String(buffer));
  mqttClient.publish(TOPIC_EMERGENCY, buffer);
}

void setup() {
  Serial.begin(115200);

  // 1. WiFi & Time
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  while (WiFi.status() != WL_CONNECTED) { delay(500); Serial.print("."); }
  Serial.println("\nWiFi Connected");
  
  configTime(GMT_OFFSET_SEC, DAYLIGHT_OFFSET_SEC, NTP_SERVER);
  
  // 2. SSL Certs
  espClient.setCACert(root_ca);
  espClient.setCertificate(client_cert);
  espClient.setPrivateKey(client_key);

  mqttClient.setServer(MQTT_BROKER, MQTT_PORT);
  mqttClient.setBufferSize(1024); // Important for encrypted payloads
}

void loop() {
  // CRITICAL FIX: You must call loop() to keep connection alive!
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