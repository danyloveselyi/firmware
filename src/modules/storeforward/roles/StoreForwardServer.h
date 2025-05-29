#pragma once

#include "StoreForwardBaseRole.h"
#include "utils/StoreForwardLogger.h"

class StoreForwardModule; // Forward declaration

class StoreForwardServer : public StoreForwardBaseRole
{
  public:
    /**
     * Constructor
     * @param historyManager The history manager to use
     * @param messenger The messenger to use
     * @param logger The logger to use
     */
    StoreForwardServer(IStoreForwardHistoryManager &historyManager, IStoreForwardMessenger &messenger,
                       StoreForwardLogger &logger);

    /**
     * Periodic processing
     */
    void onRunOnce() override;

    /**
     * Process text commands for server
     */
    void processTextCommand(const meshtastic_MeshPacket &packet) override;

    /**
     * Send requested history to a node
     * @param to The node to send history to
     * @param secondsAgo Only include messages newer than this many seconds ago
     */
    void historySend(NodeNum to, uint32_t secondsAgo);

    /**
     * Send server statistics to a node
     * @param to The node to send stats to
     */
    void sendStats(NodeNum to);

    /**
     * Send a heartbeat message to the network
     */
    void sendHeartbeat();

    /**
     * Check if the server is currently busy sending messages
     */
    bool isBusy() const override { return busy; }

    /**
     * Get the node that the server is currently sending to
     * @return The node ID of the current recipient
     */
    NodeNum getBusyRecipient() const { return busyTo; }

    /**
     * Get the last time filter used for message retrieval
     * @return The timestamp
     */
    uint32_t getLastTime() const { return last_time; }

    /**
     * Get the current request count in a busy session
     * @return The number of messages sent so far
     */
    uint32_t getRequestCount() const { return requestCount; }

    /**
     * Prepare a message payload for sending from history
     * @param dest The destination node
     * @param index The index in the message queue
     * @return A prepared mesh packet, or nullptr if no more messages
     */
    meshtastic_MeshPacket *prepareHistoryPayload(NodeNum dest, uint32_t index);

  private:
    // Status tracking
    bool busy = false;
    NodeNum busyTo = 0;
    uint32_t last_time = 0;
    uint32_t requestCount = 0;

    // Heartbeat tracking
    unsigned long lastHeartbeatTime = 0;

    // Configuration values (can be loaded from config)
    uint32_t historyReturnMax = 25;
    uint32_t historyReturnWindow = 240;              // minutes
    const uint32_t heartbeatInterval = 900;          // seconds (15 min)
    const unsigned long HEARTBEAT_INTERVAL = 900000; // 15 minutes

    // Helper methods
    bool sendNextHistoryPacket();

    // Make StoreForwardModule a friend so it can access private methods
    friend class StoreForwardModule;
};
