#pragma once

#include <string>

/**
 * Simple logger interface to abstract away the actual logging implementation.
 * This improves testability by removing direct dependencies on platform-specific logging.
 */
class ILogger
{
  public:
    enum class Level { DEBUG, INFO, WARN, ERROR };

    virtual ~ILogger() = default;

    virtual void log(Level level, const char *format, ...) = 0;
    virtual void debug(const char *format, ...) = 0;
    virtual void info(const char *format, ...) = 0;
    virtual void warn(const char *format, ...) = 0;
    virtual void error(const char *format, ...) = 0;
};
