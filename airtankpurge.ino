//v1.0.2

//will activate compressor purge solenoid for ~10 seconds every 24 hours
//button will purge on demand
//using an ASCO 1/4" 8262H022-12/DC solenoid valve with "safety shut-off" so valve will stay open about twice as long as relay is energized
//I assume this feature is to prevent "water hammer" when used in liquid applications (valve doesn't close instantly)

//conversion factor for hours
static unsigned long int hoursToMs = 3600000;

unsigned long interval = 24 * hoursToMs;
int purgeTime = 5000; //5 seconds
int loopDelay = 250; //250 ms
unsigned long previousMillis = 0;

void setup() {
  interrupts(); //should be on by default

  pinMode(0, OUTPUT); //RELAY CONTROL
  pinMode(1, INPUT_PULLUP);  //MANUAL BUTTON, PRESSED=LOW

  //test
  digitalWrite(0, HIGH);
  delay(1000); //1 seconds

  //reset
  digitalWrite(0, LOW);

}

void loop()
{
  unsigned long currentMillis = millis(); //millis() returns the number of milliseconds the Arduino has been running. Loops back to 0 after about 50 days

  if (currentMillis - previousMillis > interval) {
    previousMillis = currentMillis;
    purge();
  }

  if (digitalRead(1) == LOW) {
    purge();
    previousMillis = currentMillis; //on manual purge, start the 24 hour timer from "now"
  }

  //adding a delay here, it seemed the loop was running so frequently that it was picking up the switch press multiple times
  delay(loopDelay);
}


void purge()
{
  digitalWrite(0, HIGH);
  delay(purgeTime);
  digitalWrite(0, LOW);
}
