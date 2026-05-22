// v1.0.5

// Automatic compressor purge every 24 hours
// Manual button purges on demand
// Using ASCO 1/4" 8262H022-12/DC solenoid valve
// Arduino UNO or Nano
// Relay: Omron G2R-1-S DC12(S) 
// 12v power for solenoid soldered directly to Arduino's input jack, with power switched through relay terminals
//
// Assumptions:
// - Relay input is active HIGH
// - Button uses INPUT_PULLUP
// - Button pressed = LOW
// - Startup test intentionally resets the 24-hour timer

const byte RELAY_PIN = 7;
const byte BUTTON_PIN = 8;

const unsigned long HOURS_TO_MS   = 3600000UL; //conversion factor for hours to milliseconds
const unsigned long INTERVAL_MS   = 24UL * HOURS_TO_MS; //interval between automatic purges in hours
const unsigned long PURGE_TIME_MS = 5000UL; //5 seconds
const unsigned long DEBOUNCE_MS   = 50UL;

unsigned long previousPurgeMillis = 0;
unsigned long lastButtonChangeMillis = 0;

bool lastButtonReading = HIGH;
bool stableButtonState = HIGH;

void setup()
{
    pinMode(RELAY_PIN, OUTPUT); //RELAY CONTROL
    pinMode(BUTTON_PIN, INPUT_PULLUP);  //MANUAL BUTTON, PRESSED = LOW

    // Ensure relay starts OFF
    digitalWrite(RELAY_PIN, LOW);

    // Startup functional test
    // NOTE:
    // This intentionally occurs on every power-up/reset
    // and resets the 24-hour purge schedule

    digitalWrite(RELAY_PIN, HIGH);
    delay(1000);      // 1 second test
    digitalWrite(RELAY_PIN, LOW);

    // Start timer after startup test
    previousPurgeMillis = millis();
}

void loop()
{
  unsigned long currentMillis = millis(); //millis() returns the number of milliseconds the Arduino has been running. Loops back to 0 after about 50 days

    // Automatic purge timer
    if (currentMillis - previousPurgeMillis >= INTERVAL_MS)
    {
        purge();
        previousPurgeMillis = millis();
    }

    // Manual button handling with debounce
    bool reading = digitalRead(BUTTON_PIN);

    // Detect state change
    if (reading != lastButtonReading)
    {
        lastButtonChangeMillis = currentMillis;
        lastButtonReading = reading;
    }

    // If stable longer than debounce period
    if ((currentMillis - lastButtonChangeMillis) >= DEBOUNCE_MS)
    {
        if (reading != stableButtonState)
        {
            stableButtonState = reading;

            // Trigger once when button is pressed
            if (stableButtonState == LOW)
            {
                purge();

                // Restart 24-hour timer from manual purge
                previousPurgeMillis = millis();
            }
        }
    }
}

void purge()
{
    digitalWrite(RELAY_PIN, HIGH);

    delay(PURGE_TIME_MS);

    digitalWrite(RELAY_PIN, LOW);
}
