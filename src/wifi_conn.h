#pragma once
#include <Arduino.h>

enum class WifiState {
  Scanning,    // looking for saved networks in range
  Connecting,  // trying one candidate
  Connected,
  Backoff,     // nothing worked, waiting before the next scan
  Portal,      // WiFiManager setup hotspot is open
};

void wifiBegin();
void wifiLoop();  // non-blocking, call every loop

bool wifiIsConnected();
WifiState wifiState();
String wifiSsid();  // network being tried / connected to

void wifiStartPortal();
void wifiStopPortal();
uint32_t wifiPortalSecondsLeft();
