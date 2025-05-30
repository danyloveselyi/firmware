#include "DefaultLogger.h"
#include "DebugConfiguration.h"
#include <cstdarg>
#include <cstdio>

// Create the global instance - explicitly set to DEBUG level by default for more verbose logging
DefaultLogger defaultLogger(ILogger::LogLevel::DEBUG);

DefaultLogger::DefaultLogger(LogLevel defaultLevel) : level(defaultLevel) {}

void DefaultLogger::setLevel(LogLevel newLevel)
{
    level = newLevel;
}

ILogger::LogLevel DefaultLogger::getLevel() const
{
    return level;
}

void DefaultLogger::log(LogLevel msgLevel, const char *format, ...)
{
    // Only log if the message level is less than or equal to the current level
    // (ERROR=0, DEBUG=3, so lower value = higher priority)
    if (static_cast<int>(msgLevel) > static_cast<int>(level)) {
        return;
    }

    va_list args;
    va_start(args, format);

    // Convert our LogLevel enum to a string for DEBUG_PORT.log
    const char *levelStr = (msgLevel == LogLevel::ERROR)  ? "ERROR"
                           : (msgLevel == LogLevel::WARN) ? "WARN"
                           : (msgLevel == LogLevel::INFO) ? "INFO"
                                                          : "DEBUG";

    // Use the global debug port from DebugConfiguration.h
    DEBUG_PORT.log(levelStr, format, args);

    va_end(args);
}

void DefaultLogger::debug(const char *format, ...)
{
    if (level < LogLevel::DEBUG)
        return;

    va_list args;
    va_start(args, format);
    DEBUG_PORT.log("DEBUG", format, args);
    va_end(args);
}

void DefaultLogger::info(const char *format, ...)
{
    if (level < LogLevel::INFO)
        return;

    va_list args;
    va_start(args, format);
    DEBUG_PORT.log("INFO", format, args);
    va_end(args);
}

void DefaultLogger::warn(const char *format, ...)
{
    if (level < LogLevel::WARN)
        return;

    va_list args;
    va_start(args, format);
    DEBUG_PORT.log("WARN", format, args);
    va_end(args);
}

void DefaultLogger::error(const char *format, ...)
{
    if (level < LogLevel::ERROR)
        return;

    va_list args;
    va_start(args, format);
    DEBUG_PORT.log("ERROR", format, args);
    va_end(args);
}

// Add a diagnostic method to help verify logger level
void DefaultLogger::printLoggerStatus()
{
    const char *levelStr = (level == LogLevel::ERROR)  ? "ERROR"
                           : (level == LogLevel::WARN) ? "WARN"
                           : (level == LogLevel::INFO) ? "INFO"
                                                       : "DEBUG";

    DEBUG_PORT.log("INFO", "DefaultLogger status - Current level: %s", levelStr);
}
