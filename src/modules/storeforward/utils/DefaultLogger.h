#pragma once

#include "../interfaces/ILogger.h"
#include "DebugConfiguration.h"
#include <cstdarg>

/**
 * Default implementation of ILogger that uses the system's logging facility.
 * In production, this wraps the LOG_XXX macros from DebugConfiguration.h
 */
class DefaultLogger : public ILogger
{
  public:
    void log(Level level, const char *format, ...) override
    {
        va_list args;
        va_start(args, format);

        char buffer[256];
        vsnprintf(buffer, sizeof(buffer), format, args);

        switch (level) {
        case Level::DEBUG:
            LOG_DEBUG("%s", buffer);
            break;
        case Level::INFO:
            LOG_INFO("%s", buffer);
            break;
        case Level::WARN:
            LOG_WARN("%s", buffer);
            break;
        case Level::ERROR:
            LOG_ERROR("%s", buffer);
            break;
        }

        va_end(args);
    }

    void debug(const char *format, ...) override
    {
        va_list args;
        va_start(args, format);
        char buffer[256];
        vsnprintf(buffer, sizeof(buffer), format, args);
        LOG_DEBUG("%s", buffer);
        va_end(args);
    }

    void info(const char *format, ...) override
    {
        va_list args;
        va_start(args, format);
        char buffer[256];
        vsnprintf(buffer, sizeof(buffer), format, args);
        LOG_INFO("%s", buffer);
        va_end(args);
    }

    void warn(const char *format, ...) override
    {
        va_list args;
        va_start(args, format);
        char buffer[256];
        vsnprintf(buffer, sizeof(buffer), format, args);
        LOG_WARN("%s", buffer);
        va_end(args);
    }

    void error(const char *format, ...) override
    {
        va_list args;
        va_start(args, format);
        char buffer[256];
        vsnprintf(buffer, sizeof(buffer), format, args);
        LOG_ERROR("%s", buffer);
        va_end(args);
    }
};

// Create a singleton instance for use throughout the application
extern DefaultLogger defaultLogger;
