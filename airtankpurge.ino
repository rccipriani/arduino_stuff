unsigned long interval = 86400000; //24 hours
int purgeTime = 10000; //10 seconds
unsigned long previousMillis = 0;

void setup() {
  pinMode(0, OUTPUT); //RELAY CONTROL
  pinMode(1, INPUT_PULLUP);  //MANUAL BUTTON, PRESSED=LOW

  //test
  digitalWrite(0, HIGH);
  delay(2000); //2 seconds

  //reset
  digitalWrite(0, LOW);

}

void loop()
{
  unsigned long currentMillis = millis();

  if (currentMillis - previousMillis > interval) {
    previousMillis = currentMillis;
    purge();
  }

  if (digitalRead(1) == LOW) {
    purge();
  }

}


void purge()
{
  digitalWrite(0, HIGH);
  delay(purgeTime);
  digitalWrite(0, LOW);
}
