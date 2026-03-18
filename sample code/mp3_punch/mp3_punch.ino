#include <HardwareSerial.h>
#include <DFRobotDFPlayerMini.h>


HardwareSerial mp3Serial(2);
DFRobotDFPlayerMini mp3;

void setup() {
  Serial.begin(115200);

  // RX = 16, TX = 17
  mp3Serial.begin(9600, SERIAL_8N1, 16, 17);

  Serial.println("Initializing MP3 module...");

  if (!mp3.begin(mp3Serial)) {
    Serial.println("MP3 module not found!");
    while (true);
  }

  mp3.volume(25);

  Serial.println("Ready.");
  Serial.println("Commands:");
  Serial.println("play 0001");
  Serial.println("pause");
  Serial.println("resume");
  Serial.println("stop");
  Serial.println("vol 20");
}

void loop() {
  if (!Serial.available()) return;

  String cmd = Serial.readStringUntil('\n');
  cmd.trim();

  // ---- PLAY COMMAND ----
  if (cmd.startsWith("play")) {
    String trackStr = cmd.substring(5); // get "0001"
    trackStr.trim();

    int trackNum = trackStr.toInt(); // converts "0001" -> 1

    if (trackNum > 0) {
      Serial.print("Playing track: ");
      Serial.println(trackStr);
      mp3.play(trackNum);
    } else {
      Serial.println("Invalid track number");
    }
  }

  // ---- VOLUME COMMAND ----
  else if (cmd.startsWith("vol")) {
    String volStr = cmd.substring(4);
    volStr.trim();

    int vol = volStr.toInt();

    if (vol >= 0 && vol <= 30) {
      mp3.volume(vol);
      Serial.print("Volume set to: ");
      Serial.println(vol);
    } else {
      Serial.println("Volume must be 0-30");
    }
  }

  // ---- OTHER COMMANDS ----
  else if (cmd == "pause") {
    mp3.pause();
    Serial.println("Paused");
  }
  else if (cmd == "resume") {
    mp3.start();
    Serial.println("Resumed");
  }
  else if (cmd == "stop") {
    mp3.stop();
    Serial.println("Stopped");
  }
  else {
    Serial.println("Unknown command");
  }
}