#ifndef NixieDisplay_h
#define NixieDisplay_h

#include "Arduino.h"
#include "Globals.h"

// Segment indices in the 64-bit HV5622 map (Z570M / IN-16 / IN-17 digit order).
// This build drives HH:MM only; secondTens/secondUnits are unused.
const byte UpperLeftDot   = 31;
const byte LowerLeftDot   = 30;
const byte UpperRightDot  = 63;
const byte LowerRightDot  = 62;
const byte hourTens[]     = {9,0,1,2,3,4,5,6,7,8};
const byte hourUnits[]    = {19,10,11,12,13,14,15,16,17,18};
const byte minuteTens[]   = {29,20,21,22,23,24,25,26,27,28};
const byte minuteUnits[]  = {41,32,33,34,35,36,37,38,39,40};
const byte secondTens[]   = {51,42,43,44,45,46,47,48,49,50};
const byte secondUnits[]  = {61,52,53,54,55,56,57,58,59,60};


class NixieDisplay {
  public:
    NixieDisplay();
    void begin();
    void enableSegment(byte segment);
    void disableSegments(const byte segments[], int count);
    void disableAllSegments();
    void disableSegment(byte segment);
    void updateDisplay();
    void startSlotMachine(int startHour, int startMinute,
                         int targetHour, int targetMinute);
    void updateSlotMachine();
    bool isSlotMachineActive();

  private:
    byte _frame[8];
    bool _slotMachineActive = false;
    unsigned long _slotMachineStartTime = 0;
    uint8_t _startDigits[4];
    uint8_t _targetDigits[4];
    bool _digitActive[4];
};

#endif
