#include "StoreForwardLogger.h"
#include "DebugConfiguration.h"
#include <cstdarg>
#include <cstdio>

namespace meshtastic
{

// Create the global instance - initialize with INFO level by default
static StoreForwardLogger instance(ILogger::LogLevel::INFO);
StoreForwardLogger &sfLogger = instance;

StoreForwardLogger::StoreForwardLogger(LogLevel defaultLevel) : level(defaultLevel) {}

void StoreForwardLogger::setLevel(LogLevel newLevel)
{
    level = newLevel;
}

ILogger::LogLevel StoreForwardLogger::getLevel() const
{
    return level;
}

void StoreForwardLogger::debug(const char *format, ...)
{
    if (level < LogLevel::DEBUG)
        return;

    va_list args;
    va_start(args, format);
    DEBUG_PORT.log("DEBUG", (contextPrefix + format).c_str(), args);
    va_end(args);
}

void StoreForwardLogger::info(const char *format, ...)
{
    if (level < LogLevel::INFO)
        return;

    va_list args;
    va_start(args, format);
    DEBUG_PORT.log("INFO", (contextPrefix + format).c_str(), args);
    va_end(args);
}

void StoreForwardLogger::warn(const char *format, ...)
{
    if (level < LogLevel::WARN)
        return;

    va_list args;
    va_start(args, format);
    DEBUG_PORT.log("WARN", (contextPrefix + format).c_str(), args);
    va_end(args);
}

void StoreForwardLogger::error(const char *format, ...)
{
    if (level < LogLevel::ERROR)
        return;

    va_list args;
    va_start(args, format);
    DEBUG_PORT.log("ERROR", (contextPrefix + format).c_str(), args);
    va_end(args);
}

void StoreForwardLogger::log(LogLevel msgLevel, const char *format, ...)
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

    // Add our context prefix to the format string
    std::string prefixedFormat = contextPrefix + format;

    // Use the global debug port from DebugConfiguration.h
    DEBUG_PORT.log(levelStr, prefixedFormat.c_str(), args);

    va_end(args);
}

} // namespace meshtastic
