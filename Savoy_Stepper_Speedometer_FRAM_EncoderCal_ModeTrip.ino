/*
Stepper Motor Speedometer and OLED Odometer
Robert Cipriani May 2026
-- Modified for SPI OLED 256x64 SH1122
-- Uses FRAM for odometer/trip/ratio storage
-- Uses rotary encoder calibration harness
-- Mode/trip button: ODO -> TRIP -> MPH -> RATIO -> RPM
-- Calibration harness overrides the selected mode and displays MPH + RATIO
-- Adds display viewport constants for factory odometer-window alignment
-- Adds alignment/test display support

Credits:
Luke Hurst for the bulk of this Sketch https://retromini.weebly.com/blog/arduino-speedometer
Kevin Gale - For writing the main stepper motor speedometer sketch - https://github.com/Walterclark1/Stepper_Speedometer
Walterclark for the excellent write-up on how to use the base sketch written by Kevin Gale - http://www.hillclimb.org/forum/viewtopic.php?f=16&t=1133&sid=260e267564c31b08855a0e21ea57cbac
Guy Carpenter - Switec library author - https://github.com/clearwater/SwitecX25
PJRC for the FreqMeasure library - https://www.pjrc.com/teensy/td_libs_FreqMeasure.html
Trewjohn2001 for the Odometer code - http://www.mgexp.com/phorum/read.php?40,2761694
*/

#include <Arduino.h>
#include <SPI.h>
#include <Wire.h>
#include <Adafruit_FRAM_I2C.h>
#include "SwitecX25.h"
#include "FreqMeasure.h"
#include <U8g2lib.h>

// -----------------------------------------------------------------------------
// Build options
// -----------------------------------------------------------------------------
#define DEBUG_SERIAL 1
#define SHOW_ALIGNMENT_ON_STARTUP 0  // set to 1 to show 88888.8 after logo for fitting the OLED

// -----------------------------------------------------------------------------
// OLED Display Settings
// -----------------------------------------------------------------------------
// Real hardware display from original sketch:
//   SH1122 256x64 4-wire SPI OLED
//   Hardware SPI on Nano: MOSI D11, SCK D13
//   CS D10, DC D9, RESET D8
//   Enable U8G2_16BIT in u8g2.h if required by this display.
U8G2_SH1122_256X64_2_4W_HW_SPI display(U8G2_R0, /* cs=*/ 10, /* dc=*/ 9, /* reset=*/ 8);

// Factory odometer-window viewport/alignment constants.
// Adjust these after the OLED is physically positioned behind the gauge face.
const int VIEW_X = 5;
const int VIEW_Y = 38;
const int VIEW_W = 130;
const int VIEW_H = 24;

// -----------------------------------------------------------------------------
// FRAM
// -----------------------------------------------------------------------------
Adafruit_FRAM_I2C fram = Adafruit_FRAM_I2C();
const unsigned long framAddr = 0x50;

// FRAM address map
const uint16_t ADDR_TRIP_TENTHS = 0x00;
const uint16_t ADDR_TRIP_ONES = 0x01;
const uint16_t ADDR_TRIP_TENS = 0x02;
const uint16_t ADDR_TRIP_HUNDREDS = 0x03;

const uint16_t ADDR_ODO_TENTHS = 0x04;
const uint16_t ADDR_ODO_ONES = 0x05;
const uint16_t ADDR_ODO_TENS = 0x06;
const uint16_t ADDR_ODO_HUNDREDS = 0x07;
const uint16_t ADDR_ODO_THOUSANDS = 0x08;
const uint16_t ADDR_ODO_TEN_THOUSANDS = 0x09;
const uint16_t ADDR_ODO_HUNDRED_THOUSANDS = 0x0A;

const uint16_t ADDR_RATIO = 0x20;

// -----------------------------------------------------------------------------
// Speedometer constants
// -----------------------------------------------------------------------------
const int UpdateInterval = 100;      // 100 milliseconds speedo update rate
const int UpdateInterval2 = 1000;    // 1 second odometer/display update rate
const double StepsPerDegree = 3.0;   // Motor step is 1/3 of a degree of rotation
const unsigned int MaxMotorRotation = 170; // 170 max degrees of movement; compare to actual speedometer
const unsigned int MaxMotorSteps = MaxMotorRotation * StepsPerDegree;
const double PulsesPerMile = 4000.0; // Number of input pulses per mile
const double SecondsPerHour = 3600.0;
const int FeetPerMile = 5280;
const double SpeedoDegreesPerMPH = 170.0 / 100.0; // 100 MPH at 170 degrees

unsigned long PreviousMillis = 0;
unsigned long PreviousMillis2 = 0;
double MinMotorStep = 0;
unsigned long distsubtotalFeetTenths = 0; // tenths of a foot accumulator

double sum = 0;
int count = 0;
double avgPulseLength = 0;
unsigned int motorStep = 0;
int noInputCount = 0;
float mph = 0.0;
float rpm = 0.0; // future placeholder

// -----------------------------------------------------------------------------
// Calibration ratio
// -----------------------------------------------------------------------------
union RatioUnion
{
  float f;
  byte b[4];
};

RatioUnion ratio;

const float DefaultRatio = 1.000;
const float MinRatio = 0.500;
const float MaxRatio = 2.000;
const float RatioStep = 0.001;
const unsigned long EncoderSaveIntervalMs = 1000;
unsigned long lastRatioChangeMillis = 0;
bool ratioDirty = false;

// -----------------------------------------------------------------------------
// Pins
// -----------------------------------------------------------------------------
// FreqMeasure on ATmega328P/Nano uses digital pin 8 for input capture on many boards,
// but the original sketch used pin 2 with FreqMeasure.begin(). Confirm your actual
// FreqMeasure board/pin mapping before wiring final hardware.
const uint8_t speedPulsePin = 2;

// Stepper motor pins
SwitecX25 Motor(MaxMotorSteps, 4, 5, 6, 7);

// Calibration harness detect: A0/D14 to GND when calibration harness is plugged in.
const uint8_t calSwitchPin = A0;

// Rotary encoder calibration harness:
//   CLK/A -> D3
//   DT/B  -> D12
//   SW    -> A1
//   +     -> 5V
//   GND   -> GND
const uint8_t encoderAPin = 3;
const uint8_t encoderBPin = 12;
const uint8_t encoderButtonPin = A1;

// Mode/trip button:
//   A2 -> pushbutton -> GND
const uint8_t modeButtonPin = A2;

int lastEncoderA = HIGH;

// Button timing
const unsigned long DebounceMs = 35;
const unsigned long LongPressMs = 1000;

bool encButtonLastReading = HIGH;
bool encButtonStableState = HIGH;
unsigned long encButtonLastChange = 0;
unsigned long encButtonPressedAt = 0;
bool encLongPressHandled = false;

bool modeButtonLastReading = HIGH;
bool modeButtonStableState = HIGH;
unsigned long modeButtonLastChange = 0;
unsigned long modeButtonPressedAt = 0;
bool modeLongPressHandled = false;

// -----------------------------------------------------------------------------
// Display modes
// -----------------------------------------------------------------------------
const uint8_t MODE_ODO = 0;
const uint8_t MODE_TRIP = 1;
const uint8_t MODE_MPH = 2;
const uint8_t MODE_RATIO = 3;
const uint8_t MODE_RPM = 4;
const uint8_t MODE_COUNT = 5;
uint8_t displayMode = MODE_ODO;

// -----------------------------------------------------------------------------
// Odometer/trip digit storage
// -----------------------------------------------------------------------------
uint8_t tripdisttenthsm = 0;
uint8_t tripdistm = 0;
uint8_t tripdistenm = 0;
uint8_t tripdisthundredm = 0;

uint8_t disttenthsm = 0;
uint8_t distm = 0;
uint8_t distenm = 0;
uint8_t disthundredm = 0;
uint8_t disthousandm = 0;
uint8_t disttenthousandm = 0;
uint8_t disthundredthousandm = 0;

// Function declarations
unsigned int PulseToStep(double pulseLength);
void loadStoredValues();
void saveOdometerToFram();
void saveTripToFram();
void resetTrip();
void loadRatioFromFram();
void saveRatioToFram();
float clampRatio(float value);
void handleEncoderCalibration();
void handleEncoderButton();
void handleModeButton();
void updateSpeedometer();
void updateDistance();
void updateodometer();
void incrementOdometerTenth();
void incrementTripTenth();
float getOdometerMiles();
float getTripMiles();
float motorStepToMph(unsigned int stepValue);
void displayStartupLogo();
void displayAlignmentPattern();
void updateDisplay();
void drawFactoryValue(const char* label, float value, uint8_t decimals);
void drawCalibrationDisplay();
const __FlashStringHelper* displayModeName(uint8_t mode);
void printStatusToSerial();

void setup(void)
{
#if DEBUG_SERIAL
  Serial.begin(9600);
  delay(50);
#endif

  display.begin();

  if (fram.begin(framAddr)) {
#if DEBUG_SERIAL
    Serial.println(F("Found I2C FRAM"));
#endif
  } else {
#if DEBUG_SERIAL
    Serial.println(F("I2C FRAM not identified ... check your connections?"));
#endif
    while (1) delay(10);
  }

  pinMode(speedPulsePin, INPUT_PULLUP); // Digital pulse input; confirm FreqMeasure pin mapping for your board.
  pinMode(calSwitchPin, INPUT_PULLUP);
  pinMode(encoderAPin, INPUT_PULLUP);
  pinMode(encoderBPin, INPUT_PULLUP);
  pinMode(encoderButtonPin, INPUT_PULLUP);
  pinMode(modeButtonPin, INPUT_PULLUP);

  lastEncoderA = digitalRead(encoderAPin);

  loadStoredValues();
  loadRatioFromFram();

#if DEBUG_SERIAL
  Serial.print(F("Ratio read from FRAM: "));
  Serial.println(ratio.f, 3);
#endif

  displayStartupLogo();
#if SHOW_ALIGNMENT_ON_STARTUP
  displayAlignmentPattern();
  delay(3000);
#endif

  // Initialize stepper at 0 location, then sweep to max and back.
  Motor.zero();
  Motor.setPosition(MaxMotorSteps);
  Motor.updateBlocking();
  delay(1000);
  Motor.setPosition(0);
  Motor.updateBlocking();
  delay(1000);

  // Force to zero when two intervals have passed with no input.
  MinMotorStep = PulseToStep(2 * (UpdateInterval / 1000.0) * F_CPU);

  FreqMeasure.begin();

  PreviousMillis = millis();
  PreviousMillis2 = millis();
}

void loop()
{
  unsigned long currentMillis = millis();

  handleEncoderCalibration();
  handleEncoderButton();
  handleModeButton();

  // Update speedometer math every UpdateInterval milliseconds.
  if (currentMillis - PreviousMillis >= UpdateInterval) {
    PreviousMillis = currentMillis;
    updateSpeedometer();
  }

  // Always update the motor. It does not instantly go to the desired step.
  Motor.update();

  // Update odometer and display every second.
  if (currentMillis - PreviousMillis2 >= UpdateInterval2) {
    PreviousMillis2 = currentMillis;

    updateDistance();
    saveOdometerToFram();
    saveTripToFram();

    if (ratioDirty && (currentMillis - lastRatioChangeMillis >= EncoderSaveIntervalMs)) {
      saveRatioToFram();
      ratioDirty = false;
    }

    updateDisplay();
#if DEBUG_SERIAL
    printStatusToSerial();
#endif
  }
}

void updateSpeedometer()
{
  count = 0;
  sum = 0;

  // Read all pulses available so we can average them. This helps stabilize
  // the speedo at higher speeds where pulse length is shorter.
  while (FreqMeasure.available()) {
    sum += FreqMeasure.read();
    count++;
  }

  if (count) {
    avgPulseLength = sum / count;
    motorStep = PulseToStep(avgPulseLength) * ratio.f;
    noInputCount = 0;
  } else if (++noInputCount == 2) {
    motorStep = 0;
  }

  // Ignore speeds below the two-missed-intervals threshold so the motor does not jump.
  if (motorStep <= MinMotorStep) {
    motorStep = 0;
  }

  if (motorStep > MaxMotorSteps) {
    motorStep = MaxMotorSteps;
  }

  mph = motorStepToMph(motorStep);
  Motor.setPosition(motorStep);
}

// The FreqMeasure gives us the pulse length in CPU cycles. This formula converts
// the pulse length into pulses per second, then MPH, then degrees, then motor steps.
unsigned int PulseToStep(double pulseLength)
{
  return (unsigned int)((F_CPU * SecondsPerHour * SpeedoDegreesPerMPH * StepsPerDegree) / (PulsesPerMile * pulseLength));
}

float motorStepToMph(unsigned int stepValue)
{
  return ((stepValue / StepsPerDegree) / SpeedoDegreesPerMPH);
}

void updateDistance()
{
  // Convert mph into tenths of feet travelled during one second.
  // feet/sec = mph * 5280 / 3600
  // tenths feet/sec = feet/sec * 10
  unsigned long feetTenthsThisSecond = (unsigned long)((mph * FeetPerMile * 10.0) / SecondsPerHour);
  distsubtotalFeetTenths += feetTenthsThisSecond;
  updateodometer();
}

void loadStoredValues()
{
  tripdisttenthsm = fram.read(ADDR_TRIP_TENTHS);
  tripdistm = fram.read(ADDR_TRIP_ONES);
  tripdistenm = fram.read(ADDR_TRIP_TENS);
  tripdisthundredm = fram.read(ADDR_TRIP_HUNDREDS);

  disttenthsm = fram.read(ADDR_ODO_TENTHS);
  distm = fram.read(ADDR_ODO_ONES);
  distenm = fram.read(ADDR_ODO_TENS);
  disthundredm = fram.read(ADDR_ODO_HUNDREDS);
  disthousandm = fram.read(ADDR_ODO_THOUSANDS);
  disttenthousandm = fram.read(ADDR_ODO_TEN_THOUSANDS);
  disthundredthousandm = fram.read(ADDR_ODO_HUNDRED_THOUSANDS);

  // Sanity-check BCD-style digit storage.
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

void saveOdometerToFram()
{
  fram.write(ADDR_ODO_TENTHS, disttenthsm);
  fram.write(ADDR_ODO_ONES, distm);
  fram.write(ADDR_ODO_TENS, distenm);
  fram.write(ADDR_ODO_HUNDREDS, disthundredm);
  fram.write(ADDR_ODO_THOUSANDS, disthousandm);
  fram.write(ADDR_ODO_TEN_THOUSANDS, disttenthousandm);
  fram.write(ADDR_ODO_HUNDRED_THOUSANDS, disthundredthousandm);
}

void saveTripToFram()
{
  fram.write(ADDR_TRIP_TENTHS, tripdisttenthsm);
  fram.write(ADDR_TRIP_ONES, tripdistm);
  fram.write(ADDR_TRIP_TENS, tripdistenm);
  fram.write(ADDR_TRIP_HUNDREDS, tripdisthundredm);
}

void resetTrip()
{
  tripdisttenthsm = 0;
  tripdistm = 0;
  tripdistenm = 0;
  tripdisthundredm = 0;
  saveTripToFram();
#if DEBUG_SERIAL
  Serial.println(F("Trip reset to 0.0"));
#endif
  updateDisplay();
}

void loadRatioFromFram()
{
  ratio.b[0] = fram.read(ADDR_RATIO + 0);
  ratio.b[1] = fram.read(ADDR_RATIO + 1);
  ratio.b[2] = fram.read(ADDR_RATIO + 2);
  ratio.b[3] = fram.read(ADDR_RATIO + 3);

  if (isnan(ratio.f) || ratio.f < MinRatio || ratio.f > MaxRatio) {
    ratio.f = DefaultRatio;
    saveRatioToFram();
  }
}

void saveRatioToFram()
{
  ratio.f = clampRatio(ratio.f);
  fram.write(ADDR_RATIO + 0, ratio.b[0]);
  fram.write(ADDR_RATIO + 1, ratio.b[1]);
  fram.write(ADDR_RATIO + 2, ratio.b[2]);
  fram.write(ADDR_RATIO + 3, ratio.b[3]);
#if DEBUG_SERIAL
  Serial.print(F("Ratio saved: "));
  Serial.println(ratio.f, 3);
#endif
}

float clampRatio(float value)
{
  if (value < MinRatio) return MinRatio;
  if (value > MaxRatio) return MaxRatio;
  return value;
}

void handleEncoderCalibration()
{
  // Calibration mode only active when harness grounds A0/D14.
  bool calMode = (digitalRead(calSwitchPin) == LOW);
  if (!calMode) {
    lastEncoderA = digitalRead(encoderAPin);
    return;
  }

  int encoderA = digitalRead(encoderAPin);
  if (encoderA != lastEncoderA) {
    // Read on A rising edge to get one count per detent-ish.
    if (encoderA == HIGH) {
      int encoderB = digitalRead(encoderBPin);

      if (encoderB == LOW) {
        ratio.f += RatioStep;
      } else {
        ratio.f -= RatioStep;
      }

      ratio.f = clampRatio(ratio.f);
      lastRatioChangeMillis = millis();
      ratioDirty = true;

#if DEBUG_SERIAL
      Serial.print(F("Ratio adjusted: "));
      Serial.println(ratio.f, 3);
#endif
      updateDisplay();
    }
    lastEncoderA = encoderA;
  }
}

void handleEncoderButton()
{
  bool reading = digitalRead(encoderButtonPin);
  unsigned long now = millis();

  if (reading != encButtonLastReading) {
    encButtonLastChange = now;
    encButtonLastReading = reading;
  }

  if ((now - encButtonLastChange) > DebounceMs && reading != encButtonStableState) {
    encButtonStableState = reading;

    if (encButtonStableState == LOW) {
      encButtonPressedAt = now;
      encLongPressHandled = false;
    } else {
      if (!encLongPressHandled) {
        // Short press: save ratio.
        saveRatioToFram();
        ratioDirty = false;
      }
    }
  }

  if (encButtonStableState == LOW && !encLongPressHandled && (now - encButtonPressedAt >= LongPressMs)) {
    // Long press: reset ratio to 1.000.
    ratio.f = DefaultRatio;
    saveRatioToFram();
    ratioDirty = false;
    encLongPressHandled = true;
#if DEBUG_SERIAL
    Serial.println(F("Ratio reset to 1.000"));
#endif
    updateDisplay();
  }
}

void handleModeButton()
{
  bool reading = digitalRead(modeButtonPin);
  unsigned long now = millis();

  if (reading != modeButtonLastReading) {
    modeButtonLastChange = now;
    modeButtonLastReading = reading;
  }

  if ((now - modeButtonLastChange) > DebounceMs && reading != modeButtonStableState) {
    modeButtonStableState = reading;

    if (modeButtonStableState == LOW) {
      modeButtonPressedAt = now;
      modeLongPressHandled = false;
    } else {
      if (!modeLongPressHandled) {
        // Short press: cycle display modes.
        displayMode = (displayMode + 1) % MODE_COUNT;
#if DEBUG_SERIAL
        Serial.print(F("Mode: "));
        Serial.println(displayModeName(displayMode));
#endif
        updateDisplay();
      }
    }
  }

  if (modeButtonStableState == LOW && !modeLongPressHandled && (now - modeButtonPressedAt >= LongPressMs)) {
    // Long press in TRIP mode: reset trip.
    if (displayMode == MODE_TRIP) {
      resetTrip();
    }
    modeLongPressHandled = true;
  }
}

//-------------------------Update Odometer Counts--------------------------------------------------------
void updateodometer()
{
  // 0.1 mile = 528 feet = 5280 tenths of a foot.
  while (distsubtotalFeetTenths >= 5280UL) {
    incrementOdometerTenth();
    incrementTripTenth();
    distsubtotalFeetTenths -= 5280UL;
  }
}

void incrementOdometerTenth()
{
  ++disttenthsm;

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

  if (disthundredthousandm > 9) {
    disthundredthousandm = 0;
  }
}

void incrementTripTenth()
{
  ++tripdisttenthsm;

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
    tripdisthundredm = 0;
  }
}

float getOdometerMiles()
{
  unsigned long wholeMiles = 0;
  wholeMiles += (unsigned long)disthundredthousandm * 100000UL;
  wholeMiles += (unsigned long)disttenthousandm * 10000UL;
  wholeMiles += (unsigned long)disthousandm * 1000UL;
  wholeMiles += (unsigned long)disthundredm * 100UL;
  wholeMiles += (unsigned long)distenm * 10UL;
  wholeMiles += (unsigned long)distm;
  return wholeMiles + (disttenthsm / 10.0);
}

float getTripMiles()
{
  unsigned long wholeMiles = 0;
  wholeMiles += (unsigned long)tripdisthundredm * 100UL;
  wholeMiles += (unsigned long)tripdistenm * 10UL;
  wholeMiles += (unsigned long)tripdistm;
  return wholeMiles + (tripdisttenthsm / 10.0);
}

void displayStartupLogo()
{
  display.clearDisplay();
  display.setFont(u8g2_font_secretaryhand_t_all);
  display.setCursor(15, 38);
  display.print(F("Savoy"));
  display.display();
  delay(2000);
  display.clearDisplay();
  display.display();
}

void displayAlignmentPattern()
{
  display.clearDisplay();
  display.setFont(u8g2_font_luBS12_tn);
  display.setCursor(VIEW_X, VIEW_Y);
  display.print(F("88888.8"));
  display.display();
}

void updateDisplay()
{
  bool calMode = (digitalRead(calSwitchPin) == LOW);

  display.clearDisplay();

  if (calMode) {
    drawCalibrationDisplay();
    display.display();
    return;
  }

  switch (displayMode) {
    case MODE_ODO:
      drawFactoryValue("ODO", getOdometerMiles(), 1);
      break;
    case MODE_TRIP:
      drawFactoryValue("TRIP", getTripMiles(), 1);
      break;
    case MODE_MPH:
      drawFactoryValue("MPH", mph, 1);
      break;
    case MODE_RATIO:
      drawFactoryValue("RATIO", ratio.f, 3);
      break;
    case MODE_RPM:
      drawFactoryValue("RPM", rpm, 0);
      break;
    default:
      drawFactoryValue("ODO", getOdometerMiles(), 1);
      break;
  }

  display.display();
}

void drawFactoryValue(const char* label, float value, uint8_t decimals)
{
  // Keep the main value inside the factory odometer-window viewport.
  // The label is small and can be disabled later if the window is too tight.
  display.setFont(u8g2_font_5x7_tr);
  display.setCursor(VIEW_X, max(7, VIEW_Y - 18));
  display.print(label);

  display.setFont(u8g2_font_luBS12_tn);
  display.setCursor(VIEW_X, VIEW_Y);
  display.print(value, decimals);
}

void drawCalibrationDisplay()
{
  // Calibration harness overrides the selected mode.
  // Use compact text so both MPH and ratio fit through the odometer window.
  display.setFont(u8g2_font_5x7_tr);
  display.setCursor(VIEW_X, max(7, VIEW_Y - 26));
  display.print(F("CAL"));

  display.setCursor(VIEW_X, max(15, VIEW_Y - 15));
  display.print(F("MPH "));
  display.print(mph, 1);

  display.setCursor(VIEW_X, VIEW_Y);
  display.print(F("RATIO "));
  display.print(ratio.f, 3);
}

const __FlashStringHelper* displayModeName(uint8_t mode)
{
  switch (mode) {
    case MODE_ODO:
      return F("ODO");
    case MODE_TRIP:
      return F("TRIP");
    case MODE_MPH:
      return F("MPH");
    case MODE_RATIO:
      return F("RATIO");
    case MODE_RPM:
      return F("RPM");
    default:
      return F("?");
  }
}

void printStatusToSerial()
{
  Serial.print(F("MPH="));
  Serial.print(mph, 1);
  Serial.print(F("  Step="));
  Serial.print(motorStep);
  Serial.print(F("  ODO="));
  Serial.print(getOdometerMiles(), 1);
  Serial.print(F("  TRIP="));
  Serial.print(getTripMiles(), 1);
  Serial.print(F("  Ratio="));
  Serial.print(ratio.f, 3);
  Serial.print(F("  Mode="));
  Serial.print(displayModeName(displayMode));
  Serial.print(F("  Cal="));
  Serial.println(digitalRead(calSwitchPin) == LOW ? F("YES") : F("NO"));
}
