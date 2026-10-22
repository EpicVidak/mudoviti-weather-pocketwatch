#pragma once
#include <Arduino.h>

// Saved Wi-Fi networks, persisted in NVS ("wifi" namespace).
namespace wifiStore {

constexpr int MAX_NETWORKS = 20;

void load();
// Adds a network, or updates the password if the SSID is already saved.
// When the list is full, the oldest entry is dropped.
void add(const String& ssid, const String& pass);
bool remove(const String& ssid);
int count();
String ssidAt(int i);
String passAt(int i);
int find(const String& ssid);  // index, or -1

}  // namespace wifiStore
