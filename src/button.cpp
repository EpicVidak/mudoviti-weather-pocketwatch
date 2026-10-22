#include "button.h"

static constexpr uint32_t DEBOUNCE_MS = 30;

void Button::begin() {
  pinMode(pin_, INPUT_PULLUP);
}

void Button::update() {
  bool now = digitalRead(pin_) == LOW;
  uint32_t t = millis();
  if (now != raw_) {
    raw_ = now;
    rawChangedAt_ = t;
  }
  if (raw_ == pressed_ || t - rawChangedAt_ < DEBOUNCE_MS) return;

  pressed_ = raw_;
  if (pressed_) {
    pressedAt_ = t;
  } else if (t - pressedAt_ >= longMs_) {
    long_ = true;
  } else {
    short_ = true;
  }
}

bool Button::shortPress() {
  bool r = short_;
  short_ = false;
  return r;
}

bool Button::longPress() {
  bool r = long_;
  long_ = false;
  return r;
}

uint32_t Button::heldMs() const {
  return pressed_ ? millis() - pressedAt_ : 0;
}
