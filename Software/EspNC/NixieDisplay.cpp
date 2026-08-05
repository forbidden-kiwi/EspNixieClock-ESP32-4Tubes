#include "NixieDisplay.h"

NixieDisplay::NixieDisplay() {
    for (int i = 0; i < 8; i++) {
        _frame[i] = 0;
    }
}

void NixieDisplay::begin() {
    pinMode(PIN_HV_LE, OUTPUT);
    pinMode(PIN_HV_BL, OUTPUT);
    pinMode(PIN_HV_DATA, OUTPUT);
    pinMode(PIN_HV_CLK, OUTPUT);
    digitalWrite(PIN_HV_BL, HIGH);
    disableAllSegments();
    updateDisplay(); // Clear shift registers before enabling HV
}

void NixieDisplay::enableSegment(byte segment) {
    // Frame is MSB-first: reverse byte order relative to segment index
    byte f = 7 - (segment / 8);
    byte b = segment % 8;
    _frame[f] |= 1 << b;
}

void NixieDisplay::disableSegments(const byte segments[], int count) {
    for (int i = 0; i < count; ++i) {
        disableSegment(segments[i]);
    }
}

void NixieDisplay::disableAllSegments() {
    for (int i = 0; i < 8; ++i) {
        _frame[i] = 0b00000000;
    }
}

void NixieDisplay::disableSegment(byte segment) {
    byte f = 7 - (segment / 8);
    byte b = segment % 8;
    _frame[f] &= ~(1 << b);
}

void NixieDisplay::updateDisplay() {
    digitalWrite(PIN_HV_LE, LOW);
    for (int i = 0; i < 8; ++i) {
        shiftOut(PIN_HV_DATA, PIN_HV_CLK, MSBFIRST, _frame[i]);
    }
    digitalWrite(PIN_HV_LE, HIGH);
}

void NixieDisplay::startSlotMachine(int startHour, int startMinute,
                                    int targetHour, int targetMinute) {
    if (_slotMachineActive) {
        return;
    }

    _slotMachineActive = true;
    _slotMachineStartTime = millis();

    int tempDigits[4] = {startHour / 10, startHour % 10, startMinute / 10, startMinute % 10};
    bool used[10] = {false};

    // Ensure distinct start digits so tubes do not look stuck on the same value
    for (int i = 0; i < 4; i++) {
        int digit = tempDigits[i];
        if (used[digit]) {
            for (int offset = 1; offset <= 9; offset++) {
                int newDigit = (digit + offset) % 10;
                if (!used[newDigit]) {
                    _startDigits[i] = newDigit;
                    used[newDigit] = true;
                    break;
                }
            }
        } else {
            _startDigits[i] = digit;
            used[digit] = true;
        }
        _digitActive[i] = true;
    }

    _targetDigits[0] = targetHour / 10;
    _targetDigits[1] = targetHour % 10;
    _targetDigits[2] = targetMinute / 10;
    _targetDigits[3] = targetMinute % 10;
}

void NixieDisplay::updateSlotMachine() {
    if (!_slotMachineActive) {
        return;
    }

    unsigned long elapsedTime = millis() - _slotMachineStartTime;
    int steps = elapsedTime / 50;       // 50 ms per step
    const int minCycleSteps = 80;       // At least ~4 s of spinning

    disableAllSegments();

    const byte* digitSegments[4] = {hourTens, hourUnits, minuteTens, minuteUnits};
    bool allInactive = true;

    for (int i = 0; i < 4; i++) {
        if (!_digitActive[i]) {
            enableSegment(digitSegments[i][_targetDigits[i]]);
            continue;
        }

        int startDigit = _startDigits[i];
        int targetDigit = _targetDigits[i];
        int currentDigit = (startDigit + steps) % 10;

        if (currentDigit == targetDigit && steps >= minCycleSteps) {
            _digitActive[i] = false;
        }

        disableSegments(digitSegments[i], 10);
        enableSegment(digitSegments[i][currentDigit]);

        if (_digitActive[i]) allInactive = false;
    }

    // Left colon between HH and MM only (no seconds separator)
    disableSegment(UpperRightDot);
    disableSegment(LowerRightDot);

    updateDisplay();

    if (allInactive) {
        _slotMachineActive = false;
    }
}

bool NixieDisplay::isSlotMachineActive() {
    return _slotMachineActive;
}
