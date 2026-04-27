//////////////////////////////////////////////////
// CONFIG
//////////////////////////////////////////////////
#define PxMATRIX_OE_INVERT 1
#define PxMATRIX_DATA_INVERT 1
#define PxMATRIX_GAMMA_PRESET 3
#define PxMATRIX_DOUBLE_BUFFER 1

#include <PxMatrix.h>
#include <HardwareSerial.h>
#include <DFRobotDFPlayerMini.h>
#include <EEPROM.h>

//////////////////////////////////////////////////
// PINS
//////////////////////////////////////////////////
#define RPM_SENSOR_PIN 35
#define COIN_PIN 33
#define BUTTON_PIN 25

#define RELAY1 26
#define RELAY2 27

#define MP3_RX 16
#define MP3_TX 17

//////////////////////////////////////////////////
// DISPLAY
//////////////////////////////////////////////////
#define P_A 19
#define P_B 21
#define P_OE 22
#define P_LAT 2

PxMATRIX display(32, 16, P_LAT, P_OE, P_A, P_B);

//////////////////////////////////////////////////
// STATE MACHINE
//////////////////////////////////////////////////
enum State {
  IDLE,
  COIN_DETECTED,
  WAIT_BUTTON,
  RELAY_PHASE,
  WAIT_RPM,
  RESULT
};

State state = IDLE;

//////////////////////////////////////////////////
// TIMING
//////////////////////////////////////////////////
unsigned long stateStart = 0;

const unsigned long RELAY_ON_TIME = 3000;  //typical 16s
const unsigned long RELAY_OFF_TIME = 3000;

//////////////////////////////////////////////////
// BOOT FIX
//////////////////////////////////////////////////
bool systemReady = false;
unsigned long bootTime = 0;

//////////////////////////////////////////////////
// RPM - IMPROVED
//////////////////////////////////////////////////
volatile unsigned long lastPulseTime = 0;
volatile unsigned long pulseInterval = 0;
volatile int rpmPulseCount = 0;

void IRAM_ATTR countRPM() {
  unsigned long now = micros();
  unsigned long interval = now - lastPulseTime;

  // Debounce: ignore pulses faster than 1ms (60,000 RPM)
  if (interval > 1000) {
    pulseInterval = interval;
    lastPulseTime = now;
    rpmPulseCount++;
  }
}

// Calculate RPM from pulse interval (instantaneous)
float getInstantRPM() {
  noInterrupts();
  unsigned long interval = pulseInterval;
  unsigned long timeSinceLastPulse = micros() - lastPulseTime;
  interrupts();

  // If no pulse in last 500ms, assume stopped
  if (timeSinceLastPulse > 500000) {
    return 0.0;
  }

  // If we have a valid interval
  if (interval > 0) {
    // RPM = (60,000,000 microseconds/minute) / (interval in microseconds)
    float rpm = 60000000.0 / interval;
    return rpm;
  }

  return 0.0;
}

// Alternative: Average RPM over time window
float getAverageRPM(unsigned long windowMs = 1000) {
  static unsigned long lastCheck = 0;
  static int lastCount = 0;

  unsigned long now = millis();
  unsigned long elapsed = now - lastCheck;

  if (elapsed >= windowMs) {
    noInterrupts();
    int count = rpmPulseCount;
    interrupts();

    int pulses = count - lastCount;
    lastCount = count;
    lastCheck = now;

    // RPM = (pulses / elapsed) * 60000
    float rpm = (pulses * 60000.0) / elapsed;
    return rpm;
  }

  return 0.0;
}

//////////////////////////////////////////////////
// PEAK RPM DETECTION
//////////////////////////////////////////////////
float peakRPM = 0.0;
unsigned long rpmSampleStart = 0;
const unsigned long RPM_SAMPLE_WINDOW = 10000;  // 10 seconds to punch (increased from 3s)

void resetPeakRPM() {
  peakRPM = 0.0;
  rpmSampleStart = millis();
  noInterrupts();
  rpmPulseCount = 0;
  pulseInterval = 0;
  lastPulseTime = micros();
  interrupts();
}

bool updatePeakRPM() {
  float currentRPM = getInstantRPM();

  if (currentRPM > peakRPM) {
    peakRPM = currentRPM;
  }

  // Return true if we found a punch (RPM > threshold)
  if (currentRPM > 10.0) {
    return true;
  }

  return false;
}

//////////////////////////////////////////////////
// COIN
//////////////////////////////////////////////////
volatile int coinPulseCount = 0;
unsigned long lastCoinPulseTime = 0;
bool coinDetected = false;

void checkCoin() {
  static bool lastState = HIGH;
  static unsigned long lowStartTime = 0;

  bool currentState = digitalRead(COIN_PIN);
  unsigned long now = millis();

  if (lastState == HIGH && currentState == LOW) {
    lowStartTime = now;
  }

  if (lastState == LOW && currentState == HIGH) {
    if (now - lowStartTime > 15) {
      coinPulseCount++;
      lastCoinPulseTime = now;
    }
  }

  lastState = currentState;

  if (coinPulseCount > 0 && (now - lastCoinPulseTime) > 150) {
    if (coinPulseCount >= 10 && coinPulseCount <= 14) {
      coinDetected = true;
    }
    coinPulseCount = 0;
  }
}

//////////////////////////////////////////////////
// BUTTON
//////////////////////////////////////////////////
bool buttonPressed() {
  static int initialized = 0;
  static bool last = HIGH;

  if (!initialized) {
    last = digitalRead(BUTTON_PIN);
    initialized = 1;
    return false;
  }

  bool now = digitalRead(BUTTON_PIN);
  bool pressed = (last == HIGH && now == LOW);
  last = now;
  return pressed;
}

//////////////////////////////////////////////////
// MP3
//////////////////////////////////////////////////
HardwareSerial mp3Serial(2);
DFRobotDFPlayerMini mp3;
bool mp3Ready = false;

void playTrack(int t) {
  if (mp3Ready) mp3.play(t);
}

//////////////////////////////////////////////////
// SCORE SYSTEM - IMPROVED
//////////////////////////////////////////////////
int score = 0;
int highScore = 0;
float lastRPM = 0;

// Score calculation: HIGHER RPM = HIGHER SCORE
int calculateScore(float rpm) {
  // Define your RPM ranges
  const float MIN_RPM = 10.0;   // Minimum detectable punch
  const float MAX_RPM = 200.0;  // Maximum expected RPM (adjust based on testing)

  // Clamp RPM to valid range
  float clampedRPM = constrain(rpm, MIN_RPM, MAX_RPM);

  // Normalize to 0-1 range
  float normalized = (clampedRPM - MIN_RPM) / (MAX_RPM - MIN_RPM);

  // Apply power curve for better feel (makes mid-range scores easier to achieve)
  // Power < 1.0 = easier to get high scores
  // Power = 1.0 = linear
  // Power > 1.0 = harder to get high scores
  float curved = pow(normalized, 0.85);

  // Scale to 0-999
  int finalScore = (int)(curved * 999.0);

  return constrain(finalScore, 0, 999);
}

// Alternative: Tiered scoring system
int calculateScoreTiered(float rpm) {
  if (rpm < 20) return map(rpm, 0, 20, 0, 100);
  else if (rpm < 50) return map(rpm, 20, 50, 100, 300);
  else if (rpm < 100) return map(rpm, 50, 100, 300, 600);
  else if (rpm < 150) return map(rpm, 100, 150, 600, 850);
  else return map(rpm, 150, 250, 850, 999);
}

//////////////////////////////////////////////////
// SCORE ANIMATION
//////////////////////////////////////////////////
int displayedScore = 0;
unsigned long scoreAnimTimer = 0;

void animateScore() {
  if (displayedScore < score) {
    if (millis() - scoreAnimTimer > 8) {
      displayedScore++;
      scoreAnimTimer = millis();
    }
  }
}

//////////////////////////////////////////////////
// IDLE CONTROL
//////////////////////////////////////////////////
unsigned long idleTimer = 0;
bool idleTrackPlaying = false;

//////////////////////////////////////////////////
// RELAY CONTROL
//////////////////////////////////////////////////
unsigned long relayStart = 0;

//////////////////////////////////////////////////
// HELPER
//////////////////////////////////////////////////


//////////////////////////////////////////////////
// STATE CHANGE
//////////////////////////////////////////////////
void enterState(State newState) {
  state = newState;
  stateStart = millis();

  switch (state) {

    case IDLE:
      idleTrackPlaying = false;
      idleTimer = 0;
      break;

    case COIN_DETECTED:
      playTrack(2);
      break;

    case WAIT_BUTTON:
      playTrack(3);
      break;

    case RELAY_PHASE:
      playTrack(4);
      relayStart = millis();
      break;

    case WAIT_RPM:
      resetPeakRPM();
      break;

    case RESULT:
      displayedScore = 0;
      scoreAnimTimer = millis();
      break;
  }
}

//////////////////////////////////////////////////
// SETUP
//////////////////////////////////////////////////
void setup() {
  Serial.begin(115200);

  pinMode(RPM_SENSOR_PIN, INPUT);
  pinMode(COIN_PIN, INPUT_PULLUP);
  pinMode(BUTTON_PIN, INPUT_PULLUP);

  pinMode(RELAY1, OUTPUT);
  pinMode(RELAY2, OUTPUT);

  digitalWrite(RELAY1, HIGH);
  digitalWrite(RELAY2, HIGH);

  attachInterrupt(digitalPinToInterrupt(RPM_SENSOR_PIN), countRPM, FALLING);

  EEPROM.begin(64);
  EEPROM.get(0, highScore);
  if (highScore < 0 || highScore > 999) highScore = 0;

  mp3Serial.begin(9600, SERIAL_8N1, MP3_RX, MP3_TX);
  if (mp3.begin(mp3Serial)) {
    mp3.volume(20);
    mp3Ready = true;
  }

  display.begin(4);
  display.setTextColor(0xFF);

  buttonPressed();
  bootTime = millis();
}

//////////////////////////////////////////////////
// LOOP
//////////////////////////////////////////////////
void loop() {

  if (!systemReady) {
    if (millis() - bootTime > 1500) systemReady = true;
    else return;
  }

  display.display(50);
  checkCoin();

  switch (state) {
    case IDLE:
      {
        static int x = display.width();
        static unsigned long lastMove = 0;

        static int btnCount = 0;
        static unsigned long lastBtnTime = 0;

        // 🔥 mode control
        static bool showStatic = false;
        static unsigned long staticStart = 0;

        if (buttonPressed()) {
          if (millis() - lastBtnTime < 2000 || btnCount == 0) {
            btnCount++;
          } else {
            btnCount = 1;
          }

          lastBtnTime = millis();

          if (btnCount >= 3) {
            highScore = 0;
            EEPROM.put(0, highScore);
            EEPROM.commit();
            btnCount = 0;
          }
        }

        display.clearDisplay();
        display.setTextColor(0xFF);

        // ===== SWEEP TEXT (WORDS ONLY) =====
        const char* title = "HIGH SCORE";
        int titleWidth = strlen(title) * 12;

        if (!showStatic) {

          display.setTextSize(2);
          display.setCursor(x, 1);
          display.print(title);

          if (millis() - lastMove > 35) {
            x--;
            lastMove = millis();
          }

          if (x < -titleWidth) {
            showStatic = true;
            staticStart = millis();
          }

        } else {
          // ===== STATIC BIG NUMBER (textSize 2, compressed to fit 32px) =====
          char scoreBuf[16];
          snprintf(scoreBuf, sizeof(scoreBuf), "%d", highScore);

          int len = strlen(scoreBuf);

          display.setTextSize(2);

          int xPos = 2;  // start at edge
          int yPos = 1;

          int spacing = 9;  // 🔥 key: compress from 12 → 10

          for (int i = 0; i < len; i++) {
            display.setCursor(xPos, yPos);
            display.print(scoreBuf[i]);

            xPos += spacing;
          }

          // stay for 2 seconds
          if (millis() - staticStart > 2000) {
            showStatic = false;
            x = display.width();  // restart sweep
          }
        }

        display.showBuffer();
        display.setTextSize(1);

        // ===== ORIGINAL LOGIC =====
        if (coinDetected) {
          coinDetected = false;
          enterState(COIN_DETECTED);
        }

        if (!idleTrackPlaying) {
          playTrack(1);
          idleTrackPlaying = true;
          idleTimer = millis();
        }

        if (millis() - idleTimer > 180000) {
          idleTrackPlaying = false;
        }

        break;
      }

    case COIN_DETECTED:
      {
        char buf[48];
        snprintf(buf, sizeof(buf), "COIN");

        display.clearDisplay();
        display.setTextColor(0xFF);
        display.setTextSize(1);

        int x = 4;
        int y = 4;

        // 🔥 THICK TEXT (2-pass)
        display.setCursor(x, y);
        display.print(buf);

        display.setCursor(x + 1, y);
        display.print(buf);

        display.showBuffer();

        if (millis() - stateStart > 1200)
          enterState(WAIT_BUTTON);

        break;
      }

    case WAIT_BUTTON:
      {
        display.clearDisplay();
        display.setTextColor(0xFF);
        display.setTextSize(1);

        // ===== TOP: PUSH =====
        const char* top = "PUSH";
        int topWidth = strlen(top) * 6;
        int xTop = (display.width() - topWidth) / 2;

        display.setCursor(xTop, 0);
        display.print(top);

        // ===== BOTTOM: BTN =====
        const char* bottom = "BTN";
        int botWidth = strlen(bottom) * 6;
        int xBot = (display.width() - botWidth) / 2;

        display.setCursor(xBot, 8);
        display.print(bottom);

        display.showBuffer();

        if (buttonPressed())
          enterState(RELAY_PHASE);

        break;
      }

    case RELAY_PHASE:
      {
        // ===== STATIC WAIT DISPLAY (like COIN) =====
        char buf[16];
        snprintf(buf, sizeof(buf), "WAIT");

        display.clearDisplay();
        display.setTextColor(0xFF);
        display.setTextSize(1);

        int textWidth = strlen(buf) * 6;
        int x = (display.width() - textWidth) / 2;
        int y = (display.height() - 8) / 2;

        display.setCursor(x, y);
        display.print(buf);

        display.showBuffer();

        // ===== EXISTING LOGIC (UNCHANGED) =====
        unsigned long t = millis() - relayStart;

        if (t < RELAY_ON_TIME) {
          digitalWrite(RELAY1, LOW);
          digitalWrite(RELAY2, LOW);
        } else if (t < RELAY_ON_TIME + RELAY_OFF_TIME) {
          digitalWrite(RELAY1, HIGH);
          digitalWrite(RELAY2, HIGH);
        } else {
          enterState(WAIT_RPM);
        }

        break;
      }

    case WAIT_RPM:
      {
        float currentRPM = getInstantRPM();

        // ===== BLINK CONTROL =====
        static bool visible = true;
        static unsigned long lastBlink = 0;

        if (millis() - lastBlink > 250) {  // blink speed (ms)
          visible = !visible;
          lastBlink = millis();
        }

        // ===== DISPLAY =====
        display.clearDisplay();
        display.setTextColor(0xFF);
        display.setTextSize(1);

        if (visible) {
          char buf[16];
          snprintf(buf, sizeof(buf), "PUNCH");

          display.setCursor(1, 3);
          display.print(buf);
        }

        display.showBuffer();

        // ===== ORIGINAL LOGIC (UNCHANGED) =====
        if (currentRPM > peakRPM) {
          peakRPM = currentRPM;
        }

        if (peakRPM > 15.0 && currentRPM < 5.0) {
          lastRPM = peakRPM;
          score = calculateScore(peakRPM);

          Serial.print("FINAL RPM: ");
          Serial.print(peakRPM);
          Serial.print(" SCORE: ");
          Serial.println(score);

          enterState(RESULT);
        }

        else if (millis() - rpmSampleStart > RPM_SAMPLE_WINDOW) {
          lastRPM = peakRPM;
          score = calculateScore(peakRPM);
          enterState(RESULT);
        }

        break;
      }

    case RESULT:
      {
        animateScore();

        display.clearDisplay();

        // ===== HIGH SCORE CHECK =====
        static bool highScoreTriggered = false;
        static bool isHighScore = false;

        if (!highScoreTriggered) {
          if (score > highScore) {
            highScore = score;
            EEPROM.put(0, highScore);
            EEPROM.commit();
            isHighScore = true;
            playTrack(5);
          } else {
            isHighScore = false;
          }
          highScoreTriggered = true;
        }

        // ===== BLINK CONTROL (FIXED) =====
        bool visible = true;
        static bool blinkState = true;
        static unsigned long lastBlink = 0;

        // 👉 ONLY BLINK AFTER COUNTING IS DONE
        if (isHighScore && displayedScore >= score) {
          if (millis() - lastBlink > 200) {
            blinkState = !blinkState;
            lastBlink = millis();
          }

          visible = blinkState;
        }

        // ===== DISPLAY SCORE (SMART COMPRESSED BIG) =====
        if (visible) {

          char buf[16];
          snprintf(buf, sizeof(buf), "%d", displayedScore);

          int len = strlen(buf);

          display.setTextSize(2);  // big

          int x = 0;
          int y = 1;

          // 🔥 SMART SPACING
          int baseSpacing = 9;
          int extraSpacing = 1;

          for (int i = 0; i < len; i++) {

            display.setCursor(x, y);
            display.print(buf[i]);

            x += baseSpacing;

            if (len == 3 && i < 2) {
              x += extraSpacing;
            }
          }

          display.setTextSize(1);
        }

        display.showBuffer();

        // ===== EXIT =====
        if (millis() - stateStart > 15000) {
          highScoreTriggered = false;
          enterState(IDLE);
        }

        break;
      }
  }
}  //stop
