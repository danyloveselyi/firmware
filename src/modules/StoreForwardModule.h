#pragma once

#include "ProtobufModule.h"
#include "concurrency/OSThread.h"
#include "mesh/generated/meshtastic/storeforward.pb.h"

#include "configuration.h"
#include <Arduino.h>
#include <functional>
#include <unordered_map>

// Forward declare StoreForwardModule class first
class StoreForwardModule;

// Now declare the StoreForwardPersistence namespace
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

class StoreForwardModule : public ProtobufModule<meshtastic_StoreAndForward>, private concurrency::OSThread
{
    bool busy = 0;
    uint32_t busyTo = 0;
    char routerMessage[meshtastic_Constants_DATA_PAYLOAD_LEN] = {0};

    PacketHistoryStruct *packetHistory = 0;
    uint32_t packetHistoryTotalCount = 0;
    uint32_t last_time = 0;
    uint32_t requestCount = 0;

    uint32_t packetTimeMax = 5000; // Interval between sending history packets as a server.

    bool is_client = false;
    bool is_server = false;

    // Unordered_map stores the last request for each nodeNum (`to` field)
    std::unordered_map<NodeNum, uint32_t> lastRequest;

    // Adding Friendships to Access Private Class Members
    friend void StoreForwardPersistence::saveToFlash(StoreForwardModule *module);
    friend void StoreForwardPersistence::loadFromFlash(StoreForwardModule *module);
    // Removed the friend declaration for checkSaveInterval

    // Message acknowledgment tracking
    bool waitingForAck = false;     // Flag indicating we're waiting for an ACK
    uint32_t lastMessageId = 0;     // ID of the last sent message for tracking
    uint8_t messageRetryCount = 0;  // Current retry attempt count
    uint8_t maxRetryCount = 3;      // Maximum number of retries before giving up
    uint32_t lastSendTime = 0;      // When the last message was sent
    uint32_t retryTimeoutMs = 5000; // Retry timeout in milliseconds (5 seconds)
    bool ignoreRequest = false;     // Flag to ignore incoming requests while busy

  public:
    StoreForwardModule();
    // Adding a destructor
    ~StoreForwardModule();

    unsigned long lastHeartbeat = 0;
    uint32_t heartbeatInterval = 900;

    /**
     Update our local reference of when we last saw that node.
     @return 0 if we have never seen that node before otherwise return the last time we saw the node.
     */
    void historyAdd(const meshtastic_MeshPacket &mp);
    void statsSend(uint32_t to);
    void historySend(uint32_t secAgo, uint32_t to);
    uint32_t getNumAvailablePackets(NodeNum dest, uint32_t last_time);

    /**
     * Send our payload into the mesh
     */
    bool sendPayload(NodeNum dest = NODENUM_BROADCAST, uint32_t packetHistory_index = 0, bool isRetry = false);
    meshtastic_MeshPacket *preparePayload(NodeNum dest, uint32_t packetHistory_index, bool local = false, bool isRetry = false);
    void sendMessage(NodeNum dest, const meshtastic_StoreAndForward &payload);
    void sendMessage(NodeNum dest, meshtastic_StoreAndForward_RequestResponse rr);
    void sendErrorTextMessage(NodeNum dest, bool want_response);
    meshtastic_MeshPacket *getForPhone();
    // Returns true if we are configured as server AND we could allocate PSRAM.
    bool isServer() { return is_server; }

    /*
      -Override the wantPacket method.
    */
    virtual bool wantPacket(const meshtastic_MeshPacket *p) override
    {
        if (is_server && waitingForAck) {
            // When waiting for ACKs, also check for routing packets (ACKs)
            if (p->to == nodeDB->getNodeNum() && p->decoded.portnum == meshtastic_PortNum_ROUTING_APP) {
                return true;
            }
        }

        // Check regular message types
        switch (p->decoded.portnum) {
        case meshtastic_PortNum_TEXT_MESSAGE_APP:
        case meshtastic_PortNum_STORE_FORWARD_APP:
            return true;
        default:
            return false;
        }
    }

  private:
    void populatePSRAM();

    // S&F Defaults
    uint32_t historyReturnMax = 25;     // Return maximum of 25 records by default.
    uint32_t historyReturnWindow = 240; // Return history of last 4 hours by default.
    uint32_t records = 0;               // Calculated
    bool heartbeat = false;             // No heartbeat.

    // stats
    uint32_t requests = 0;         // Number of times any client sent a request to the S&F.
    uint32_t requests_history = 0; // Number of times the history was requested.

    uint32_t retry_delay = 0; // If server is busy, retry after this delay (in ms).

  protected:
    virtual int32_t runOnce() override;

    /** Called to handle a particular incoming message

    @return ProcessMessage::STOP if you've guaranteed you've handled this message and no other handlers should be considered for
    it
    */
    virtual ProcessMessage handleReceived(const meshtastic_MeshPacket &mp) override;
    virtual bool handleReceivedProtobuf(const meshtastic_MeshPacket &mp, meshtastic_StoreAndForward *p);

  public:
    /**
     * Resets the history position for a specific client.
     *
     * @param clientNodeNum The node number of the client to reset.
     */
    void resetClientHistoryPosition(NodeNum clientNodeNum);
};

extern StoreForwardModule *storeForwardModule;