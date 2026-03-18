#define RELAY1 27
#define RELAY2 32
#define RELAY3 26

void setup() {
  Serial.begin(115200);
  pinMode(RELAY1, OUTPUT);
  pinMode(RELAY2,OUTPUT);
  pinMode(RELAY3,OUTPUT);
}

void loop() {

  digitalWrite(RELAY1, LOW);  // ON
  digitalWrite(RELAY2, LOW);  // ON
  digitalWrite(RELAY3, LOW);  // ON
  Serial.println("LOW");
  delay(2000);

  digitalWrite(RELAY1, HIGH);  // OFF
  digitalWrite(RELAY2, HIGH);  // OFF
  digitalWrite(RELAY3, HIGH); 
  Serial.println("HIGH");
  delay(2000);
}