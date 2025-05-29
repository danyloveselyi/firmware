#pragma once

#include "IStoreForwardRole.h"
#include "interfaces/IStoreForwardMessenger.h"

/**
 * StoreForwardClient implements the client-side functionality of the
 * Store & Forward module. It handles discovering S&F servers on the network,
 * requesting stored messages, and processing received S&F messages.
 */
class StoreForwardClient : public IStoreForwardRole
{
  public:
    /**
     * Constructor - initializes a new Store & Forward client
     * @param messenger The messenger to use for communication
     */
    StoreForwardClient(IStoreForwardMessenger &messenger);

    /**
     * Periodic processing - checks server availability and handles retries
     */
    void onRunOnce() override;

    /**
     * Process received packets related to Store & Forward functionality
     * @param packet The received mesh packet
     */
    void onReceivePacket(const meshtastic_MeshPacket &packet) override;

    /**
     * Requests message history from a specified server
     * @param serverNode The node ID of the server
     * @param minutes Optional time window in minutes (0 for default)
     */
    void requestHistory(NodeNum serverNode, uint32_t minutes = 0);

    /**
     * Request server statistics
     * @param serverNode The server node ID
     */
    void requestStats(NodeNum serverNode);

    /**
     * Send a ping to a server
     * @param serverNode The server node ID
     */
    void sendPing(NodeNum serverNode);

    /**
     * Gets the configured heartbeat interval
     * @return Heartbeat interval in seconds
     */
    uint32_t getHeartbeatInterval() const { return heartbeatInterval; }

    /**
     * Checks if the client has had recent contact with a server
     * @return True if there has been recent server contact, false otherwise
     */
    bool hasServerContact() const { return lastHeartbeat > 0; }

    /**
     * Gets the timestamp of the last heartbeat received from the server
     * @return Timestamp of the last heartbeat
     */
    unsigned long getLastHeartbeat() const { return lastHeartbeat; }

  private:
    IStoreForwardMessenger &messenger;

    // Server tracking
    NodeNum primaryServer = 0;
    bool serverAvailable = false;

    // Time tracking
    unsigned long lastRequestTime = 0;
    unsigned long lastHeartbeat = 0;
    unsigned long retry_delay = 0;
    uint32_t heartbeatInterval = 900; // Default 15 min

    // Configuration values
    const unsigned long REQUEST_INTERVAL = 300000; // 5 minutes

    // Process S&F specific messages
    void processStoreForwardMessage(const meshtastic_MeshPacket &packet, const meshtastic_StoreAndForward &data);
};
