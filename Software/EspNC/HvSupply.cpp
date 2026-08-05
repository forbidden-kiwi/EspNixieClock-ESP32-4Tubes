#include "HvSupply.h"

HvSupply::HvSupply() {}

void HvSupply::begin() {
  pinMode(PIN_HV_EN, OUTPUT);
  digitalWrite(PIN_HV_EN, HIGH); // 170V is now present on the board
  _hvon = true;
}

bool HvSupply::isOn() {
  return _hvon;
}

void HvSupply::switchOn() {
  digitalWrite(PIN_HV_EN, HIGH);
  _hvon = true;
}

void HvSupply::switchOff() {
  digitalWrite(PIN_HV_EN, LOW);
  _hvon = false;
}
