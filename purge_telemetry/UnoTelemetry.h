#pragma once
#include <SoftwareSerial.h>
#include "ControllerClock.h"

// D2 RX <- adapter TX; D3 TX -> adapter RX. D7/D8/D13 untouched.
SoftwareSerial espLink(2, 3);
const char UNO_FIRMWARE[] = "1.3.0";
Uptime controllerUptime;
LineReader<40> commandLine;
uint32_t completedPurges = 0, lastPurgeDuration = 0;
bool completionPending = false, statusRequested = true;
char telemetryTx[240];
bool clockReportPending = false, timeRequestSent = false;
uint32_t lastTimeRequest = 0, lastLineEnd = 0;
uint32_t lastPurgeEpoch = 0;
bool lastPurgeTimeValid = false;
uint8_t telemetryPos = 0;
uint32_t lastSnapshot = 0, lastTxByte = 0;

// Future hooks: valid only after actual sensor measurements/assessment.
struct ControllerMeasurements {
  bool pressureValid = false, rpmValid = false, motorValid = false;
  bool successValid = false, success = false, motorRunning = false;
  long pressureMilliBar = 0;
  unsigned long rpm = 0;
} measurements;

void recordPurgeComplete(uint32_t duration) {
  lastPurgeTimeValid = isClockValid();
  lastPurgeEpoch = lastPurgeTimeValid ? getCurrentEpoch() : 0;
  ++completedPurges;
  lastPurgeDuration = duration;
  // TODO: Uno pressure-response assessment before setting successValid.
  measurements.successValid = false;
  completionPending = true; // latest event only; snapshot recovers count/duration
  statusRequested = true;
}

void serviceTelemetry(uint32_t now, bool startup, bool active,
                      uint32_t deadlineStart, uint32_t deadlineDuration) {
  controllerUptime.tick(now);
  processClock.tick(now);
  // Stop RX interrupts and telemetry near either relay shutoff deadline.
  if ((startup || active) && uint32_t(now - deadlineStart) >= deadlineDuration - 10) {
    espLink.stopListening();
    return;
  }
  espLink.listen();
  for (byte budget = 0; budget < 8 && espLink.available(); ++budget) {
    if (commandLine.push(espLink.read(), now)) {
      if (!strcmp(commandLine.data, "CMD status")) statusRequested = true;
      uint32_t epoch;
      if (!strncmp(commandLine.data, "TIME ", 5) &&
          parseDecimal(commandLine.data + 5, epoch)) {
        handleTimeSync(epoch);
        clockReportPending = true;
      }
      // CMD purge intentionally has NO execution path. Future commands must
      // pass local interlocks; never accept pin states or durations from UART.
    }
  }
  if (!telemetryTx[telemetryPos]) {
    if (uint32_t(now - lastLineEnd) < 100) return; // peer reply window
    telemetryPos = 0;
    if (!processClock.clockValid && (!timeRequestSent || uint32_t(now-lastTimeRequest)>=30000)) {
      strcpy(telemetryTx, "GET TIME\n");
      timeRequestSent = true; lastTimeRequest = now;
    } else if (completionPending) {
      snprintf(telemetryTx, sizeof telemetryTx,
        "EVENT purge_complete count=%lu duration_ms=%lu success=%s epoch=%lu time_valid=%u\n",
        (unsigned long)completedPurges, (unsigned long)lastPurgeDuration,
        measurements.successValid ? (measurements.success ? "true" : "false") : "unknown",
        (unsigned long)lastPurgeEpoch, lastPurgeTimeValid ? 1U : 0U);
      completionPending = false;
    } else if (statusRequested || uint32_t(now - lastSnapshot) >= 5000) {
      snprintf(telemetryTx, sizeof telemetryTx,
        "STAT uptime=%lu state=%s count=%lu duration_ms=%lu success=%s firmware=%s epoch=%lu time_valid=%u\n",
        (unsigned long)controllerUptime.seconds, startup ? "STARTUP" : active ? "PURGING" : "IDLE",
        (unsigned long)completedPurges, (unsigned long)lastPurgeDuration,
        measurements.successValid ? (measurements.success ? "true" : "false") : "unknown", UNO_FIRMWARE,
        (unsigned long)lastPurgeEpoch, lastPurgeTimeValid ? 1U : 0U);
      lastSnapshot = now; statusRequested = false; clockReportPending = true;
    } else if (clockReportPending) {
      snprintf(telemetryTx, sizeof telemetryTx,
        "CLOCK valid=%u sync_epoch=%lu sync_age=%lu correction=%ld\n",
        processClock.clockValid ? 1U : 0U, (unsigned long)processClock.syncEpoch,
        (unsigned long)processClock.age(),
        (long)(processClock.lastCorrectionSeconds > INT32_MAX ? INT32_MAX :
               processClock.lastCorrectionSeconds < INT32_MIN ? INT32_MIN : processClock.lastCorrectionSeconds));
      clockReportPending = false;
    } else return;
  }
  // SoftwareSerial TX is synchronous: only one ~1.04 ms byte per call,
  // spaced 3 ms apart. No flush(), blocking handshake, or wait for a peer.
  if (uint32_t(now - lastTxByte) >= 3) {
    espLink.write(telemetryTx[telemetryPos++]);
    lastTxByte = now;
    if (!telemetryTx[telemetryPos]) lastLineEnd = now;
  }
}
