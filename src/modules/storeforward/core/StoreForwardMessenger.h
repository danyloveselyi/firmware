#pragma once

#include "../interfaces/ILogger.h"
#include "../interfaces/IStoreForwardMessenger.h"
#include "MeshService.h"
#include "Router.h"
#include "mesh/generated/meshtastic/storeforward.pb.h" // Add missing include

class StoreForwardMessenger : public IStoreForwardMessenger
{
  public:
    /**
     * Constructor
     * @param service The mesh service to use
     * @param logger The logger to use
     * @param router The router to use for sending packets (optional)
     */
    StoreForwardMessenger(MeshService &service, ILogger &logger, Router *router = nullptr);

    // IStoreForwardMessenger interface implementation
    void sendTextNotification(NodeNum to, const char *text) override;
    void sendHistoryResponse(NodeNum to, uint32_t numMessages, uint32_t window, uint32_t lastIndex) override;
    void sendStats(NodeNum to, uint32_t maxMessages, uint32_t currentMessages, uint32_t overwrittenMessages, uint32_t uptime,
                   bool heartbeatEnabled, uint32_t returnMax, uint32_t returnWindow) override;
    void sendHeartbeat(uint32_t period) override;
    void requestHistory(NodeNum serverNode, uint32_t minutes) override;
    void requestStats(NodeNum serverNode) override;
    void sendPing(NodeNum serverNode) override;
    meshtastic_MeshPacket *prepareHistoryPayload(const meshtastic_MeshPacket &msg, NodeNum dest) override;

    // Send to next hop
    bool sendToNextHop(const meshtastic_MeshPacket &p) override;

    // Check if Router is available
    bool hasRouter() const override { return router != nullptr; }

  private:
    Router *router; // Can be nullptr
    MeshService &service;
    ILogger &logger;

    /**
     * Helper method to allocate a new packet for sending
     * @param to Destination node ID
     * @param portNum Protocol port number
     * @param wantAck Whether to request acknowledgment
     * @return A newly allocated mesh packet
     */
    meshtastic_MeshPacket *allocatePacket(NodeNum to, meshtastic_PortNum portNum, bool wantAck = false);

    // Helper methods
    bool sendProtobuf(meshtastic_StoreAndForward &packet, NodeNum destNum, meshtastic_StoreAndForward_RequestResponse rr);
};
