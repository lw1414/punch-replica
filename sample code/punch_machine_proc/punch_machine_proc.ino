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

const unsigned long RELAY_ON_TIME  = 16000;
const unsigned long RELAY_OFF_TIME = 16000;


//////////////////////////////////////////////////
// BOOT FIX
//////////////////////////////////////////////////
bool systemReady = false;
unsigned long bootTime = 0;

//////////////////////////////////////////////////
// RPM
//////////////////////////////////////////////////
volatile int rpmPulse = 0;

void IRAM_ATTR countRPM() {
  rpmPulse++;
}

float getRPM() {
  int c = rpmPulse;
  rpmPulse = 0;
  return c * 30.0;
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
// FIX: seed 'last' from actual pin state to avoid
//      a false-press on the very first call after boot.
//////////////////////////////////////////////////
bool buttonPressed() {
  static int initialized = 0;
  static bool last = HIGH;

  if (!initialized) {
    last = digitalRead(BUTTON_PIN);  // ← read real state first
    initialized = 1;
    return false;                    // never trigger on first call
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
// SCORE SYSTEM
//////////////////////////////////////////////////
int score = 0;
int highScore = 0;
float lastRPM = 0;

//////////////////////////////////////////////////
// SCORE ANIMATION
//////////////////////////////////////////////////
int displayedScore = 0;
unsigned long scoreAnimTimer = 0;

void animateScore() {
  if (displayedScore < score) {
    if (millis() - scoreAnimTimer > 5) {
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
// HELPER: draw text and flush to panel
// FIX: centralises clearDisplay + setCursor + showBuffer
//      so no state ever forgets to call showBuffer().
//////////////////////////////////////////////////
void showText(const char* line1, const char* line2 = nullptr) {
  display.clearDisplay();
  display.setCursor(0, 0);
  display.print(line1);
  if (line2) {
    display.setCursor(0, 8);
    display.print(line2);
  }
  display.showBuffer();   // ← was missing in several states
}

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

  // Seed the button de-bounce state so the very first loop()
  // call does not see a phantom falling edge.
  buttonPressed();

  bootTime = millis();
}

//////////////////////////////////////////////////
// LOOP
//////////////////////////////////////////////////
void loop() {

  // BOOT LOCK
  if (!systemReady) {
    if (millis() - bootTime > 1500) systemReady = true;
    else return;
  }

  display.display(50);
  checkCoin();

  switch (state) {

    // --------------------------------------------------
case IDLE:
{
  static int x = display.width();
  static unsigned long lastMove = 0;

  char buf[32];
  snprintf(buf, sizeof(buf), "HIGH SCORE:%d", highScore);

  display.clearDisplay();

  // -----------------------------
  // BIG TEXT EMULATION (LOCAL ONLY)
  // -----------------------------
  display.setTextColor(0xFF);

  // scale factor ONLY for this state
  display.setTextSize(2);

  int textWidth = strlen(buf) * 12;

  display.setCursor(x, 1);
  display.print(buf);

  display.showBuffer();

  // reset immediately so other states are NOT affected
  display.setTextSize(1);

  // scroll logic
  if (millis() - lastMove > 35) {
    x--;

    if (x < -textWidth) {
      x = display.width();
    }

    lastMove = millis();
  }

  // ---- EXISTING LOGIC (UNCHANGED) ----
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
    // --------------------------------------------------
    case COIN_DETECTED:
      showText("COIN OK");          // FIX: was missing showBuffer()

      if (millis() - stateStart > 1200)
        enterState(WAIT_BUTTON);
      break;

    // --------------------------------------------------
    case WAIT_BUTTON:
      showText("PUSH BUTTON");      // FIX: was missing showBuffer()

      if (buttonPressed())
        enterState(RELAY_PHASE);
      break;

    // --------------------------------------------------
    case RELAY_PHASE:
    {
      showText("WAIT");             // FIX: was missing showBuffer()

      unsigned long t = millis() - relayStart;

      if (t < RELAY_ON_TIME) {
        digitalWrite(RELAY1, LOW);
        digitalWrite(RELAY2, LOW);
      }
      else if (t < RELAY_ON_TIME + RELAY_OFF_TIME) {
        digitalWrite(RELAY1, HIGH);
        digitalWrite(RELAY2, HIGH);
      }
      else {
        rpmPulse = 0;
        enterState(WAIT_RPM);
      }
      break;
    }

    // --------------------------------------------------
    case WAIT_RPM:
    {
      showText("PUNCH");            // FIX: was missing showBuffer()

      float rpm = getRPM();

      if (rpm > 5) {
        lastRPM = rpm;

        float capped = rpm;
        if (capped > 120.0f) capped = 120.0f;

        score = constrain((int)(pow(capped / 120.0f, 1.8f) * 999), 0, 999);

        enterState(RESULT);
      }
      break;
    }

    // --------------------------------------------------
    case RESULT:
    {
      animateScore();

      display.clearDisplay();

      if (score > highScore) {
        highScore = score;
        EEPROM.put(0, highScore);
        EEPROM.commit();

        playTrack(5);

        char buf[16];
        snprintf(buf, sizeof(buf), "HIGH:%d", highScore);
        display.setCursor(0, 0);
        display.print(buf);
      } else {
        char buf[16];
        snprintf(buf, sizeof(buf), "S:%d", displayedScore);
        display.setCursor(0, 0);
        display.print(buf);
      }

      {
        char buf[16];
        snprintf(buf, sizeof(buf), "RPM:%d", (int)lastRPM);
        display.setCursor(0, 8);
        display.print(buf);
      }

      display.showBuffer();   // ← was present but kept explicit here

      if (millis() - stateStart > 8000) {
        enterState(IDLE);
      }
      break;
    }
  }
}
