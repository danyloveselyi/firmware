#pragma once

#include "../interfaces/ILogger.h"
#include <string>

namespace meshtastic
{

/**
 * StoreForwardLogger - A specialized logger for the Store & Forward module
 * that includes module-specific context in log messages.
 */
class StoreForwardLogger : public ILogger
{
  public:
    /**
     * Constructor
     * @param defaultLevel The initial log level
     */
    StoreForwardLogger(LogLevel defaultLevel = LogLevel::INFO);

    /**
     * Set the current logging level
     * @param level The new logging level
     */
    void setLevel(LogLevel level) override;

    /**
     * Get the current logging level
     * @return The current logging level
     */
    LogLevel getLevel() const override;

    /**
     * Log a message with the specified level
     * @param level The log level
     * @param format The format string
     * @param ... The arguments to format
     */
    void log(LogLevel level, const char *format, ...) override;

    /**
     * Log a debug message
     * @param format The format string
     * @param ... The arguments to format
     */
    void debug(const char *format, ...) override;

    /**
     * Log an info message
     * @param format The format string
     * @param ... The arguments to format
     */
    void info(const char *format, ...) override;

    /**
     * Log a warning message
     * @param format The format string
     * @param ... The arguments to format
     */
    void warn(const char *format, ...) override;

    /**
     * Log an error message
     * @param format The format string
     * @param ... The arguments to format
     */
    void error(const char *format, ...) override;

  private:
    LogLevel level;
    std::string contextPrefix = "S&F: ";
};

// Global instance for the module to use
extern StoreForwardLogger &sfLogger;

} // namespace meshtastic
