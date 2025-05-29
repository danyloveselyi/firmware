#pragma once

#include "IStoreForwardRole.h"
#include "interfaces/IStoreForwardHistoryManager.h"
#include "interfaces/IStoreForwardMessenger.h"

class StoreForwardModule; // Forward declaration

class StoreForwardServer : public IStoreForwardRole
{
  public:
    // Updated constructor to use interface types instead of concrete implementations
    StoreForwardServer(IStoreForwardHistoryManager &historyManager, IStoreForwardMessenger &messenger);

    // Interface implementation
    void onRunOnce() override;
    void onReceivePacket(const meshtastic_MeshPacket &packet) override;

    // Server-specific methods
    void historySend(NodeNum to, uint32_t secondsAgo);
    void sendStats(NodeNum to);
    void sendHeartbeat();

    // Public properties for status tracking
    bool isBusy() const { return busy; }
    NodeNum getBusyRecipient() const { return busyTo; }
    uint32_t getLastTime() const { return last_time; }
    uint32_t getRequestCount() const { return requestCount; }

    // Make this public so StoreForwardModule can access it
    meshtastic_MeshPacket *prepareHistoryPayload(NodeNum dest, uint32_t index);

  private:
    IStoreForwardHistoryManager &historyManager;
    IStoreForwardMessenger &messenger;

    // Status tracking
    bool busy = false;
    NodeNum busyTo = 0;
    uint32_t last_time = 0;
    uint32_t requestCount = 0;

    // Heartbeat tracking
    unsigned long lastHeartbeatTime = 0;
    unsigned long lastStatusLog = 0;

    // Configuration values (can be loaded from config)
    uint32_t historyReturnMax = 25;
    uint32_t historyReturnWindow = 240;              // minutes
    const uint32_t heartbeatInterval = 900;          // seconds (15 min)
    const unsigned long STATUS_LOG_INTERVAL = 60000; // 1 minute
    const unsigned long HEARTBEAT_INTERVAL = 900000; // 15 minutes

    // Helper methods
    void processTextCommand(const meshtastic_MeshPacket &packet);
    bool sendNextHistoryPacket();

    // Make StoreForwardModule a friend so it can access private methods
    friend class StoreForwardModule;
};
