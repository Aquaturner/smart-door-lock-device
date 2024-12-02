  #include <SPI.h>
  #include <MFRC522.h>
  #include <ESP32Servo.h>
  #include <Keypad.h>
  #include <Adafruit_Fingerprint.h>
  #include <HardwareSerial.h>
  #include <WiFi.h>
  #include <HTTPClient.h>


  // RFID Pins
  #define RST_PIN 22    // Reset pin
  #define SS_PIN 21    // SDA pin (Chip Select)
  //finger pins
  #define RX_PIN 16  // ESP32 RX pin connected to AS608 TX
  #define TX_PIN 17  // ESP32 TX pin connected to AS608 RX
  HardwareSerial mySerial(2);  // Use UART2
  Adafruit_Fingerprint finger(&mySerial);
  // Servo Pin
  #define SERVO_PIN 15
  // Hall
  #define HALL_SENSOR_PIN 34 
  // Keypad setup
  const byte ROWS = 4;  // Four rows
  const byte COLS = 4;  // Four columns
  char keys[ROWS][COLS] = {
  {'1', '2', '3', 'A'},
  {'4', '5', '6', 'B'},
  {'7', '8', '9', 'C'},
  {'*', '0', '#', 'D'}
  };
  byte rowPins[ROWS] = {13, 12, 14, 27};  // Connect to row pins of the keypad
  byte colPins[COLS] = {26, 25, 33, 32};  // Connect to column pins of the keypad
  Keypad keypad = Keypad(makeKeymap(keys), rowPins, colPins, ROWS, COLS);

  // Create RFID and Servo objects
  MFRC522 rfid(SS_PIN, RST_PIN);
  Servo lockServo;

  // Authorized RFID Tag UID (replace with your tag's UID)
  String authorizedUID1 = "EE2F1A2";  // Example UID (update with your own)
  String authorizedUID2 = "E348C12C";  // Example UID (update with your own)
  // PIN setup
  String validPIN = "1234";  // Predefined valid PIN
  String enteredPIN = "";    // User-entered PIN


  // Lock state
  bool isUnlocked = false;

  // Wi-Fi credentials
  const char *ssid = "BOom";
  const char *password = "13579000";

  // ESP32-CAM IP
  const char* cameraIP = "http://172.20.10.8/capture";

  void setup() {
    // Start Serial Monitor
    Serial.begin(115200);
    Serial.println("RFID Security System Starting...");

    // Initialize SPI bus and RFID module
    SPI.begin();
    rfid.PCD_Init();
    Serial.println("RFID reader initialized.");

    // Initialize Servo
    lockServo.attach(SERVO_PIN);
    lockServo.write(0);  // Lock position
    Serial.println("Servo initialized and locked.");

    // Keypad instructions
    Serial.println("Enter PIN on the keypad to unlock.");

    Serial.begin(115200);
    mySerial.begin(57600, SERIAL_8N1, RX_PIN, TX_PIN);

    if (finger.verifyPassword()) {
      Serial.println("Fingerprint sensor detected!");
    } else {
      Serial.println("Fingerprint sensor not detected. Check connections.");
      while (1) delay(1);  // Halt if the sensor isn't detected
    }
    Serial.println("Ready to verify fingerprints.");

    // Wi-Fi connection
    WiFi.begin(ssid, password);
    while (WiFi.status() != WL_CONNECTED) {
      delay(500);
      Serial.print(".\n");
    }
    Serial.println("Wi-Fi connected.");
    // Set the Hall effect sensor pin as input
    pinMode(HALL_SENSOR_PIN, INPUT);
  }

  void loop() {
    // Check if an RFID card is present
    if (rfid.PICC_IsNewCardPresent() && rfid.PICC_ReadCardSerial()) {
      handleRFID();  // Handle the scanned RFID
    }
      // Check keypad input
    char key = keypad.getKey();
    if (key) {
      handleKeypadInput(key);
    }
    // Check fingerprint input
    handleFingerprint();

    int sensorState = digitalRead(HALL_SENSOR_PIN);
    if (sensorState == HIGH) {
      //Serial.println("Door is closed");
    } else {
      // If the sensor reads LOW, the door is open (magnet is away)
      //Serial.println("Door is open");
      captureAndSendImage();
    }

    delay(100);  // Small delay for stability
  }

  // Handle scanned RFID
  void handleRFID() {
    // Read the UID from the RFID tag
    String uid = "";
    for (byte i = 0; i < rfid.uid.size; i++) {
      uid += String(rfid.uid.uidByte[i], HEX);
    }
    uid.toUpperCase();  // Convert to uppercase for comparison
    Serial.print("Scanned RFID UID: ");
    Serial.println(uid);

    // Check if the UID matches the authorized UID
    if ((uid == authorizedUID1) || uid == authorizedUID2) {
      if (!isUnlocked) {
        Serial.println("Access Granted. Unlocking...");
        unlock();
      } else {
        Serial.println("Already unlocked!");
      }
    } else {
      Serial.println("Access Denied. Unauthorized RFID.");
      captureAndSendImage();
    }

    // Halt the RFID card
    rfid.PICC_HaltA();
  }

  // Handle keypad input
void handleKeypadInput(char key) {
  if (key == '#') {
    // Submit PIN
    if (enteredPIN == validPIN) {
      Serial.println("PIN correct, unlocking...");
      unlock();
    } else {
      Serial.println("Invalid PIN.");
      captureAndSendImage();
    }
    enteredPIN = "";  // Clear the PIN
  } else if (key == '*') {
    // Clear PIN
    Serial.println("PIN cleared.");
    enteredPIN = "";
  } else {
    // Add key to entered PIN
    enteredPIN += key;
    Serial.print("Entered PIN: ");
    Serial.println(enteredPIN);
  }
}

// Handle fingerprint input
void handleFingerprint() {
  int result = finger.getImage();
  if (result == FINGERPRINT_NOFINGER) {
    return;  // No finger detected
  }

  if (result == FINGERPRINT_OK) {
    Serial.println("Fingerprint detected.");
    result = finger.image2Tz();
    if (result == FINGERPRINT_OK) {
      result = finger.fingerFastSearch();
      if (result == FINGERPRINT_OK) {
        Serial.print("Access Granted via Fingerprint! ID: ");
        Serial.println(finger.fingerID);
        unlock();
      } else {
        Serial.println("Access Denied. Fingerprint not found.");
        captureAndSendImage();
      }
    } else {
      Serial.println("Error converting fingerprint to template.");
    }
  } else {
    Serial.println("Error capturing fingerprint image.");
  }
}

// Unlock function
void unlock() {
  isUnlocked = true;
  lockServo.write(90);  // Unlock position
  delay(5000);          // Keep unlocked for 5 seconds
  lockServo.write(0);   // Lock position
  isUnlocked = false;
  Serial.println("System locked again.");
}

void captureAndSendImage() {
  HTTPClient http;

  Serial.println("Sending request to capture image...");

  http.begin(cameraIP);  // Send request to ESP32-CAM
  int httpCode = http.GET();  // Execute HTTP GET

  if (httpCode == 200) {  // Success
    Serial.println("Image capture successful. Sending data to PC...");

    // Stream image data to Serial
    WiFiClient* stream = http.getStreamPtr();
    uint8_t buffer[512];
    size_t size;

    // Start the data transmission with a header
    Serial.println("START_IMAGE");

    while ((size = stream->available()) > 0) {
      size = stream->readBytes(buffer, min(size, sizeof(buffer)));
      //Serial.write(buffer, size);
    }

    // End the data transmission with a footer
    Serial.println("END_IMAGE");

    Serial.println("Image sent to PC");
  } else {
    Serial.printf("Failed to capture image. HTTP error: %d\n", httpCode);
  }

  http.end();
}
