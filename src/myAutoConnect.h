#ifndef _MYAUTOCONNECT_H_
#define _MYAUTOCONNECT_H_

#if defined(ARDUINO_ARCH_ESP8266)
  #include <ESP8266WiFi.h>
  #include <ESP8266WebServer.h>
#elif defined(ARDUINO_ARCH_ESP32)
  #include <WiFi.h>
  #include <WebServer.h>
#endif

#include <SPIFFS.h>
#include <ArduinoJson.h>
#include <AutoConnect.h>

bool autoConnectWifi(AutoConnect::DetectExit_ft fn=NULL, bool forcePortal=false, const char * portalSSID=NULL);


#endif  // _MYAUTOCONNECT_H_