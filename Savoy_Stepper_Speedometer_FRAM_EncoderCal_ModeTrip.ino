/*
Stepper Motor Speedometer and OLED Odometer
Robert Cipriani May 2024
-- Modified for SPI OLED 256x64
-- Added logic for calibration harness with rotary encoder and pushbutton
-- Added display mode / trip reset pushbutton
-- Moved from EEPROM to FRAM and removed transmission "neutral" signal

Credits:
Luke Hurst for the bulk of this Sketch https://retromini.weebly.com/blog/arduino-speedometer
Kevin Gale - For writing the main stepper motor speedometer sketch - https://github.com/Walterclark1/Stepper_Speedometer
Walterclark for the excellent write-up on how to use the base sketch written by Kevin Gale - http://www.hillclimb.org/forum/viewtopic.php?f=16&t=1133&sid=260e267564c31b08855a0e21ea57cbac
Guy Carpenter - Switec library author - https://github.com/clearwater/SwitecX25
PJRC for the FreqMeasure library - https://www.pjrc.com/teensy/td_libs_FreqMeasure.html
Trewjohn2001 for the Odometer code - http://www.mgexp.com/phorum/read.php?40,2761694
*/

#include <Adafruit_FRAM_I2C.h>
#include "SwitecX25.h"
#include "FreqMeasure.h"
#include <SPI.h>
#include <Arduino.h>
#include <U8g2lib.h>
#include <math.h>

//----Define OLED Display Settings------
//U8G2_SH1122_256X64_2_4W_SW_SPI display(U8G2_R0, /* clock=*/ 13, /* data=*/ 11, /* cs=*/ 10, /* dc=*/ 9, /* reset=*/ 8);        // Enable U8G2_16BIT in u8g2.h ?
// 2 pages in memory, hardware SPI, no rotations
U8G2_SH1122_256X64_2_4W_HW_SPI display(U8G2_R0,  /* cs=*/ 10, /* dc=*/ 9, /* reset=*/ 8);        // Enable U8G2_16BIT in u8g2.h ?
//hw: data 11, clock 13
//-----End OLED Display Settings--------

Adafruit_FRAM_I2C fram = Adafruit_FRAM_I2C(); // instantiate and initialize FRAM

/* Connect SCL    to analog 5
   Connect SDA    to analog 4
   Connect VDD    to 5.0V DC
   Connect GROUND to common ground */

const int UpdateInterval = 100;   // 100 milliseconds speedo update rate
const int UpdateInterval2 = 1000; // 1000 milliseconds odometer/display update rate
const double StepsPerDegree = 3.0;  // Motor step is 1/3 of a degree of rotation
const unsigned int MaxMotorRotation = 170; // 170 max degrees of movement **need to compare to actual speedometer**
const unsigned int MaxMotorSteps = MaxMotorRotation * StepsPerDegree;
const double PulsesPerMile = 4000.0; // Number of input pulses per mile
const double SecondsPerHour = 3600.0;
const int FeetPerMile = 5280; // Feet per mile conversion factor for Odometer update function
const double SpeedoDegreesPerMPH = 170.0 / 100.0; // Speed on face of dial at 180 degrees is 110 (?) mph.

unsigned long PreviousMillis = 0;   // last time we updated the speedo
unsigned long PreviousMillis2 = 0;
double MinMotorStep;  // lowest step that will be used - calculated from update interval
unsigned long distsubtotal = 0;     // Running total of feet covered for main odometer update function
unsigned long tripsubtotal = 0;     // Running total of feet covered for trip odometer update function
unsigned long FeetTravelled = 0;    // mph converted into feet travelled for Odometer function
double sum = 0;
int count = 0;
double avgPulseLength = 0;
unsigned int motorStep = 0;
int noInputCount = 0;
float mph = 0;

// variables for calibration ratio storage
union RatioUnion
{
  float f;
  byte b[4];
};

RatioUnion ratio;

//----Calibration harness / encoder pin outs-------------------------------------------
// Harness detect is active LOW. When the calibration harness is plugged in, connect CAL_DETECT_PIN to GND.
const int CAL_DETECT_PIN = A0;     // A0 is also digital pin 14 on an Arduino Uno/Nano style board
const int ENCODER_A_PIN = 3;       // Encoder channel A. Avoid D2 because FreqMeasure uses the pulse input.
const int ENCODER_B_PIN = 12;      // Encoder channel B. If D12 conflicts with your SPI/OLED wiring, move this pin.
const int ENCODER_BUTTON_PIN = A1; // Encoder pushbutton, active LOW. Short press saves; long press resets ratio to 1.000.

//----Display mode / trip reset button-------------------------------------------------
// Active LOW. Short press cycles display modes. Long press resets trip odometer only when the trip screen is active.
const int MODE_BUTTON_PIN = A2;

enum DisplayMode
{
  MODE_ODOMETER = 0,
  MODE_TRIP = 1,
  MODE_RATIO = 2,
  MODE_RPM = 3,        // Placeholder for future tach/RPM input.
  MODE_COUNT = 4
};

DisplayMode displayMode = MODE_ODOMETER;

const float DEFAULT_RATIO = 1.000;
const float MIN_RATIO = 0.500;
const float MAX_RATIO = 2.000;
const float RATIO_STEP = 0.001; // One encoder detent changes ratio by 0.1% if your encoder outputs one detent per counted step.

bool calibrationMode = false;
int lastEncoderA = HIGH;
unsigned long lastEncoderMoveMillis = 0;
unsigned long encoderButtonLastChangeMillis = 0;
unsigned long encoderButtonPressedMillis = 0;
bool encoderButtonLastReading = HIGH;
bool encoderButtonState = HIGH;
bool ratioDirty = false;

unsigned long modeButtonLastChangeMillis = 0;
unsigned long modeButtonPressedMillis = 0;
bool modeButtonLastReading = HIGH;
bool modeButtonState = HIGH;
bool modeButtonLongPressHandled = false;

const unsigned long ENCODER_DEBOUNCE_MS = 2;
const unsigned long BUTTON_DEBOUNCE_MS = 30;
const unsigned long LONG_PRESS_MS = 1500;
const unsigned long RATIO_AUTOSAVE_MS = 3000;
unsigned long lastRatioAutosaveMillis = 0;

// fram address
const unsigned long framAddr = 0x50;

// FRAM memory map
const uint16_t TRIP_TENTHS_ADDR = 0x00;
const uint16_t TRIP_ONES_ADDR = 0x01;
const uint16_t TRIP_TENS_ADDR = 0x02;
const uint16_t TRIP_HUNDREDS_ADDR = 0x03;

const uint16_t ODO_TENTHS_ADDR = 0x04;
const uint16_t ODO_ONES_ADDR = 0x05;
const uint16_t ODO_TENS_ADDR = 0x06;
const uint16_t ODO_HUNDREDS_ADDR = 0x07;
const uint16_t ODO_THOUSANDS_ADDR = 0x08;
const uint16_t ODO_TEN_THOUSANDS_ADDR = 0x09;
const uint16_t ODO_HUNDRED_THOUSANDS_ADDR = 0x0A;

// Leave space after the odometer digits. Store ratio away from odometer data to avoid overlap.
const uint16_t RATIO_ADDR = 0x20;

//----Define Stepper motor library variables and pin outs-------------------------------------------
SwitecX25 Motor(MaxMotorSteps, 4, 5, 6, 7); // Create the motor object with the maximum steps allowed

//-----------------trip----------------------
uint8_t tripdisttenthsm = 0;
uint8_t tripdistm = 0;
uint8_t tripdistenm = 0;
uint8_t tripdisthundredm = 0;
//----------------END-trip-----------------------

//------Stored Odometer Values From FRAM -----
uint8_t disttenthsm = 0;
uint8_t distm = 0;
uint8_t distenm = 0;
uint8_t disthundredm = 0;
uint8_t disthousandm = 0;
uint8_t disttenthousandm = 0;
uint8_t disthundredthousandm = 0;
//------End FRAM Read-----------------------------

void setup(void)
{
  Serial.begin(9600);
  display.begin(); //Init OLED

  if (fram.begin(framAddr)) {
    Serial.println("Found I2C FRAM");
  } else {
    Serial.println("I2C FRAM not identified ... check your connections?\r\n");
    while (1) delay(10);
  }

  readOdometerFromFRAM();
  readRatioFromFRAM();

  Serial.print("Ratio read from FRAM: ");
  Serial.println(ratio.f, 6);

  pinMode(2, INPUT_PULLUP); // Define digital pin 2 as pulse signal input. Does this need PULLUP if using VA module (internal pullup)?

  pinMode(CAL_DETECT_PIN, INPUT_PULLUP);
  pinMode(ENCODER_A_PIN, INPUT_PULLUP);
  pinMode(ENCODER_B_PIN, INPUT_PULLUP);
  pinMode(ENCODER_BUTTON_PIN, INPUT_PULLUP);
  pinMode(MODE_BUTTON_PIN, INPUT_PULLUP);
  lastEncoderA = digitalRead(ENCODER_A_PIN);

  //-----Display logo on start-up
  display.clearDisplay();
  display.setFont(u8g2_font_secretaryhand_t_all);
  display.setCursor(15, 38);
  display.print("Savoy");
  display.display();
  delay(2000);
  display.clearDisplay();
  display.display();
  display.setFont(u8g2_font_luBS12_tn);
  //-----End Logo-----------------------

  Motor.zero(); //Initialize stepper at 0 location
  Motor.setPosition(510); //3*170 degrees, at 100mph mark
  Motor.updateBlocking();
  delay(1000);
  Motor.setPosition(0);  //0MPH
  Motor.updateBlocking();
  delay(1000);

  MinMotorStep = PulseToStep(2 * (UpdateInterval / 1000.0) * F_CPU); //Force to zero when two intervals have passed with input

  FreqMeasure.begin(); // Start freqmeasure library
}

void loop() {

  unsigned long currentMillis = millis();

  calibrationMode = (digitalRead(CAL_DETECT_PIN) == LOW);

  handleModeButton();

  if (calibrationMode) {
    handleEncoderCalibration();
  }

  // Update the motor position every UpdateInterval milliseconds
  if (currentMillis - PreviousMillis >= UpdateInterval) {
    PreviousMillis = currentMillis;
    count = 0;
    sum = 0;

    // Read all the pulses available so we can average them
    while (FreqMeasure.available()) {
      sum += FreqMeasure.read();
      count++;
    }

    if (count) {
      // Average all the readings we got over our fixed time interval. This helps
      // stabilize the speedo at higher speeds. The pulse length gets shorter and
      // thus harder to measure accurately but we get more pulses to average.
      // It may be necessary to update the FreqMeasure library to change the buffer
      // length to hold the full number of pulses per update interval at the highest
      // speedo values.
      avgPulseLength = sum / count;

      motorStep = (unsigned int)(PulseToStep(avgPulseLength) * ratio.f);
      if (motorStep > MaxMotorSteps) motorStep = MaxMotorSteps;
      noInputCount = 0;
    }
    else if (++noInputCount == 2) { // force speed to zero after two missed intervals
      motorStep = 0;
    }

    // Ignore speeds below the two missed intervals speed so the motor doesn't jump
    if (motorStep <= MinMotorStep)
      motorStep = 0;

    Motor.setPosition(motorStep);
  }

  // Always update the motor. It doesn't instantly go to the desired step so even if
  // we didn't call setPosition the motor may still be moving to position from the last
  // setPosition call.
  Motor.update();

  //-----------------Update Odometer Counter and Display every second -----------------------------------
  unsigned long currentMillis2 = millis();

  if (currentMillis2 - PreviousMillis2 >= UpdateInterval2) {
    PreviousMillis2 = currentMillis2;

    mph = ((motorStep / StepsPerDegree) / SpeedoDegreesPerMPH); // Might be a better way of calculating mph based on input frequency
    Serial.print("MPH: ");
    Serial.println(mph); // Used for testing to output speed in serial monitor.

    FeetTravelled = (unsigned long)((mph * FeetPerMile) / SecondsPerHour); // Convert mph into feet per second for running subtotal count.
    distsubtotal += FeetTravelled;  // add feet traveled to running subtotal
    tripsubtotal += FeetTravelled;  // add feet traveled to trip running subtotal

    updateodometer();      // Adds distance travelled to odometer
    updatetripodometer();  // Adds distance travelled to trip odometer

    updatefram(); // Save odometer values after the once-per-second odometer update.

    autosaveRatioIfNeeded();
    displayCurrentMode();
  }
  //---------------------------END ODOMETER-------------------------------------------------------------
}

// The FreqMeasure gives us the pulse length in CPU cycles.  This formula converts this into a motor step.
// Basically we are converting the length of the pulse in CPU cycles into pulses per second and then
// converting that into MPH. Once we have MPH that number is converted into degrees and that is then
// converted into a number of steps.
unsigned int PulseToStep(double pulseLength)
{
  return (unsigned int)((F_CPU * SecondsPerHour * SpeedoDegreesPerMPH * StepsPerDegree) / (PulsesPerMile * pulseLength));
}

//-------Start of Functions-------------------------------------------------------------------------------------------------

void handleEncoderCalibration() {
  unsigned long now = millis();

  int encoderA = digitalRead(ENCODER_A_PIN);

  // Count on channel A falling edge. Direction comes from channel B.
  if (encoderA != lastEncoderA && (now - lastEncoderMoveMillis) >= ENCODER_DEBOUNCE_MS) {
    lastEncoderMoveMillis = now;

    if (encoderA == LOW) {
      int encoderB = digitalRead(ENCODER_B_PIN);

      if (encoderB == HIGH) {
        adjustRatio(RATIO_STEP);
      } else {
        adjustRatio(-RATIO_STEP);
      }
    }

    lastEncoderA = encoderA;
  }

  handleEncoderButton();
}

void handleEncoderButton() {
  unsigned long now = millis();
  bool reading = digitalRead(ENCODER_BUTTON_PIN);

  if (reading != encoderButtonLastReading) {
    encoderButtonLastChangeMillis = now;
  }

  if ((now - encoderButtonLastChangeMillis) > BUTTON_DEBOUNCE_MS) {
    if (reading != encoderButtonState) {
      encoderButtonState = reading;

      if (encoderButtonState == LOW) {
        encoderButtonPressedMillis = now;
      } else {
        unsigned long pressTime = now - encoderButtonPressedMillis;

        if (pressTime >= LONG_PRESS_MS) {
          ratio.f = DEFAULT_RATIO;
          ratioDirty = true;
          saveRatioToFRAM();
          Serial.println("Ratio reset to 1.000");
        } else {
          saveRatioToFRAM();
          Serial.println("Ratio saved");
        }
      }
    }
  }

  encoderButtonLastReading = reading;
}

void handleModeButton() {
  unsigned long now = millis();
  bool reading = digitalRead(MODE_BUTTON_PIN);

  if (reading != modeButtonLastReading) {
    modeButtonLastChangeMillis = now;
  }

  if ((now - modeButtonLastChangeMillis) > BUTTON_DEBOUNCE_MS) {
    if (reading != modeButtonState) {
      modeButtonState = reading;

      if (modeButtonState == LOW) {
        modeButtonPressedMillis = now;
        modeButtonLongPressHandled = false;
      } else {
        unsigned long pressTime = now - modeButtonPressedMillis;

        // Short press cycles modes. Long press in trip mode is handled while held.
        if (pressTime < LONG_PRESS_MS && !modeButtonLongPressHandled) {
          cycleDisplayMode();
        }
      }
    }

    // Long press resets the trip odometer only when the trip screen is active.
    if (modeButtonState == LOW && !modeButtonLongPressHandled && displayMode == MODE_TRIP && (now - modeButtonPressedMillis >= LONG_PRESS_MS)) {
      resetTripOdometer();
      modeButtonLongPressHandled = true;
      displayTripResetMessage();
      Serial.println("Trip odometer reset");
    }
  }

  modeButtonLastReading = reading;
}

void cycleDisplayMode() {
  displayMode = (DisplayMode)((displayMode + 1) % MODE_COUNT);

  Serial.print("Display mode: ");
  Serial.println((int)displayMode);

  displayCurrentMode();
}

void adjustRatio(float adjustment) {
  ratio.f += adjustment;

  if (ratio.f < MIN_RATIO) ratio.f = MIN_RATIO;
  if (ratio.f > MAX_RATIO) ratio.f = MAX_RATIO;

  ratioDirty = true;

  Serial.print("Ratio adjusted: ");
  Serial.println(ratio.f, 6);
}

void autosaveRatioIfNeeded() {
  unsigned long now = millis();

  if (ratioDirty && calibrationMode && (now - lastRatioAutosaveMillis >= RATIO_AUTOSAVE_MS)) {
    saveRatioToFRAM();
    lastRatioAutosaveMillis = now;
  }
}

void readRatioFromFRAM() {
  ratio.b[0] = fram.read(RATIO_ADDR + 0);
  ratio.b[1] = fram.read(RATIO_ADDR + 1);
  ratio.b[2] = fram.read(RATIO_ADDR + 2);
  ratio.b[3] = fram.read(RATIO_ADDR + 3);

  // If FRAM is blank/corrupt or ratio is outside a sane range, start at 1:1.
  if (isnan(ratio.f) || ratio.f < MIN_RATIO || ratio.f > MAX_RATIO) {
    ratio.f = DEFAULT_RATIO;
    saveRatioToFRAM();
  }
}

void saveRatioToFRAM() {
  fram.write(RATIO_ADDR + 0, ratio.b[0]);
  fram.write(RATIO_ADDR + 1, ratio.b[1]);
  fram.write(RATIO_ADDR + 2, ratio.b[2]);
  fram.write(RATIO_ADDR + 3, ratio.b[3]);
  ratioDirty = false;
}

void readOdometerFromFRAM() {
  tripdisttenthsm = fram.read(TRIP_TENTHS_ADDR);
  tripdistm = fram.read(TRIP_ONES_ADDR);
  tripdistenm = fram.read(TRIP_TENS_ADDR);
  tripdisthundredm = fram.read(TRIP_HUNDREDS_ADDR);

  disttenthsm = fram.read(ODO_TENTHS_ADDR);
  distm = fram.read(ODO_ONES_ADDR);
  distenm = fram.read(ODO_TENS_ADDR);
  disthundredm = fram.read(ODO_HUNDREDS_ADDR);
  disthousandm = fram.read(ODO_THOUSANDS_ADDR);
  disttenthousandm = fram.read(ODO_TEN_THOUSANDS_ADDR);
  disthundredthousandm = fram.read(ODO_HUNDRED_THOUSANDS_ADDR);

  sanitizeOdometerDigits();
}

void sanitizeOdometerDigits() {
  if (tripdisttenthsm > 9) tripdisttenthsm = 0;
  if (tripdistm > 9) tripdistm = 0;
  if (tripdistenm > 9) tripdistenm = 0;
  if (tripdisthundredm > 9) tripdisthundredm = 0;

  if (disttenthsm > 9) disttenthsm = 0;
  if (distm > 9) distm = 0;
  if (distenm > 9) distenm = 0;
  if (disthundredm > 9) disthundredm = 0;
  if (disthousandm > 9) disthousandm = 0;
  if (disttenthousandm > 9) disttenthousandm = 0;
  if (disthundredthousandm > 9) disthundredthousandm = 0;
}

void displayCurrentMode() {
  switch (displayMode) {
    case MODE_ODOMETER:
      displayodometer();
      break;

    case MODE_TRIP:
      displaytripodometer();
      break;

    case MODE_RATIO:
      displayRatio();
      break;

    case MODE_RPM:
      displayRpmPlaceholder();
      break;
  }
}

void displayRatio() {
  display.clearDisplay();
  display.setFont(u8g2_font_luBS12_tn);

  display.setCursor(5, 18);
  if (calibrationMode) {
    display.print("CAL RATIO");
  } else {
    display.print("RATIO");
  }

  display.setCursor(5, 42);
  display.print(ratio.f, 3);

  display.setCursor(5, 62);
  display.print("MPH ");
  display.print(mph, 1);

  display.display();
}

void displayRpmPlaceholder() {
  display.clearDisplay();
  display.setFont(u8g2_font_luBS12_tn);

  display.setCursor(5, 20);
  display.print("RPM");

  display.setCursor(5, 46);
  display.print("Future");

  display.display();
}

void displayTripResetMessage() {
  display.clearDisplay();
  display.setFont(u8g2_font_luBS12_tn);

  display.setCursor(5, 24);
  display.print("TRIP");

  display.setCursor(5, 50);
  display.print("RESET");

  display.display();
}

void displayodometer() {

  //display.setTextSize(3)
  //display.setTextColor(WHITE);
  display.clearDisplay();
  display.setFont(u8g2_font_luBS12_tn);

  display.setCursor(5, 16);
  display.print("ODO");

  //----------------Display hundred thousand m units------------------------------------
  display.setCursor(5, 46);
  display.println(disthundredthousandm);

  //----------------Display ten thousand m units------------------------------------
  display.setCursor(25, 46);
  display.println(disttenthousandm);

  //----------------Display thousand m units------------------------------------
  display.setCursor(45, 46);
  display.println(disthousandm);

  //----------------Display hundreds m units------------------------------------
  display.setCursor(65, 46);
  display.println(disthundredm);

  //----------------Display tens m units----------------------------------------
  display.setCursor(85, 46);
  display.println(distenm);

  //----------------Display miles units---------------------------------------------
  display.setCursor(105, 46);
  display.println(distm);

  //----------------Display tenths m units---------------------------------------------
  display.setCursor(130, 46);

  //set reverse video mode
  display.sendF("c", 0x0a1);

  display.println(disttenthsm);
  display.display();

  //set normal mode
  display.sendF("c", 0x0a6);
}

void displaytripodometer() {

  display.clearDisplay();
  display.setFont(u8g2_font_luBS12_tn);

  display.setCursor(5, 16);
  display.print("TRIP");

  //----------------Display hundreds m units------------------------------------
  display.setCursor(5, 46);
  display.println(tripdisthundredm);

  //----------------Display tens m units----------------------------------------
  display.setCursor(25, 46);
  display.println(tripdistenm);

  //----------------Display miles units---------------------------------------------
  display.setCursor(45, 46);
  display.println(tripdistm);

  //----------------Display tenths m units---------------------------------------------
  display.setCursor(70, 46);

  //set reverse video mode
  display.sendF("c", 0x0a1);

  display.println(tripdisttenthsm);
  display.display();

  //set normal mode
  display.sendF("c", 0x0a6);
}

//-------------------------Update Odometer Counts--------------------------------------------------------

void updateodometer() {

  while (distsubtotal >= 528) {
    ++disttenthsm;
    distsubtotal -= 528;
  }

  if (disttenthsm > 9) {
    ++distm;
    disttenthsm = 0;
  }

  if (distm > 9) {
    ++distenm;
    distm = 0;
  }

  if (distenm > 9) {
    ++disthundredm;
    distenm = 0;
  }

  if (disthundredm > 9) {
    ++disthousandm;
    disthundredm = 0;
  }

  if (disthousandm > 9) {
    ++disttenthousandm;
    disthousandm = 0;
  }

  if (disttenthousandm > 9) {
    ++disthundredthousandm;
    disttenthousandm = 0;
  }
}

void updatetripodometer() {

  while (tripsubtotal >= 528) {
    ++tripdisttenthsm;
    tripsubtotal -= 528;
  }

  if (tripdisttenthsm > 9) {
    ++tripdistm;
    tripdisttenthsm = 0;
  }

  if (tripdistm > 9) {
    ++tripdistenm;
    tripdistm = 0;
  }

  if (tripdistenm > 9) {
    ++tripdisthundredm;
    tripdistenm = 0;
  }

  if (tripdisthundredm > 9) {
    // Trip display is limited to 999.9 miles. Roll over after that.
    tripdisthundredm = 0;
  }
}

void resetTripOdometer() {
  tripdisttenthsm = 0;
  tripdistm = 0;
  tripdistenm = 0;
  tripdisthundredm = 0;
  tripsubtotal = 0;
  updatefram();
}

void updatefram() {

  fram.write(TRIP_TENTHS_ADDR, tripdisttenthsm);
  fram.write(TRIP_ONES_ADDR, tripdistm);
  fram.write(TRIP_TENS_ADDR, tripdistenm);
  fram.write(TRIP_HUNDREDS_ADDR, tripdisthundredm);
  fram.write(ODO_TENTHS_ADDR, disttenthsm);
  fram.write(ODO_ONES_ADDR, distm);
  fram.write(ODO_TENS_ADDR, distenm);
  fram.write(ODO_HUNDREDS_ADDR, disthundredm);
  fram.write(ODO_THOUSANDS_ADDR, disthousandm);
  fram.write(ODO_TEN_THOUSANDS_ADDR, disttenthousandm);
  fram.write(ODO_HUNDRED_THOUSANDS_ADDR, disthundredthousandm);
}
