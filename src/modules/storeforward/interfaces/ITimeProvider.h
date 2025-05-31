#pragma once

#include <cstdint>

/**
 * Interface for time functionality
 */
class ITimeProvider
{
  public:
    virtual ~ITimeProvider() = default;

    // Get current Unix timestamp
    virtual uint32_t getUnixTime() const = 0;

    // Check if the time is valid
    virtual bool hasValidTime() const = 0;
};
