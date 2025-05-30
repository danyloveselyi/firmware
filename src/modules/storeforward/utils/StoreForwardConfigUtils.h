#pragma once

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
 * Get the maximum number of retries for message sending
 */
inline uint8_t getMaxRetries()
{
    // These fields don't exist in the current protobuf definition, use defaults
    return 3; // Default to 3 retries
}

/**
 * Get the retry timeout in milliseconds
 */
inline uint32_t getRetryTimeout()
{
    // These fields don't exist in the current protobuf definition, use defaults
    return 5000; // Default to 5 seconds (5000 ms)
}

/**
 * Get the log level based on configuration
 * @return The log level
 */
inline uint8_t getLogLevel()
{
    // In the future, this could be configured via a setting
    // For now, default to INFO level
    return 3; // Default to INFO level
}

} // namespace StoreForwardConfigUtils
