#pragma once

#include "../interfaces/ILogger.h"
#include "../interfaces/IStoreForwardMessenger.h"
#include "MeshService.h"
#include "Router.h"
#include "mesh/generated/meshtastic/storeforward.pb.h"

/**
 * Implementation of IStoreForwardMessenger that uses the Router and MeshService
 * to send messages over the mesh network.
 */
class StoreForwardMessenger : public IStoreForwardMessenger
{
  public:
    /**
     * Constructor
     * @param router The router to use for sending packets
     * @param service The mesh service to use
     * @param logger The logger to use
     */
    StoreForwardMessenger(Router &router, MeshService &service, ILogger &logger);

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

  private:
    Router &router;
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
};
