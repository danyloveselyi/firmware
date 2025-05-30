#pragma once

#include "../interfaces/ILogger.h"

/**
 * Default logger implementation for Store & Forward module
 * Uses the system's DEBUG_PORT for output
 */
class DefaultLogger : public ILogger
{
  public:
    /**
     * Constructor
     * @param defaultLevel The initial log level
     */
    DefaultLogger(LogLevel defaultLevel);

    // ILogger interface implementation
    void log(LogLevel level, const char *format, ...) override;
    void debug(const char *format, ...) override;
    void info(const char *format, ...) override;
    void warn(const char *format, ...) override;
    void error(const char *format, ...) override;
    void setLevel(LogLevel level) override;
    LogLevel getLevel() const override;

    /**
     * Print logger status information
     * Helper method for diagnostics
     */
    void printLoggerStatus();

  private:
    LogLevel level;
};

// Global default logger instance
extern DefaultLogger defaultLogger;
