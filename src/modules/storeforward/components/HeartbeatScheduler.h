#pragma once

#include "interfaces/ILogger.h"
#include "interfaces/IStoreForwardMessenger.h"
#include "interfaces/ITimeProvider.h"

/**
 * Manages scheduled tasks like heartbeats and status logging
 * Uses the injected time provider for better testability
 */
class HeartbeatScheduler
{
  public:
    /**
     * Constructor
     * @param messenger The messenger to use for sending heartbeats
     * @param timeProvider The time provider to use
     * @param logger The logger to use
     * @param heartbeatInterval The interval between heartbeats in seconds
     * @param statusLogInterval The interval between status log messages in milliseconds
     */
    HeartbeatScheduler(IStoreForwardMessenger &messenger, ITimeProvider &timeProvider, ILogger &logger,
                       uint32_t heartbeatInterval = 900, unsigned long statusLogInterval = 60000);

    /**
     * Check and run any scheduled tasks
     * @param heartbeatEnabled Whether heartbeats are enabled
     * @param statusMessage The status message to log (if it's time)
     * @return true if any task was executed
     */
    bool runScheduledTasks(bool heartbeatEnabled, const char *statusMessage);

    /**
     * Reset the heartbeat timer
     * Forces a heartbeat to be sent at the next check
     */
    void resetHeartbeatTimer();

  private:
    IStoreForwardMessenger &messenger;
    ITimeProvider &timeProvider;
    ILogger &logger;

    unsigned long lastHeartbeatTime = 0;
    unsigned long lastStatusLogTime = 0;

    uint32_t heartbeatIntervalMs;
    unsigned long statusLogIntervalMs;
};
