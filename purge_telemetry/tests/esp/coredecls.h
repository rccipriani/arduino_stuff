#pragma once
#include <functional>
std::function<void()> ntpCallback;
inline void settimeofday_cb(std::function<void()> cb){ntpCallback=cb;}
