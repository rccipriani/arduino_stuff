// Project: Compressor Auto Purge Controller
// Author: Robert Cipriani
// Last Updated: 2026-06-04
//
// v1.1.0
//
// Hardware:
// - Arduino UNO
// - ASCO 1/4" 8262H022-12/DC solenoid valve
// - Omron G2R-1-S DC12(S) relay
//
// Features:
// - Automatic purge every 24 hours
// - Manual purge button with debounce
// - Startup functional test (1 second)
// - Non-blocking timing using millis()
// - Status LED indication
//     Slow blink = controller running normally
//     Fast blink = purge cycle active
// - Manual purge resets 24-hour timer
// - Startup test intentionally resets 24-hour timer
//
// Assumptions:
// - Relay input is active HIGH
// - Button uses INPUT_PULLUP
// - Button pressed = LOW
// - Relay output energizes purge solenoid
//
// Future Expansion:
// - Pressure transmitter feedback
// - Purge confirmation logic
// - Fault/alarm indication
// - Runtime statistics
// - PLC migration (Allen-Bradley Micro820)
//
// Revision History:
// v1.0.0 Initial release
// v1.0.5 Debounced manual purge button
// v1.1.0 Non-blocking state machine and status LED support

const byte RELAY_PIN = 7;
const byte BUTTON_PIN = 8;
const byte LED_PIN = 13;

const unsigned long HOURS_TO_MS = 3600000UL;          // conversion factor: hours to milliseconds
const unsigned long INTERVAL_MS = 24UL * HOURS_TO_MS; // 24-hour interval between automatic purges
const unsigned long PURGE_TIME_MS = 5000UL;           // purge duration: 5 seconds
const unsigned long DEBOUNCE_MS = 50UL;               // button debounce time

const unsigned long LED_NORMAL_INTERVAL = 1000UL;     // slow blink while running
const unsigned long LED_PURGE_INTERVAL = 125UL;       // fast blink while purging

unsigned long previousPurgeMillis = 0;
unsigned long lastButtonChangeMillis = 0;

// Startup test
bool startupTestActive = true;
unsigned long startupTestStartMillis = 0;

// Purge state
bool purgeActive = false;
unsigned long purgeStartMillis = 0;

// Button debounce
bool lastButtonReading = HIGH;
bool stableButtonState = HIGH;

// Status LED
bool ledState = LOW;
unsigned long lastLedToggleMillis = 0;

void setup()
{
    pinMode(RELAY_PIN, OUTPUT);          // RELAY CONTROL
    pinMode(BUTTON_PIN, INPUT_PULLUP);   // MANUAL BUTTON, PRESSED = LOW
    pinMode(LED_PIN, OUTPUT);            // STATUS LED

    // Ensure relay starts OFF
    digitalWrite(RELAY_PIN, LOW);

    // Startup functional test
    // NOTE:
    // This intentionally occurs on every power-up/reset
    // and resets the 24-hour purge schedule

    digitalWrite(RELAY_PIN, HIGH);

    startupTestActive = true;
    startupTestStartMillis = millis();
}

void loop()
{
    unsigned long currentMillis = millis();

    //--------------------------------------
    // Startup Test
    //--------------------------------------

    if (startupTestActive)
    {
        if (currentMillis - startupTestStartMillis >= 1000UL)
        {
            digitalWrite(RELAY_PIN, LOW);

            startupTestActive = false;

            // Start 24-hour timer after startup test
            previousPurgeMillis = currentMillis;
        }
    }

    //--------------------------------------
    // Automatic Purge Timer
    //--------------------------------------

    if (!startupTestActive && !purgeActive)
    {
        if (currentMillis - previousPurgeMillis >= INTERVAL_MS)
        {
            startPurge(currentMillis);
        }
    }

    //--------------------------------------
    // Manual Button Handling with Debounce
    //--------------------------------------

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
                startPurge(currentMillis);

                // Restart 24-hour timer from manual purge
                previousPurgeMillis = currentMillis;
            }
        }
    }

    //--------------------------------------
    // Purge State Machine
    //--------------------------------------

    if (purgeActive)
    {
        if (currentMillis - purgeStartMillis >= PURGE_TIME_MS)
        {
            digitalWrite(RELAY_PIN, LOW);

            purgeActive = false;

            // Automatic purge also resets the timer
            previousPurgeMillis = currentMillis;
        }
    }

    //--------------------------------------
    // Status LED
    //--------------------------------------

    unsigned long ledInterval =
        purgeActive ? LED_PURGE_INTERVAL : LED_NORMAL_INTERVAL;

    if (currentMillis - lastLedToggleMillis >= ledInterval)
    {
        lastLedToggleMillis = currentMillis;

        ledState = !ledState;

        digitalWrite(LED_PIN, ledState);
    }
}

void startPurge(unsigned long currentMillis)
{
    // Ignore requests while already purging
    if (purgeActive)
    {
        return;
    }

    purgeActive = true;
    purgeStartMillis = currentMillis;

    digitalWrite(RELAY_PIN, HIGH);
}
        
