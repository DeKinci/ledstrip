#include "Arduino.h"

// Global Serial instance for mock
MockSerial Serial;

// Mock millis value
uint32_t _mockMillisValue = 0;