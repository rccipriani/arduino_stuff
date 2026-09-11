#pragma once
// Replace placeholders locally. Never commit actual credentials.
constexpr char WIFI_SSID[] = "YOUR_SSID";
constexpr char WIFI_PASSWORD[] = "YOUR_WIFI_PASSWORD";
constexpr char MQTT_HOST[] = "192.168.1.10";
constexpr uint16_t MQTT_PORT = 1883;
constexpr char MQTT_USER[] = "";
constexpr char MQTT_PASSWORD[] = "";
constexpr char MQTT_BASE[] = "compressor/purge_controller";
constexpr char NTP_SERVER[] = "pool.ntp.org";
constexpr char TIMEZONE[] = "UTC0"; // POSIX TZ; published timestamps remain UTC
