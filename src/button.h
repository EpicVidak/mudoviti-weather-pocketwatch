#pragma once
#include <Arduino.h>

// Debounced active-low push button (onboard BOOT = GPIO9).
class Button {
 public:
  Button(uint8_t pin, uint32_t longMs) : pin_(pin), longMs_(longMs) {}

  void begin();
  void update();  // call every loop

  bool shortPress();  // released before longMs (consumes the event)
  bool longPress();   // released after longMs (consumes the event)
  uint32_t heldMs() const;  // 0 when not pressed
  uint32_t longMs() const { return longMs_; }

 private:
  uint8_t pin_;
  uint32_t longMs_;
  bool raw_ = false;
  bool pressed_ = false;
  uint32_t rawChangedAt_ = 0;
  uint32_t pressedAt_ = 0;
  bool short_ = false;
  bool long_ = false;
};
