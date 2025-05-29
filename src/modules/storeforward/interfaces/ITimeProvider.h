#pragma once

#include <cstdint>

/**
 * Interface for time-related operations
 * Abstracts system time functions like millis() for better testability
 */
class ITimeProvider
{
  public:
    virtual ~ITimeProvider() = default;

    /**
     * Get current time in milliseconds since boot
     * @return Milliseconds since boot
     */
    virtual unsigned long getMillis() const = 0;

    /**
     * Get current unix timestamp
     * @return Current unix timestamp
     */
    virtual uint32_t getUnixTime() const = 0;

    /**
     * Sleep for specified milliseconds
     * @param ms Milliseconds to sleep
     */
    virtual void delay(unsigned long ms) const = 0;
};
