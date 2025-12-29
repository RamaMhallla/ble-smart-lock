#include <BLEDevice.h> //To enable BLE on ESP32 (Bluetooth configuration).
#include <BLEServer.h> //ESP32 as Server (receives a connection from the phone)
#include <BLEUtils.h> //Helpful tools within BLE (definitions and help functions).
#include <BLE2902.h> //famous Descriptor with notify, so that devices (such as iOS/Android) accept notifications.

// UART-like BLE service (Nordic UART)
#define SERVICE_UUID           "6e400001-b5a3-f393-e0a9-e50e24dcca9e" 
//RX Characteristic: The mobile phone writes to it (Write) to send data to the ESP32.
#define CHARACTERISTIC_RX_UUID "6e400002-b5a3-f393-e0a9-e50e24dcca9e"
//TX Characteristic: The ESP32 sends a (Notify) message to send a reply to the mobile phone.
#define CHARACTERISTIC_TX_UUID "6e400003-b5a3-f393-e0a9-e50e24dcca9e"

//A server pointer variable. Initially empty (nullptr).
BLEServer* pServer = nullptr;

//Indicators for Characteristics (TX for transmission, RX for reception).
BLECharacteristic* pTxCharacteristic = nullptr;
BLECharacteristic* pRxCharacteristic = nullptr;

//A variable to determine whether a (mobile) client is connected or not.
bool deviceConnected = false;

const char* CORRECT_PIN = "1234";

// ====================== Server Callbacks ======================
// class inherits from BLEServerCallbacks so that we run code while connection/disconnection.
class MyServerCallbacks : public BLEServerCallbacks {
  //When a mobile phone connects:Set deviceConnected = true, Print on Serial that a client has connected.
  void onConnect(BLEServer* pServer) override {
    deviceConnected = true;
    Serial.println(">>> Client connected");
  }

  //When the phone disconnects: Set deviceConnected to false and Print a message.
  void onDisconnect(BLEServer* pServer) override {
    deviceConnected = false;
    Serial.println(">>> Client disconnected");

    // VERY IMPORTANT: restart advertising
    delay(300);
    //We bring the current advertising object.
    BLEAdvertising* adv = BLEDevice::getAdvertising();
    //We restart the ad so that any mobile device can detect the device again and reconnect.
    //We print a message stating that the ad is back.
    //Important Because some settings might stop the ad after disconnection if you don't restart it.
    adv->start();
    Serial.println(">>> Advertising restarted!");
  }
};

// ====================== RX Callbacks ==========================
//A class dedicated to handling events on a characteristic (writing).
class MyCallbacks : public BLECharacteristicCallbacks {
  //This function is called automatically when the phone is running Write on RX.
  void onWrite(BLECharacteristic* characteristic) override {

    //It reads data written as String (Arduino String).
    String val = characteristic->getValue(); 

    // Convert Arduino String to std::string (so that the comparison is convenient).
    std::string valueStd = std::string(val.c_str());

    //If there is no data (empty) -> Exit.
    if (valueStd.length() == 0) return;

    Serial.print("Received PIN: ");
    //The PIN that arrived is printed on the Serial Monitor.
    Serial.println(valueStd.c_str());

    if (valueStd == CORRECT_PIN) {
      Serial.println("PIN correct → Opening door!");
      //The message is prepared as "OK" on TX and sent to the mobile device via Notify.
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
  //The device name is specified in the advertisement (this is the name Flutter is looking for).
  BLEDevice::init("GarageDoorESP");

  //The server is created.
  //Callbacks are established for connection/disconnection.
  pServer = BLEDevice::createServer();
  pServer->setCallbacks(new MyServerCallbacks());
  
  //The Service creates the specified UUID.
  BLEService* pService = pServer->createService(SERVICE_UUID);

  // TX (ESP32 -> mobile)
  //Creates a TX characteristic with notify properties only.
  pTxCharacteristic = pService->createCharacteristic(
    CHARACTERISTIC_TX_UUID,
    BLECharacteristic::PROPERTY_NOTIFY
  );
  //The BLE2902 adds a descriptor to enable proper notification on devices.
  pTxCharacteristic->addDescriptor(new BLE2902());

  // RX (mobile -> ESP32)
  //RX creates characteristic with write properties.
  pRxCharacteristic = pService->createCharacteristic(
    CHARACTERISTIC_RX_UUID,
    BLECharacteristic::PROPERTY_WRITE
  );
  //The callback is linked to onWrite (when the mobile phone writes the PIN).
  pRxCharacteristic->setCallbacks(new MyCallbacks());

  pService->start();

  BLEAdvertising* adv = BLEDevice::getAdvertising();
  //Adding the service UUID to the advertisement so that the mobile phone knows the service is supported.
  adv->addServiceUUID(SERVICE_UUID);
  //Allow sending scan response (additional information when scan).
  adv->setScanResponse(true);
  //BLE connection settings (especially helpful for stability on some iOS/Android devices).
  adv->setMinPreferred(0x06);
  adv->setMaxPreferred(0x12);

  //The announcement begins: The mobile phone can now see GarageDoorESP.
  adv->start();

  Serial.println("Bluetooth Ready – Waiting for connections...");
  Serial.println("============================================");
}

//No need for a loop because everything works via callbacks.
//When the phone enters a PIN, onWrite activates automatically.
void loop() {}
