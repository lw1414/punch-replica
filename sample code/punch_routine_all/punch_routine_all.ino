\

#include <PxMatrix.h>
#include <Ticker.h>
#include <HardwareSerial.h>
#include <DFRobotDFPlayerMini.h>

//////////////////////////////////////////////////
// PINS
//////////////////////////////////////////////////
#define RPM_SENSOR_PIN 35 // IO34 is available also
#define COIN_PIN       33
#define DETECT_PIN     25

#define RELAY1 27
#define RELAY2 32
#define RELAY3 26

#define MP3_RX 16
#define MP3_TX 17

//////////////////////////////////////////////////
// DISPLAY
//////////////////////////////////////////////////
#define P_A   19
#define P_B   21
#define P_OE  22
#define P_LAT 2

PxMATRIX display(32, 16, P_LAT, P_OE, P_A, P_B);
Ticker displayTicker;
uint8_t scan_counts = 0;
const uint8_t frame_scan_counts = (1 << PxMATRIX_COLOR_DEPTH);

void display_updater() {
  display.display(50); // refresh display safely
  if (++scan_counts == frame_scan_counts) scan_counts = 0;
}

//////////////////////////////////////////////////
// RPM
//////////////////////////////////////////////////
volatile int rpmPulseCount = 0;

void IRAM_ATTR countRPM() {
  rpmPulseCount++;
}

float currentRPM = 0;
unsigned long lastRPMMillis = 0;

float measureRPM() {
  int count = rpmPulseCount;
  rpmPulseCount = 0;
  return count * 30.0; // 2 sec → RPM
}

//////////////////////////////////////////////////
// COIN COUNTER
//////////////////////////////////////////////////
volatile int coinCount = 0;
bool lastCoinState = HIGH;

void checkCoin() {
  bool current = digitalRead(COIN_PIN);
  if (lastCoinState == HIGH && current == LOW) {
    coinCount++;
  }
  lastCoinState = current;
}

//////////////////////////////////////////////////
// RELAY + MP3 CONTROL
//////////////////////////////////////////////////
HardwareSerial mp3Serial(2);
DFRobotDFPlayerMini mp3;
bool mp3Ready = false;
bool mp3Playing = false;

unsigned long relayStartMillis = 0;
bool relayActive = false;
const unsigned long relayPulseTime = 300; // 300ms pulse

void triggerAction() {
  // Only RELAY1 pulses
  if (!relayActive) {
    digitalWrite(RELAY1, LOW);
    digitalWrite(RELAY2, LOW);
    digitalWrite(RELAY3, LOW);
    relayStartMillis = millis();
    relayActive = true;
  }

  // Toggle MP3
  if (!mp3Ready) return;
  if (!mp3Playing) {
    mp3.play(1);
    mp3Playing = true;
  } else {
    mp3.stop();
    mp3Playing = false;
  }
}

bool lastDetectState = LOW;

//////////////////////////////////////////////////
// SETUP
//////////////////////////////////////////////////
void setup() {
  Serial.begin(115200);

  // Pins
  pinMode(RPM_SENSOR_PIN, INPUT);
  pinMode(COIN_PIN, INPUT_PULLUP);
  pinMode(DETECT_PIN, INPUT);          // trigger on HIGH

  pinMode(RELAY1, OUTPUT);
  pinMode(RELAY2, OUTPUT);
  pinMode(RELAY3, OUTPUT);
  digitalWrite(RELAY1, HIGH);
  digitalWrite(RELAY2, HIGH);
  digitalWrite(RELAY3, HIGH);

  // RPM interrupt
  attachInterrupt(digitalPinToInterrupt(RPM_SENSOR_PIN), countRPM, FALLING);

  // MP3 INIT
  mp3Serial.begin(9600, SERIAL_8N1, MP3_RX, MP3_TX);
  if (mp3.begin(mp3Serial)) {
    mp3.volume(20);
    mp3Ready = true;
    Serial.println("MP3 Ready");
  } else {
    Serial.println("MP3 Init Fail");
  }

  // DISPLAY INIT
  display.begin(4);
  display.setTextColor(0xFF);

  // Ticker refresh for PxMatrix
  displayTicker.attach_ms(1, display_updater);
}

//////////////////////////////////////////////////
// LOOP
//////////////////////////////////////////////////
void loop() {
  unsigned long now = millis();

  // --------- Coin counting ---------
  checkCoin();

  // --------- Detect trigger (IO25 HIGH) ---------
  bool detectState = digitalRead(DETECT_PIN);
  if (detectState == HIGH && lastDetectState == LOW) {
    triggerAction();
  }
  lastDetectState = detectState;

  // --------- Non-blocking relay pulse for RELAY1 ---------
  if (relayActive && (now - relayStartMillis >= relayPulseTime)) {
    digitalWrite(RELAY1, HIGH);
    digitalWrite(RELAY2, HIGH);
    digitalWrite(RELAY3, HIGH);
    relayActive = false;
  }

  // --------- Update RPM every 2 seconds ---------
  if (now - lastRPMMillis >= 2000) {
    lastRPMMillis = now;
    currentRPM = measureRPM();
  }

  // --------- Update display every 50ms ---------
  static unsigned long lastDisplayUpdate = 0;
  if (now - lastDisplayUpdate >= 50) {
    lastDisplayUpdate = now;

    display.clearDisplay();
    display.setCursor(0, 0);
    display.print("C:");  display.print(coinCount);
    display.setCursor(0, 8);
    display.print("R:");  display.print((int)currentRPM);
    display.showBuffer();
  }
}