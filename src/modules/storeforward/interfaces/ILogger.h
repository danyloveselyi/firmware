#pragma once

#include <string>

/**
 * Interface for logging functionality
 */
class ILogger
{
  public:
    // Log levels in ascending order of verbosity
    enum LogLevel { LEVEL_CRITICAL = 0, LEVEL_ERROR = 1, LEVEL_WARN = 2, LEVEL_INFO = 3, LEVEL_DEBUG = 4, LEVEL_TRACE = 5 };

    virtual ~ILogger() = default;

    // Basic log method that can be overridden
    virtual void log(LogLevel level, const char *format, ...) = 0;

    // Basic logging methods
    virtual void critical(const char *format, ...) = 0;
    virtual void error(const char *format, ...) = 0;
    virtual void warn(const char *format, ...) = 0;
    virtual void info(const char *format, ...) = 0;
    virtual void debug(const char *format, ...) = 0;
    virtual void trace(const char *format, ...) = 0;

    // Level management
    virtual void setLevel(LogLevel level) = 0;
    virtual LogLevel getLevel() const = 0;
};
