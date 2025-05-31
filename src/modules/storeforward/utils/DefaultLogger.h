#pragma once

#include "../interfaces/ILogger.h"
#include <cstdarg>

/**
 * Default implementation of the ILogger interface that uses the LOG_* macros
 */
class DefaultLogger : public ILogger
{
  public:
    DefaultLogger() : level(LogLevel::LEVEL_INFO) {}

    // Implement the log method
    void log(LogLevel level, const char *format, ...) override;

    // Basic logging methods
    void critical(const char *format, ...) override;
    void error(const char *format, ...) override;
    void warn(const char *format, ...) override;
    void info(const char *format, ...) override;
    void debug(const char *format, ...) override;
    void trace(const char *format, ...) override;

    // Level management
    void setLevel(LogLevel level) override { this->level = level; }
    LogLevel getLevel() const override { return level; }

  private:
    LogLevel level;
};

// Global instance of the default logger
extern DefaultLogger defaultLogger;
