#pragma once

#include "MeshTypes.h"
#include <cstdint>
#include <string>

/**
 * Interface for store & forward messaging capabilities
 */
class IStoreForwardMessenger
{
  public:
    virtual ~IStoreForwardMessenger() = default;

    // Core messaging methods (original interface methods)
    virtual meshtastic_MeshPacket *allocReply() = 0;
    virtual bool sendReply(meshtastic_MeshPacket *packet) = 0;
    virtual bool sendPayload(NodeNum dest, uint8_t portNum, const uint8_t *payload, size_t len) = 0;
    virtual bool sendText(NodeNum dest, uint8_t portNum, const char *text) = 0;

    // Extended messaging methods needed by implementation
    virtual void sendTextNotification(NodeNum to, const char *text) = 0;
    virtual void sendHistoryResponse(NodeNum to, uint32_t numMessages, uint32_t window, uint32_t lastIndex) = 0;
    virtual void sendStats(NodeNum to, uint32_t maxMessages, uint32_t currentMessages, uint32_t overwrittenMessages,
                           uint32_t uptime, bool hasStore, uint32_t requestsHandled, uint32_t window) = 0;
    virtual void sendHeartbeat(uint32_t period) = 0;
    virtual void requestHistory(NodeNum serverNode, uint32_t minutes) = 0;
    virtual void requestStats(NodeNum serverNode) = 0;
    virtual void sendPing(NodeNum serverNode) = 0;
    virtual meshtastic_MeshPacket *prepareHistoryPayload(const meshtastic_MeshPacket &msg, NodeNum dest) = 0;
    virtual bool sendToNextHop(const meshtastic_MeshPacket &p) = 0;

    // Router access
    virtual bool hasRouter() const = 0;
};
