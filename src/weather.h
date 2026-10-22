#pragma once
#include <Arduino.h>

// Hourly temperature and weather type for Belgrade from Open-Meteo, plus the local clock (NTP).
void weatherLoop();  // call every loop; fetches only while Wi-Fi is connected

bool weatherReady();  // clock synced + data covering the next HOURS_SHOWN hours
int weatherCurrentHour();
// Hour and temperature rows of the weather screen: one 7-char cell per hour, always equal length.
// Both rows start with one empty cell, so the current hour is the second cell; its '|'
// separators are at character columns WEATHER_CELL_CHARS - 1 and 2 * WEATHER_CELL_CHARS - 1.
constexpr int WEATHER_CELL_CHARS = 7;
void weatherRows(String& hours, String& temps);

// Weather type per hour, grouped from the WMO weather codes Open-Meteo returns.
enum class WeatherIcon : uint8_t {
  None, Sun, Moon, PartlySun, PartlyMoon, Cloud, Fog, Drizzle, Rain, Snow, Storm,
};
// One icon per cell of weatherRows() (the leading empty cell is None). Returns the count.
int weatherIcons(WeatherIcon* out, int max);
