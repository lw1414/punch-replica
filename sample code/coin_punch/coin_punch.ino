// === Coin Counter (Simple Version) ===
#define COIN_PIN 33  // change to your actual pin

volatile int pulseCount = 0;
unsigned long lastPulseTime = 0;
int totalPesos = 0;

const unsigned long coinTimeout = 150;  // ms

void setup() {
  Serial.begin(115200);
  pinMode(COIN_PIN, INPUT_PULLUP);
}

void loop() {
  static bool lastState = HIGH;
  static unsigned long lastChangeTime = 0;
  static unsigned long lowStartTime = 0;

  const unsigned long debounceTime = 10;
  const unsigned long minPulseWidth = 15;

  bool currentState = digitalRead(COIN_PIN);
  unsigned long now = millis();

  // Detect falling edge (start of pulse)
  if (lastState == HIGH && currentState == LOW) {
    if (now - lastChangeTime > debounceTime) {
      lowStartTime = now;
    }
    lastChangeTime = now;
  }

  // Detect rising edge (end of pulse)
  if (lastState == LOW && currentState == HIGH) {
    unsigned long pulseWidth = now - lowStartTime;

    if (pulseWidth >= minPulseWidth) {
      pulseCount++;
      lastPulseTime = now;
      Serial.print("Pulse: ");
      Serial.println(pulseCount);
    }

    lastChangeTime = now;
  }

  lastState = currentState;

  // Convert pulses to coin value
  if (pulseCount > 0 && (now - lastPulseTime) > coinTimeout) {
    int value = 0;

    if (pulseCount >= 1 && pulseCount <= 3) value = 1;
    else if (pulseCount >= 5 && pulseCount <= 7) value = 5;
    else if (pulseCount >= 10 && pulseCount <= 14) value = 10;
    else if (pulseCount >= 16 && pulseCount <= 20) value = 20;
    
    if (value > 0) {
      totalPesos += value;
      Serial.print("Detected: ₱");
      Serial.print(value);
      Serial.print(" | Total: ₱");
      Serial.println(totalPesos);
    }

    pulseCount = 0;
  }
}