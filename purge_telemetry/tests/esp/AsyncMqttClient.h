#pragma once
#include <functional>
#include <string>
struct AsyncMqttClientMessageProperties {bool retain=false;};
enum class AsyncMqttClientDisconnectReason { TCP };
class AsyncMqttClient {
public:
 bool online=true; int publications=0;
 std::string lastTopic,lastPayload;
 bool connected(){return online;} void connect(){}
 void disconnect(bool){online=false;}
 void setServer(const char*,uint16_t){} void setClientId(const char*){}
 void setCleanSession(bool){} void setKeepAlive(int){}
 void setWill(const char*,int,bool,const char*){}
 void setCredentials(const char*,const char*){}
 template<class T> void onConnect(T){}
 template<class T> void onDisconnect(T){}
 template<class T> void onPublish(T){}
 template<class T> void onMessage(T){}
 void subscribe(const char*,int){}
 uint16_t publish(const char* topic,int,bool,const char* payload) {
   ++publications;lastTopic=topic;lastPayload=payload;return 1;
 }
};
