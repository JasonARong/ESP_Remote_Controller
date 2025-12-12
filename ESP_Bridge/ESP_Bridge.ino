#include <Arduino.h>
#include <USB.h>
#include <USBHID.h>
#include <USBHIDKeyboard.h>
#include <NimBLEDevice.h>

USBHID HID;
USBHIDKeyboard Keyboard;

#define SERVICE_UUID        "00001234-0000-1000-8000-00805f9b34fb"
#define CHARACTERISTIC_UUID "0000abcd-0000-1000-8000-00805f9b34fb"

NimBLEServer* pServer = nullptr;
NimBLEService* pService = nullptr;
NimBLECharacteristic* pChar = nullptr;

bool deviceConnected = false;
bool oldDeviceConnected = false;


/************** CUSTOM HID MOUSE DESCRIPTOR **************/
// Standard Mouse with 2 buttons + X/Y movement + Wheel
// Expandable for future enhancements (horizontal scroll, more buttons, etc.)
static const uint8_t MOUSE_REPORT_DESCRIPTOR[] = {
  0x05, 0x01,        // Usage Page (Generic Desktop)
  0x09, 0x02,        // Usage (Mouse)
  0xA1, 0x01,        // Collection (Application)
    0x85, HID_REPORT_ID_MOUSE, //   Report ID (mouse)
  
    0x09, 0x01,        //   Usage (Pointer)
    0xA1, 0x00,        //   Collection (Physical)
    
      // Buttons (2 buttons: left, right)
      0x05, 0x09,        //     Usage Page (Button)
      0x19, 0x01,        //     Usage Minimum (Button 1)
      0x29, 0x02,        //     Usage Maximum (Button 2)
      0x15, 0x00,        //     Logical Minimum (0)
      0x25, 0x01,        //     Logical Maximum (1)
      0x95, 0x02,        //     Report Count (2)
      0x75, 0x01,        //     Report Size (1)
      0x81, 0x02,        //     Input (Data, Variable, Absolute)
      
      // Padding (6 bits to complete the byte)
      0x95, 0x01,        //     Report Count (1)
      0x75, 0x06,        //     Report Size (6)
      0x81, 0x03,        //     Input (Constant)
      
      // X and Y relative movement (16-bit for precision)
      0x05, 0x01,        //     Usage Page (Generic Desktop)
      0x09, 0x30,        //     Usage (X)
      0x09, 0x31,        //     Usage (Y)
      0x16, 0x00, 0x80,  //     Logical Minimum (-32768)
      0x26, 0xFF, 0x7F,  //     Logical Maximum (32767)
      0x75, 0x10,        //     Report Size (16)
      0x95, 0x02,        //     Report Count (2)
      0x81, 0x06,        //     Input (Data, Variable, Relative)
      
      // Vertical Wheel
      0x09, 0x38,        //     Usage (Wheel)
      0x15, 0x81,        //     Logical Minimum (-127)
      0x25, 0x7F,        //     Logical Maximum (127)
      0x75, 0x08,        //     Report Size (8)
      0x95, 0x01,        //     Report Count (1)
      0x81, 0x06,        //     Input (Data, Variable, Relative)
    
    0xC0,              //   End Collection (Physical)
  0xC0               // End Collection (Application)
};

// Mouse report structure matching the descriptor above
typedef struct {
  uint8_t buttons;   // Bit 0: Left, Bit 1: Right
  int16_t x;         // X movement (-32768 to 32767)
  int16_t y;         // Y movement (-32768 to 32767)
  int8_t wheel;      // Wheel movement (-127 to 127)
} __attribute__((packed)) MouseReport; // Crucial: no added spaces for alignment in memory, squish all bytes together

MouseReport mouseReport = {0, 0, 0, 0};

/************** CUSTOM HID DEVICE CLASS **************/
class CustomMouseDevice : public USBHIDDevice {
public:
  CustomMouseDevice() {
    static bool initialized = false;
    if (!initialized) {
      initialized = true;
    }
  }

  uint16_t _onGetDescriptor(uint8_t* buffer) {
    memcpy(buffer, MOUSE_REPORT_DESCRIPTOR, sizeof(MOUSE_REPORT_DESCRIPTOR));
    return sizeof(MOUSE_REPORT_DESCRIPTOR);
  }

  // Send mouse report
  bool sendReport() {
    // Send using the mouse report ID so the host can route it correctly
    return HID.SendReport(HID_REPORT_ID_MOUSE, &mouseReport, sizeof(mouseReport));
  }

  // Individual control methods (for future use)
  void setButtons(uint8_t buttons) {
    mouseReport.buttons = buttons;
  }

  void setMovement(int16_t x, int16_t y) {
    mouseReport.x = x;
    mouseReport.y = y;
  }

  void setWheel(int8_t wheel) {
    mouseReport.wheel = wheel;
  }

  void reset() {
    mouseReport = {0, 0, 0, 0};
  }
};

// Custom Mouse Device
CustomMouseDevice Mouse;

/************** BLE CALLBACKS **************/
class MouseMoveCallback : public NimBLECharacteristicCallbacks {
  uint8_t lastButtonState = 0;

  void onWrite(NimBLECharacteristic* pCharacteristic, NimBLEConnInfo& connInfo) {
    std::string value = pCharacteristic->getValue();
    const uint8_t* data = (const uint8_t*)value.data();
    size_t len = value.length();

    if (len == 0) return;

    // ----- Keyboard path -----
    // Keyboard combo packets: [0xF1, N, keyId1, keyId2, keyId3]
    if (data[0] == 0xF1 && len >= 2) {
      Serial.println("Keyboard path");
      uint8_t count = data[1];
      size_t available = (len >= 2) ? (len - 2) : 0;
      size_t n = std::min<size_t>(available, count);
      handleKeyboardCombo(data + 2, n);
      return;
    }

    // ----- Mouse path -----
    // Mouse packets: [buttons, Scroll, dxLE(1), dxLE(2), dyLE(1), dyLE(2)] 
    if (value.length() != 6) {
      Serial.printf("⚠️ Got %d bytes (expected 6)\n", value.length());
      return;
    }

    // Parse button state
    uint8_t buttonState = data[0];
    
    // Parse scroll wheel (signed byte)
    int8_t wheelDelta = (int8_t)data[1];
    
    // Parse mouse movement (signed 16-bit integers)
    int16_t dx = (int16_t)(data[2] | (data[3] << 8));
    int16_t dy = (int16_t)(data[4] | (data[5] << 8));
    
    // Check for changes before updating (for debug output)
    bool buttonChanged = (buttonState != lastButtonState);
    
    // Update button state in report
    mouseReport.buttons = buttonState & 0x03; // masks out bits 2–7, keeps 0(left) and 1(right)
    lastButtonState = buttonState;
    
    mouseReport.x = dx;
    mouseReport.y = dy;
    mouseReport.wheel = wheelDelta;

    // Send the report
    Mouse.sendReport();
    
    // Reset movement and wheel for next update
    mouseReport.x = 0;
    mouseReport.y = 0;
    mouseReport.wheel = 0;
    mouseReport.buttons = 0;

    // Debug output
    if (dx != 0 || dy != 0 || wheelDelta != 0 || buttonChanged) {
      Serial.printf("📊 dx=%d dy=%d buttons=%d wheel=%d\n", 
                    dx, dy, buttonState, wheelDelta);
    }
  }

  // Handle keyboard combo packet
  void handleKeyboardCombo(const uint8_t* keyIds, size_t count) {
    if (count == 0) {
      Serial.println("ℹ️ Keyboard combo with 0 keys");
      return;
    }

    Serial.print("🎹 Keyboard combo: ");
    for (size_t i = 0; i < count; ++i) {
      Serial.printf("%u ", keyIds[i]);
    }
    Serial.println();

    // Press all keys
    for (size_t i = 0; i < count; ++i) {
      pressLogicalKey(keyIds[i], /*pressed=*/true);
    }

    // Short hold, then release
    delay(5);
    Keyboard.releaseAll();
  }

  // Map your LogicalKey IDs → HID keycodes (macOS mapping for now)
  void pressLogicalKey(uint8_t id, bool pressed) {
    uint8_t keycode = 0;

    switch (id) {
      // Must match iOS LogicalKey rawValues:
      // control=1, shift=2, alt=3, command=4, arrows=10..13
      case 1: keycode = KEY_LEFT_CTRL; break;
      case 2: keycode = KEY_LEFT_SHIFT; break;
      case 3: keycode = KEY_LEFT_ALT; break;
      case 4: keycode = KEY_LEFT_GUI; break;  // Command on Mac / Win key on Windows

      case 10: keycode = KEY_LEFT_ARROW;  break;
      case 11: keycode = KEY_RIGHT_ARROW; break;
      case 12: keycode = KEY_UP_ARROW;    break;
      case 13: keycode = KEY_DOWN_ARROW;  break;

      default:
        Serial.printf("⚠️ Unknown logical key id: %u\n", id);
        return;
    }

    if (pressed) {
      Keyboard.press(keycode);
    } else {
      Keyboard.release(keycode);
    }
  }
};

/************** SERVER CALLBACKS **************/
class ServerCallbacks : public NimBLEServerCallbacks {
  void onConnect(NimBLEServer* pServer, NimBLEConnInfo& connInfo) {
    deviceConnected = true;
    Serial.println("📶 iPhone connected");
    
    // Update connection parameters for better stability (iOS-optimized)
    pServer->updateConnParams(
      connInfo.getConnHandle(),
      12,   // min interval (15ms)
      24,   // max interval (30ms)
      0,    // latency
      400   // timeout (4000ms)
    );
    
    NimBLEDevice::getAdvertising()->stop();
    Serial.println("✅ Connection parameters updated");
  }
  
  void onDisconnect(NimBLEServer* pServer, NimBLEConnInfo& connInfo, int reason) {
    deviceConnected = false;
    Serial.printf("🔌 iPhone disconnected, reason: %d\n", reason);
    delay(100);
  }
  
  void onMTUChange(uint16_t MTU, NimBLEConnInfo& connInfo) {
    Serial.printf("📏 MTU updated to: %d\n", MTU);
  }
};

// Static callback instances to avoid memory leaks
static ServerCallbacks serverCallbacks;
static MouseMoveCallback mouseMoveCallback;

/************** SETUP **************/
void setup() {
  Serial.begin(115200);
  delay(500);
  
  /*** Initialize USB HID with custom descriptor ***/
  Serial.println("🔧 Initializing Custom HID Mouse...");
  
  // Add custom mouse device (Mouse is already initialized as global object)
  HID.addDevice(&Mouse, sizeof(MOUSE_REPORT_DESCRIPTOR));
  
  // Start USB: initialize HID, keyboard, then the USB stack
  HID.begin();
  Keyboard.begin();
  USB.begin();
  
  Serial.println("✅ Custom USB HID Mouse initialized");  
  
  /*** Initialize BLE ***/
  Serial.println("🔧 Initializing NimBLE...");
  
  NimBLEDevice::init("ESP_MouseBridge");
  NimBLEDevice::setMTU(247);
  NimBLEDevice::setPower(ESP_PWR_LVL_P6);
  NimBLEDevice::setSecurityAuth(false, false, true);
  NimBLEDevice::setSecurityIOCap(BLE_HS_IO_NO_INPUT_OUTPUT);
  
  // Create server
  pServer = NimBLEDevice::createServer();
  pServer->setCallbacks(&serverCallbacks);
  
  // Create service
  pService = pServer->createService(SERVICE_UUID);
  
  // Create characteristic
  pChar = pService->createCharacteristic(
    CHARACTERISTIC_UUID, 
    NIMBLE_PROPERTY::WRITE_NR
  );
  pChar->setCallbacks(&mouseMoveCallback);
  
  pService->start();
  
  // Configure advertising
  NimBLEAdvertising* pAdvertising = NimBLEDevice::getAdvertising();
  pAdvertising->addServiceUUID(SERVICE_UUID);
  pAdvertising->setMinInterval(32);
  pAdvertising->setMaxInterval(244);
  pAdvertising->start();
  
  Serial.println("✅ BLE advertising started");
  Serial.println("📱 Ready for iPhone connection");
  Serial.println("🎧 Listening for writes...");
  Serial.println();
}

/************** MAIN LOOP **************/
void loop() {
  // Handle connection state changes
  if (deviceConnected && !oldDeviceConnected) {
    oldDeviceConnected = deviceConnected;
  }
  
  // Restart advertising if disconnected
  if (!deviceConnected && oldDeviceConnected) {
    delay(500);
    NimBLEDevice::getAdvertising()->start();
    Serial.println("🔄 Restarting advertising...");
    oldDeviceConnected = deviceConnected;
  }
  
  delay(100);
}