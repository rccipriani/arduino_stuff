#include "../Arduino.h"
#include <string.h>
#include <assert.h>
#include <iostream>
uint32_t testMillis=1000;
int pinStates[20]={};
#include "../../esp01/esp01.ino"
void receive(const char* input) {
 char copy[300]; strcpy(copy,input); receiveLine(copy);
}
int main() {
 setup();
 assert(!ntpSynchronized);
 timeSendPending=true;
 serviceSerial(testMillis);
 assert(Serial.tx.empty()); // plausible host time alone must not authorize TIME
 receive("STAT uptime=123 state=IDLE count=2 duration_ms=5000 success=unknown firmware=1.3.0 epoch=1789088325 time_valid=1");
 assert(haveController && count==2 && timeValid && lastEpoch==1789088325);
 std::string stamp=lastTime;
 assert(!stamp.empty() && stamp.back()=='Z');
 receive("STAT uptime=124 state=IDLE count=999 duration_ms=5000 success=unknown firmware=1.3.0 epoch=0 time_valid=1");
 assert(count==2); // invalid history rejects whole snapshot
 receive("EVENT purge_complete count=3 duration_ms=5000 success=unknown epoch=0 time_valid=0");
 assert(count==3 && !timeValid && lastTime[0]==0);
 receive("EVENT purge_complete count=4294967296 duration_ms=5000 success=unknown epoch=0 time_valid=0");
 assert(count==3);
 receive("CLOCK valid=1 sync_epoch=1789088325 sync_age=600 correction=-8");
 assert(unoClockValid && unoSyncAge==600 && unoCorrection==-8);
 ntpCallback(); assert(ntpSynchronized);
 serviceSerial(testMillis); assert(Serial.tx.find("TIME ")==0);
 Serial.tx.clear(); receive("GET TIME"); serviceSerial(testMillis);
 assert(Serial.tx.find("TIME ")==0);
 AsyncMqttClientMessageProperties properties;
 testMillis=10000; onMessage(purgeCommand,(char*)"request",properties,7,0,7);
 assert(!requestStatus);
 testMillis=16000; properties.retain=true;
 onMessage(statusCommand,(char*)"request",properties,7,0,7); assert(!requestStatus);
 properties.retain=false;
 onMessage(statusCommand,(char*)"request",properties,7,0,7); assert(requestStatus);
 inFlight=0; publishNext(); assert(mqtt.publications==1);
 for(int i=0;i<10000;++i) {++testMillis;publishNext();}
 assert(mqtt.publications==1); // no queue growth while PUBACK is absent
 inFlight=0; topicIndex=4; testMillis=40000; publishNext();
 assert(mqtt.lastTopic.find("controller/available")!=std::string::npos && mqtt.lastPayload=="false");
 std::cout<<"PASS: gateway history/validity parsing, no cold-boot TIME, NTP/GET TIME, disabled/retained commands, bounded MQTT publication, stale Uno availability\n";
}
