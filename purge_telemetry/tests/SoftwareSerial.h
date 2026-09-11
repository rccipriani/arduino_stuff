#pragma once
#include "Arduino.h"
#include <deque>
#include <string>
class SoftwareSerial {
public:
  std::deque<char> rx;
  std::string tx;
  bool listening=true;
  SoftwareSerial(int,int) {}
  void begin(int) {}
  bool listen() { listening=true; return true; }
  bool stopListening() { listening=false; rx.clear(); return true; }
  int available() { return int(rx.size()); }
  int read() { char c=rx.front(); rx.pop_front(); return c; }
  size_t write(char c) { tx+=c; ++testMillis; return 1; }
  void inject(const char* s) { while(*s) rx.push_back(*s++); }
};
