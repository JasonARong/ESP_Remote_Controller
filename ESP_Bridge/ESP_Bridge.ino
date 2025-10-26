#include <Arduino.h>
#include <USB.h>
#include <USBHIDMouse.h>
#include <NimBLEDevice.h>

USBHIDMouse Mouse;

#define SERVICE_UUID        "00001234-0000-1000-8000-00805f9b34fb"
#define CHARACTERISTIC_UUID "0000abcd-0000-1000-8000-00805f9b34fb"

NimBLEServer* pServer = nullptr;
NimBLEService* pService = nullptr;
NimBLECharacteristic* pChar = nullptr;

bool deviceConnected = false;
bool oldDeviceConnected = false;

/*** --- BLE Write Callback --- ***/
class MouseMoveCallback : public NimBLECharacteristicCallbacks {
  uint8_t lastButtonState;  // remember previous button state

  void onWrite(NimBLECharacteristic* pCharacteristic, NimBLEConnInfo& connInfo) {
    std::string value = pCharacteristic->getValue();
    if (value.length() != 6) {
      Serial.printf("⚠️ Got %d bytes (expected 6)\n", value.length());
      return;
    }

    const uint8_t* data = (const uint8_t*)value.data();
    uint8_t buttonState = data[0];
    // uint8_t reserved = data[1]; // (not used yet)
    int16_t dx = (int16_t)(data[2] | (data[3] << 8));
    int16_t dy = (int16_t)(data[4] | (data[5] << 8));
    
    Mouse.move(dx, dy);

    bool prevLeft = (lastButtonState & 0x01);
    bool currLeft = (buttonState & 0x01);

    if (currLeft && !prevLeft) {
      Mouse.press(MOUSE_LEFT);
    } else if (!currLeft && prevLeft) {
      Mouse.release(MOUSE_LEFT);
    }

    lastButtonState = buttonState;
    Serial.printf("Received dx=%d dy=%d buttonState=%d\n", dx, dy, buttonState);
  }
};

/*** --- Server Connection Callbacks --- ***/
class ServerCallbacks : public NimBLEServerCallbacks {
  void onConnect(NimBLEServer* pServer, NimBLEConnInfo& connInfo) {
    deviceConnected = true;
    Serial.println("📶 iPhone connected");
    
    // Update connection parameters for better stability (iOS-optimized)
    // These values work well with iOS devices
    pServer->updateConnParams(
      connInfo.getConnHandle(),
      12,   // min interval (15ms = 12*1.25ms)
      24,   // max interval (30ms = 24*1.25ms) 
      0,    // latency
      400   // timeout (4000ms = 400*10ms)
    );
    
    // Stop advertising when connected
    NimBLEDevice::getAdvertising()->stop();
    Serial.println("✅ Connection parameters updated");
  }
  
  void onDisconnect(NimBLEServer* pServer, NimBLEConnInfo& connInfo, int reason) {
    deviceConnected = false;
    Serial.printf("🔌 iPhone disconnected, reason: %d\n", reason);
    Serial.println("   Common reasons: 8=timeout, 19=remote user, 22=local host, 531=connection failed");
    
    // Don't restart advertising immediately - give time to clean up
    delay(100);
  }
  
  void onMTUChange(uint16_t MTU, NimBLEConnInfo& connInfo) {
    Serial.printf("📏 MTU updated to: %d\n", MTU);
  }
};

void setup() {
  Serial.begin(115200);
  delay(500);
  USB.begin();
  Mouse.begin();
  Serial.println("✅ USB HID Mouse started");
  
  /************** BLE SETUP **************/
  Serial.println("🔧 Initializing NimBLE...");
  
  NimBLEDevice::init("ESP_MouseBridge");
  NimBLEDevice::setMTU(247); // MTU (Maximum Transmission Unit), sets max BLE packet size
  NimBLEDevice::setPower(ESP_PWR_LVL_P6); // Power level, P9 is max, but P6 often more stable
  NimBLEDevice::setSecurityAuth(false, false, true); // false = each reconnection is a new session, false = No PIN comparison required, true = optional encryption
  NimBLEDevice::setSecurityIOCap(BLE_HS_IO_NO_INPUT_OUTPUT); // Fully automatic pairing
  
  // Create SERVER
  pServer = NimBLEDevice::createServer();
  pServer->setCallbacks(new ServerCallbacks());
  // Create SERVICE
  pService = pServer->createService(SERVICE_UUID);
  // Create CHARACTERISTIC
  pChar = pService->createCharacteristic(CHARACTERISTIC_UUID, NIMBLE_PROPERTY::WRITE_NR);
  pChar->setCallbacks(new MouseMoveCallback());
  
  pService->start();
  

  // Configure advertising
  NimBLEAdvertising* pAdvertising = NimBLEDevice::getAdvertising();
  pAdvertising->addServiceUUID(SERVICE_UUID);
  pAdvertising->setMinInterval(32);  // (32*0.625ms = 20ms) iOS-friendly 
  pAdvertising->setMaxInterval(244); // (244*0.625ms = 152.5ms) iOS-friendly
  pAdvertising->start();
  

  Serial.println("✅ BLE advertising started");
  Serial.println("📱 Ready for iPhone connection");
  Serial.println("🎧 Listening for writes...");
}

void loop() {
  // Handle connection state changes
  if (deviceConnected && !oldDeviceConnected) {
    oldDeviceConnected = deviceConnected;
  }
  
  // Restart advertising if disconnected
  if (!deviceConnected && oldDeviceConnected) {
    delay(500); // Give time before restarting
    NimBLEDevice::getAdvertising()->start();
    Serial.println("🔄 Restarting advertising...");
    oldDeviceConnected = deviceConnected;
  }
  
  delay(100);
}