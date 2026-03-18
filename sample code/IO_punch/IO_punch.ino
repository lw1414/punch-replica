  // === PIN DEFINITIONS ===
  #define RPM_SENSOR_PIN 35
  #define DETECT_PIN     25

  // === RPM VARIABLES ===
  volatile int pulseCount = 0;
  unsigned long lastTime = 0;
  float rpm = 0;

  // === DETECT VARIABLES ===
  bool lastState = HIGH;

  // === INTERRUPT FUNCTION ===
  void IRAM_ATTR countPulse() {
    pulseCount++;
  }

  void setup() {
    Serial.begin(115200);

    // RPM sensor
    pinMode(RPM_SENSOR_PIN, INPUT);
    attachInterrupt(digitalPinToInterrupt(RPM_SENSOR_PIN), countPulse, FALLING);

    // Detect pin (pulled HIGH)
    pinMode(DETECT_PIN, INPUT_PULLUP);

    Serial.println("System Ready...");
  }

  void loop() {

    // ===== RPM CALCULATION =====
    if (millis() - lastTime >= 1000) {
      detachInterrupt(RPM_SENSOR_PIN);

      rpm = pulseCount * 60.0;

      Serial.print("RPM: ");
      Serial.println(rpm);

      pulseCount = 0;
      lastTime = millis();

      attachInterrupt(digitalPinToInterrupt(RPM_SENSOR_PIN), countPulse, FALLING);
    }

    // ===== DIGITAL DETECTION (PRINT ONLY ON TRIGGER) =====
    bool currentState = digitalRead(DETECT_PIN);

    if (lastState == HIGH && currentState == LOW) {
      Serial.println("IO25 TRIGGERED");
    }

    lastState = currentState;

    delay(10); // small debounce
  }