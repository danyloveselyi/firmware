#include "StoreForwardLogger.h" // Fix include path to use local file
#include <cstdio>

StoreForwardLogger::StoreForwardLogger(ILogger &baseLogger, const char *context, LogLevel level)
    : baseLogger(baseLogger), context(context), logLevel(level)
{
}

bool StoreForwardLogger::shouldLog(LogLevel level) const
{
    return level >= logLevel;
}

void StoreForwardLogger::setLogLevel(LogLevel level)
{
    logLevel = level;
}

StoreForwardLogger::LogLevel StoreForwardLogger::getLogLevel() const
{
    return logLevel;
}

void StoreForwardLogger::log(Level level, const char *format, ...)
{
    if (!shouldLog(convertLevel(level)))
        return;

    va_list args;
    va_start(args, format);
    logWithContext(level, format, args);
    va_end(args);
}

void StoreForwardLogger::debug(const char *format, ...)
{
    if (!shouldLog(LogLevel::DEBUG))
        return;

    va_list args;
    va_start(args, format);
    logWithContext(Level::DEBUG, format, args);
    va_end(args);
}

void StoreForwardLogger::info(const char *format, ...)
{
    if (!shouldLog(LogLevel::INFO))
        return;

    va_list args;
    va_start(args, format);
    logWithContext(Level::INFO, format, args);
    va_end(args);
}

void StoreForwardLogger::warn(const char *format, ...)
{
    if (!shouldLog(LogLevel::WARN))
        return;

    va_list args;
    va_start(args, format);
    logWithContext(Level::WARN, format, args);
    va_end(args);
}

void StoreForwardLogger::error(const char *format, ...)
{
    if (!shouldLog(LogLevel::ERROR))
        return;

    va_list args;
    va_start(args, format);
    logWithContext(Level::ERROR, format, args);
    va_end(args);
}

void StoreForwardLogger::logWithContext(Level level, const char *format, va_list args)
{
    // Create a buffer for the formatted message with context
    char contextBuffer[256];
    snprintf(contextBuffer, sizeof(contextBuffer), "[%s] %s", context, format);

    // Pass the context-prefixed message to the base logger
    baseLogger.log(level, contextBuffer, args);
}

ILogger::Level StoreForwardLogger::convertLevel(LogLevel level) const
{
    switch (level) {
    case LogLevel::TRACE:
        return Level::DEBUG; // We don't have TRACE in ILogger
    case LogLevel::DEBUG:
        return Level::DEBUG;
    case LogLevel::INFO:
        return Level::INFO;
    case LogLevel::WARN:
        return Level::WARN;
    case LogLevel::ERROR:
        return Level::ERROR;
    case LogLevel::NONE:
        return Level::ERROR; // Never log if NONE
    default:
        return Level::INFO;
    }
}

StoreForwardLogger::LogLevel StoreForwardLogger::convertLevel(Level level) const
{
    switch (level) {
    case Level::DEBUG:
        return LogLevel::DEBUG;
    case Level::INFO:
        return LogLevel::INFO;
    case Level::WARN:
        return LogLevel::WARN;
    case Level::ERROR:
        return LogLevel::ERROR;
    default:
        return LogLevel::INFO;
    }
}
