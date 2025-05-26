#pragma once

#include "NodeDB.h"
#include "ProtobufModule.h"
#include "concurrency/OSThread.h"
#include "configuration.h"
#include "mesh/Channels.h"
#include "mesh/generated/meshtastic/storeforward.pb.h"
#include <Arduino.h>
#include <functional>
#include <unordered_map>

class StoreForwardModule;

namespace StoreForwardPersistence
{
void saveToFlash(StoreForwardModule *module);
void loadFromFlash(StoreForwardModule *module);
} // namespace StoreForwardPersistence

struct PacketHistoryStruct {
    uint32_t time;
    uint32_t to;
    uint32_t from;
    uint32_t id;
    uint8_t channel;
    uint32_t reply_id;
    bool emoji;
    uint8_t payload[meshtastic_Constants_DATA_PAYLOAD_LEN];
    pb_size_t payload_size;
};

class StoreForwardModule : public concurrency::OSThread, public ProtobufModule<meshtastic_StoreAndForward>
{
  private:
    bool busy = false;
    uint32_t busyTo = 0;
    char routerMessage[meshtastic_Constants_DATA_PAYLOAD_LEN] = {0};

    PacketHistoryStruct *packetHistory = nullptr;
    uint32_t packetHistoryTotalCount = 0;
    uint32_t last_time = 0;
    uint32_t requestCount = 0;

    uint32_t packetTimeMax = 5000;

    bool is_client = false;
    bool is_server = false;

    std::unordered_map<NodeNum, uint32_t> lastRequest;
    std::unordered_map<NodeNum, uint8_t> clientChannels;

    friend void StoreForwardPersistence::saveToFlash(StoreForwardModule *module);
    friend void StoreForwardPersistence::loadFromFlash(StoreForwardModule *module);

    bool waitingForAck = false;
    uint32_t lastMessageId = 0;
    uint8_t messageRetryCount = 0;
    uint8_t maxRetryCount = 3;
    uint32_t lastSendTime = 0;
    uint32_t retryTimeoutMs = 5000;
    bool ignoreRequest = false;

    uint32_t historyReturnMax = 25;
    uint32_t historyReturnWindow = 240;
    uint32_t records = 0;
    bool heartbeat = false;

    uint32_t requests = 0;
    uint32_t requests_history = 0;
    uint32_t retry_delay = 0;

    uint8_t findBestChannelForNode(NodeNum nodeNum);
    void populatePSRAM();
    void configureModuleSettings();

    void logStatusPeriodically();
    void handleRetries();
    void tryTransmitMessageQueue();
    void sendHeartbeatIfNeeded();
    void trySendPendingResetConfirmation();
    void trySendPendingResetNotification();
    void trySendPendingNoMessages();
    void sendTextNotification(NodeNum target, const char *message);
    const char *getClientName(meshtastic_NodeInfoLite *node);
    void checkPendingNotifications();

  protected:
    int32_t runOnce() override;
    virtual ProcessMessage handleReceived(const meshtastic_MeshPacket &mp) override;
    virtual bool handleReceivedProtobuf(const meshtastic_MeshPacket &mp, meshtastic_StoreAndForward *p) override;

  public:
    StoreForwardModule();
    ~StoreForwardModule();

    unsigned long lastHeartbeat = 0;
    uint32_t heartbeatInterval = 900;

    void historyAdd(const meshtastic_MeshPacket &mp);
    void statsSend(uint32_t to);
    void historySend(uint32_t secAgo, uint32_t to);
    uint32_t getNumAvailablePackets(NodeNum dest, uint32_t last_time);

    bool sendPayload(NodeNum dest = NODENUM_BROADCAST, uint32_t packetHistory_index = 0, bool isRetry = false);
    meshtastic_MeshPacket *preparePayload(NodeNum dest, uint32_t last_time, bool local = false, bool isRetry = false);
    void sendMessage(NodeNum dest, const meshtastic_StoreAndForward &payload);
    void sendMessage(NodeNum dest, meshtastic_StoreAndForward_RequestResponse rr);
    void sendErrorTextMessage(NodeNum dest, bool want_response);
    meshtastic_MeshPacket *getForPhone();

    bool isServer() { return is_server; }

    virtual bool wantPacket(const meshtastic_MeshPacket *p) override
    {
        if (is_server && waitingForAck) {
            if (p->to == nodeDB->getNodeNum() && p->decoded.portnum == meshtastic_PortNum_ROUTING_APP) {
                return true;
            }
        }
        switch (p->decoded.portnum) {
        case meshtastic_PortNum_TEXT_MESSAGE_APP:
        case meshtastic_PortNum_STORE_FORWARD_APP:
            return true;
        default:
            return false;
        }
    }

    void resetClientHistoryPosition(NodeNum clientNodeNum);
};

extern StoreForwardModule *storeForwardModule;
