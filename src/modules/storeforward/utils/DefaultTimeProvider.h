#pragma once

#include "RTC.h"
#include "interfaces/ITimeProvider.h"

/**
 * Default implementation of ITimeProvider using system functions
 */
class DefaultTimeProvider : public ITimeProvider
{
  public:
    /**
     * Get current time in milliseconds since boot
     * @return Milliseconds since boot
     */
    unsigned long getMillis() const override { return millis(); }

    /**
     * Get current unix timestamp
     * @return Current unix timestamp
     */
    uint32_t getUnixTime() const override { return getTime(); }

    /**
     * Sleep for specified milliseconds
     * @param ms Milliseconds to sleep
     */
    void delay(unsigned long ms) const override { ::delay(ms); }
};
