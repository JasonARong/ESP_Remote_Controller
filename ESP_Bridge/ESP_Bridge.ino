#include <Arduino.h>
#include <USB.h>
#include <USBHIDMouse.h>
#include <NimBLEDevice.h>

USBHIDMouse Mouse;

// UUIDs must match Swift app
#define SERVICE_UUID        "1234"
#define CHARACTERISTIC_UUID "ABCD"

BLEServer* pServer        = nullptr;
BLEService* pService      = nullptr;
BLECharacteristic* pChar  = nullptr;

/*** --- BLE Write Callback --- ***/
class MouseMoveCallback : public NimBLECharacteristicCallbacks {
  void onWrite(NimBLECharacteristic* pCharacteristic, NimBLEConnInfo& connInfo) {
    std::string value = pCharacteristic->getValue();
    // Serial.printf("🎯 onWrite triggered! Length=%d\n", value.length());
    
    if (value.length() == 4) {
      int16_t dx = (int16_t)((uint8_t)value[0] | ((uint8_t)value[1] << 8));
      int16_t dy = (int16_t)((uint8_t)value[2] | ((uint8_t)value[3] << 8));

      Serial.printf("Received dx=%d dy=%d\n", dx, dy);
      Mouse.move(dx, dy);
    } else {
      Serial.printf("Got %d bytes (expected 4)\n", value.length());
    }
  }
};

/*** --- Server Connection Callbacks --- ***/
class ServerCallbacks : public NimBLEServerCallbacks {
  void onConnect(NimBLEServer* pServer, NimBLEConnInfo& connInfo) {
    Serial.println("📶 iPhone connected");
  }

  void onDisconnect(NimBLEServer* pServer, NimBLEConnInfo& connInfo, int reason) {
    Serial.printf("🔌 iPhone disconnected, reason %d\n", reason);
    NimBLEDevice::getAdvertising()->start(); // restart advertising
  }
};

void setup() {
  Serial.begin(115200);
  delay(200);
  Mouse.begin();
  USB.begin();
  Serial.println("USB HID Mouse started");

  /************** BLE SETUP **************/
  NimBLEDevice::init("ESP_MouseBridge");
  NimBLEDevice::setMTU(247);   
  NimBLEDevice::setPower(ESP_PWR_LVL_P9);
  NimBLEDevice::setSecurityAuth(false, false, false);
  NimBLEDevice::setSecurityIOCap(BLE_HS_IO_NO_INPUT_OUTPUT);

  pServer = NimBLEDevice::createServer();
  pServer->setCallbacks(new ServerCallbacks());

  pService = pServer->createService(SERVICE_UUID);

  pChar = pService->createCharacteristic(
              CHARACTERISTIC_UUID,
              NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::WRITE_NR
          );
  pChar->setCallbacks(new MouseMoveCallback());

  pService->start();

  NimBLEAdvertising* pAdvertising = NimBLEDevice::getAdvertising();
  pAdvertising->addServiceUUID(SERVICE_UUID);
  pAdvertising->start();

  Serial.println("BLE advertising started");
  Serial.println("🎧 Listening for writes...");
}

void loop() {
  delay(100);
}