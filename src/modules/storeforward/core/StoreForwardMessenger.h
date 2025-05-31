#pragma once

#include "../interfaces/ILogger.h"
#include "../interfaces/IStoreForwardMessenger.h"
#include "MeshService.h"
#include "Router.h"

class StoreForwardMessenger : public IStoreForwardMessenger
{
  public:
    StoreForwardMessenger(MeshService &service, ILogger &logger, Router *router = nullptr);

    // Core messaging methods (from interface)
    meshtastic_MeshPacket *allocReply() override;
    bool sendReply(meshtastic_MeshPacket *packet) override;
    bool sendPayload(NodeNum dest, uint8_t portNum, const uint8_t *payload, size_t len) override;
    bool sendText(NodeNum dest, uint8_t portNum, const char *text) override;

    // Extended messaging methods (from interface)
    void sendTextNotification(NodeNum to, const char *text) override;
    void sendHistoryResponse(NodeNum to, uint32_t numMessages, uint32_t window, uint32_t lastIndex) override;
    void sendStats(NodeNum to, uint32_t maxMessages, uint32_t currentMessages, uint32_t overwrittenMessages, uint32_t uptime,
                   bool hasStore, uint32_t requestsHandled, uint32_t window) override;
    void sendHeartbeat(uint32_t period) override;
    void requestHistory(NodeNum serverNode, uint32_t minutes) override;
    void requestStats(NodeNum serverNode) override;
    void sendPing(NodeNum serverNode) override;
    meshtastic_MeshPacket *prepareHistoryPayload(const meshtastic_MeshPacket &msg, NodeNum dest) override;

    // Router functionality
    bool hasRouter() const override { return router != nullptr; }
    bool sendToNextHop(const meshtastic_MeshPacket &p) override;

  protected:
    // Helper method to allocate packets with common settings
    meshtastic_MeshPacket *allocatePacket(NodeNum to, meshtastic_PortNum portNum, bool wantAck = false);

  private:
    MeshService &service;
    ILogger &logger;
    Router *router;
};
