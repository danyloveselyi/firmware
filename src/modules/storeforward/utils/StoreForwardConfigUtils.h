#pragma once

#include "StoreForwardLogger.h"
#include "configuration.h"

namespace StoreForwardConfigUtils
{
/**
 * Check if Store & Forward module is enabled
 * @return true if enabled
 */
inline bool isEnabled()
{
    return moduleConfig.store_forward.enabled;
}

/**
 * Check if Store & Forward server mode is configured
 * @return true if server mode is configured
 */
inline bool isServerConfigured()
{
    return moduleConfig.store_forward.is_server;
}

/**
 * Check if server mode should be active based on configuration and available memory
 * @param hasEnoughMemory Whether the device has sufficient memory for server operation
 * @return true if server mode should be active
 */
inline bool isServerMode(bool hasEnoughMemory)
{
    return isEnabled() && isServerConfigured() && hasEnoughMemory;
}

/**
 * Get the maximum number of messages to return per request
 * @return Maximum number of messages
 */
inline uint32_t getHistoryReturnMax()
{
    return moduleConfig.store_forward.history_return_max > 0 ? moduleConfig.store_forward.history_return_max : 25; // Default
}

/**
 * Get the time window (in minutes) for history requests
 * @return Time window in minutes
 */
inline uint32_t getHistoryReturnWindow()
{
    return moduleConfig.store_forward.history_return_window > 0 ? moduleConfig.store_forward.history_return_window
                                                                : 240; // Default: 4 hours
}

/**
 * Check if heartbeat is enabled
 * @return true if heartbeat is enabled
 */
inline bool isHeartbeatEnabled()
{
    return moduleConfig.store_forward.heartbeat;
}

/**
 * Get the configured log level for Store & Forward module
 * @return The log level
 */
inline StoreForwardLogger::LogLevel getLogLevel()
{
    // In the future, this could be configured via a setting
    // For now, default to INFO level
    return StoreForwardLogger::LogLevel::INFO;
}

} // namespace StoreForwardConfigUtils
