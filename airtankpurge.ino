//v1.0.4

//will activate compressor purge solenoid for ~5 seconds every 24 hours
//button will purge on demand
//using an ASCO 1/4" 8262H022-12/DC solenoid valve

static unsigned long int hoursToMs = 3600000;  //conversion factor for hours to milliseconds
unsigned long interval = 24 * hoursToMs; //interval between automatic purges in hours
int purgeTime = 5000; //5 seconds
int loopDelay = 250; //250 ms
unsigned long previousMillis = 0; //millis() count for last purge
int switchRead = 0; //flag for first switch read

void setup() {
  interrupts(); //should be on by default, needed for millis()

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
    switchRead = 1;
    purge();
    previousMillis = currentMillis; //on manual purge, start the 24 hour timer from "now"
  }

  //adding a delay here, it seemed the loop was running so frequently that it was picking up the switch press multiple times
  if (switchRead == 1) {
    delay(loopDelay);
    switchRead = 0;
  }
}


void purge()
{
  digitalWrite(0, HIGH);
  delay(purgeTime);
  digitalWrite(0, LOW);
  //adding a delay   
  delay(5000);
}
