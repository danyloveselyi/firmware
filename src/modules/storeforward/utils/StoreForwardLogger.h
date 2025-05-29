#pragma once

#include "../interfaces/ILogger.h"
#include <cstdarg>
#include <string>

/**
 * Enhanced logger for Store & Forward
 * Adds context information to log messages and supports configurable log levels
 */
class StoreForwardLogger : public ILogger
{
  public:
    enum class LogLevel { TRACE, DEBUG, INFO, WARN, ERROR, NONE };

    /**
     * Constructor
     * @param baseLogger The underlying logger to use
     * @param context The context string to prepend to all messages (e.g., "S&F-Server")
     * @param level The minimum log level to output
     */
    StoreForwardLogger(ILogger &baseLogger, const char *context, LogLevel level = LogLevel::INFO);

    void log(Level level, const char *format, ...) override;
    void debug(const char *format, ...) override;
    void info(const char *format, ...) override;
    void warn(const char *format, ...) override;
    void error(const char *format, ...) override;

    /**
     * Check if a log message at the specified level should be logged
     * @param level The log level to check
     * @return true if the log should be emitted
     */
    bool shouldLog(LogLevel level) const;

    /**
     * Set the current log level
     * @param level The new log level
     */
    void setLogLevel(LogLevel level);

    /**
     * Get the current log level
     * @return The current log level
     */
    LogLevel getLogLevel() const;

  private:
    ILogger &baseLogger;
    const char *context;
    LogLevel logLevel;

    // Helper method to prepend context
    void logWithContext(Level level, const char *format, va_list args);

    // Convert internal LogLevel to ILogger Level
    Level convertLevel(LogLevel level) const;

    // Convert ILogger Level to internal LogLevel
    LogLevel convertLevel(Level level) const;
};
