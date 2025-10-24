#include <Arduino.h>
#include <USB.h>
#include <USBHIDMouse.h>
#include <NimBLEDevice.h>

USBHIDMouse Mouse;

// ⚠️ CRITICAL: Use proper 128-bit UUIDs (must match your Swift app!)
#define SERVICE_UUID        "00001234-0000-1000-8000-00805f9b34fb"
#define CHARACTERISTIC_UUID "0000abcd-0000-1000-8000-00805f9b34fb"

BLEServer* pServer = nullptr;
BLEService* pService = nullptr;
BLECharacteristic* pChar = nullptr;

bool deviceConnected = false;
bool oldDeviceConnected = false;

/*** --- BLE Write Callback --- ***/
class MouseMoveCallback : public NimBLECharacteristicCallbacks {
  void onWrite(NimBLECharacteristic* pCharacteristic, NimBLEConnInfo& connInfo) {
    std::string value = pCharacteristic->getValue();
    
    if (value.length() == 4) {
      int16_t dx = (int16_t)((uint8_t)value[0] | ((uint8_t)value[1] << 8));
      int16_t dy = (int16_t)((uint8_t)value[2] | ((uint8_t)value[3] << 8));
      Serial.printf("Received dx=%d dy=%d\n", dx, dy);
      Mouse.move(dx, dy);
    } else {
      Serial.printf("⚠️ Got %d bytes (expected 4)\n", value.length());
    }
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
  
  // Set MTU (iOS typically uses 185)
  NimBLEDevice::setMTU(247);
  
  // Set power level - P9 is max, but P6 often more stable
  NimBLEDevice::setPower(ESP_PWR_LVL_P9);
  
  // Disable security for easier connection (re-enable if you need security)
  NimBLEDevice::setSecurityAuth(false, false, true);
  NimBLEDevice::setSecurityIOCap(BLE_HS_IO_NO_INPUT_OUTPUT);
  
  // Create server
  pServer = NimBLEDevice::createServer();
  pServer->setCallbacks(new ServerCallbacks());
  
  // Create service
  pService = pServer->createService(SERVICE_UUID);
  
  // Create characteristic with both WRITE types for flexibility
  pChar = pService->createCharacteristic(
    CHARACTERISTIC_UUID,
    NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::WRITE_NR
  );
  pChar->setCallbacks(new MouseMoveCallback());
  
  // Start service
  pService->start();
  
  // Configure advertising
  NimBLEAdvertising* pAdvertising = NimBLEDevice::getAdvertising();
  pAdvertising->addServiceUUID(SERVICE_UUID);
  
  // iOS-friendly advertising parameters (intervals in 0.625ms units)
  pAdvertising->setMinInterval(32);  // 20ms
  pAdvertising->setMaxInterval(244); // 152.5ms
  
  // Start advertising
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