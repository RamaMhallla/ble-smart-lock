#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>

// UART-like BLE service (Nordic UART)
#define SERVICE_UUID           "6e400001-b5a3-f393-e0a9-e50e24dcca9e"
#define CHARACTERISTIC_RX_UUID "6e400002-b5a3-f393-e0a9-e50e24dcca9e"
#define CHARACTERISTIC_TX_UUID "6e400003-b5a3-f393-e0a9-e50e24dcca9e"

BLEServer* pServer = nullptr;
BLECharacteristic* pTxCharacteristic = nullptr;
BLECharacteristic* pRxCharacteristic = nullptr;

bool deviceConnected = false;

const char* CORRECT_PIN = "1234";

// ====================== Server Callbacks ======================
class MyServerCallbacks : public BLEServerCallbacks {
  void onConnect(BLEServer* pServer) override {
    deviceConnected = true;
    Serial.println(">>> Client connected");
  }

  void onDisconnect(BLEServer* pServer) override {
    deviceConnected = false;
    Serial.println(">>> Client disconnected");

    // VERY IMPORTANT: restart advertising
    delay(300);
    BLEAdvertising* adv = BLEDevice::getAdvertising();
    adv->start();
    Serial.println(">>> Advertising restarted!");
  }
};

// ====================== RX Callbacks ==========================
class MyCallbacks : public BLECharacteristicCallbacks {
  void onWrite(BLECharacteristic* characteristic) override {

    // getValue() عندك يرجّع Arduino String، وليس std::string
    String val = characteristic->getValue();  // ✔ 100% يعمل على كل الإصدارات

    // تحويل إلى std::string بشكل صحيح
    std::string valueStd = std::string(val.c_str());

    if (valueStd.length() == 0) return;

    Serial.print("Received PIN: ");
    Serial.println(valueStd.c_str());

    if (valueStd == CORRECT_PIN) {
      Serial.println("PIN correct → Opening door!");
      pTxCharacteristic->setValue("OK");
      pTxCharacteristic->notify();
    } else {
      Serial.println("WRONG PIN → Access denied");
      pTxCharacteristic->setValue("WRONG");
      pTxCharacteristic->notify();
    }
  }
};


void setup() {
  Serial.begin(115200);
  delay(1000);

  Serial.println("========== ESP32 Garage Door ==========");

  BLEDevice::init("GarageDoorESP");

  pServer = BLEDevice::createServer();
  pServer->setCallbacks(new MyServerCallbacks());

  BLEService* pService = pServer->createService(SERVICE_UUID);

  // TX (ESP32 -> mobile)
  pTxCharacteristic = pService->createCharacteristic(
    CHARACTERISTIC_TX_UUID,
    BLECharacteristic::PROPERTY_NOTIFY
  );
  pTxCharacteristic->addDescriptor(new BLE2902());

  // RX (mobile -> ESP32)
  pRxCharacteristic = pService->createCharacteristic(
    CHARACTERISTIC_RX_UUID,
    BLECharacteristic::PROPERTY_WRITE
  );
  pRxCharacteristic->setCallbacks(new MyCallbacks());

  pService->start();

  BLEAdvertising* adv = BLEDevice::getAdvertising();
  adv->addServiceUUID(SERVICE_UUID);
  adv->setScanResponse(true);
  adv->setMinPreferred(0x06);
  adv->setMaxPreferred(0x12);
  adv->start();

  Serial.println("Bluetooth Ready – Waiting for connections...");
  Serial.println("============================================");
}

void loop() {}
