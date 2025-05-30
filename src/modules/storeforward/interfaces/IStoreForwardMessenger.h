#pragma once

#include "NodeDB.h"
#include "mesh/generated/meshtastic/mesh.pb.h"

/**
 * Interface for sending Store & Forward messages
 */
class IStoreForwardMessenger
{
  public:
    /**
     * Send a text notification to a node
     * @param to Destination node ID
     * @param text The notification text
     */
    virtual void sendTextNotification(NodeNum to, const char *text) = 0;

    /**
     * Send a history response message
     * @param to Destination node ID
     * @param numMessages Number of messages available
     * @param window Time window in seconds
     * @param lastIndex Last request index
     */
    virtual void sendHistoryResponse(NodeNum to, uint32_t numMessages, uint32_t window, uint32_t lastIndex) = 0;

    /**
     * Send statistics to a node
     * @param to Destination node ID
     * @param maxMessages Maximum messages capacity
     * @param currentMessages Current message count
     * @param overwrittenMessages Number of overwritten messages
     * @param uptime Uptime in seconds
     * @param heartbeatEnabled Whether heartbeat is enabled
     * @param returnMax Maximum messages to return per request
     * @param returnWindow Time window in minutes
     */
    virtual void sendStats(NodeNum to, uint32_t maxMessages, uint32_t currentMessages, uint32_t overwrittenMessages,
                           uint32_t uptime, bool heartbeatEnabled, uint32_t returnMax, uint32_t returnWindow) = 0;

    /**
     * Send a heartbeat message to the network
     * @param period Heartbeat period in seconds
     */
    virtual void sendHeartbeat(uint32_t period) = 0;

    /**
     * Request history from a server
     * @param serverNode Server node ID
     * @param minutes Time window in minutes
     */
    virtual void requestHistory(NodeNum serverNode, uint32_t minutes) = 0;

    /**
     * Request statistics from a server
     * @param serverNode Server node ID
     */
    virtual void requestStats(NodeNum serverNode) = 0;

    /**
     * Send a ping to a server
     * @param serverNode Server node ID
     */
    virtual void sendPing(NodeNum serverNode) = 0;

    /**
     * Prepare a history payload based on an original message
     * @param msg The original message to forward
     * @param dest The destination node
     * @return A newly allocated mesh packet ready to send
     */
    virtual meshtastic_MeshPacket *prepareHistoryPayload(const meshtastic_MeshPacket &msg, NodeNum dest) = 0;

    /**
     * Send a packet to the next hop
     * @param p The packet to send
     * @return True if the packet was sent
     */
    virtual bool sendToNextHop(const meshtastic_MeshPacket &p) = 0;

    /**
     * Check if this messenger has a router available
     * @return True if a router is available
     */
    virtual bool hasRouter() const = 0;

    /**
     * Virtual destructor
     */
    virtual ~IStoreForwardMessenger() = default;
};
