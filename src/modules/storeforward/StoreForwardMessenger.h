#pragma once

#include "MeshService.h"
#include "Router.h"
#include "interfaces/ILogger.h"
#include "interfaces/IStoreForwardMessenger.h"
#include "mesh/generated/meshtastic/storeforward.pb.h"
#include <string>

/**
 * Implementation of IStoreForwardMessenger for creating and sending Store & Forward protocol messages.
 * Handles message formatting and communication with the mesh network.
 */
class StoreForwardMessenger : public IStoreForwardMessenger
{
  public:
    /**
     * Constructor with dependency injection
     *
     * @param router The router for packet allocation and routing
     * @param service The mesh service for sending packets
     * @param logger The logger for diagnostic output
     */
    StoreForwardMessenger(Router &router, MeshService &service, ILogger &logger);

    // Implementation of IStoreForwardMessenger interface
    void sendTextNotification(NodeNum dest, const char *message) override;
    void sendHeartbeat(uint32_t heartbeatInterval) override;
    void sendStats(NodeNum to, uint32_t messageTotal, uint32_t messagesSaved, uint32_t messagesMax, uint32_t upTime,
                   bool heartbeatEnabled, uint32_t returnMax, uint32_t returnWindow) override;
    void sendHistoryResponse(NodeNum to, uint32_t messageCount, uint32_t windowTime, uint32_t lastRequestIndex) override;
    meshtastic_MeshPacket *prepareHistoryPayload(const meshtastic_MeshPacket &historyMessage, NodeNum dest) override;
    void requestHistory(NodeNum serverNode, uint32_t minutes = 0) override;
    void requestStats(NodeNum serverNode) override;
    void sendPing(NodeNum serverNode) override;

  private:
    Router &router;
    MeshService &service;
    ILogger &logger;

    // Helper method to create and allocate a packet
    meshtastic_MeshPacket *allocatePacket(NodeNum to, meshtastic_PortNum portNum, bool wantAck = false);
};
