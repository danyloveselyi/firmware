#pragma once

#include <cstdint>

/**
 * Logger interface for Store & Forward module
 */
class ILogger
{
  public:
    // Define log levels enum
    enum LogLevel {
        ERROR = 0, // Highest priority
        WARN = 1,
        INFO = 2,
        DEBUG = 3 // Lowest priority
    };

    // Virtual methods for logging
    virtual void log(LogLevel level, const char *format, ...) = 0;
    virtual void debug(const char *format, ...) = 0;
    virtual void info(const char *format, ...) = 0;
    virtual void warn(const char *format, ...) = 0;
    virtual void error(const char *format, ...) = 0;

    // Level management
    virtual void setLevel(LogLevel level) = 0;
    virtual LogLevel getLevel() const = 0;

    // Virtual destructor
    virtual ~ILogger() = default;
};
