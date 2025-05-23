#pragma once

// Необходимые включения
#include "ProtobufModule.h"
#include "Router.h"
#include "concurrency/OSThread.h"
#include "configuration.h"
#include "mesh/generated/meshtastic/storeforward.pb.h" // Важно включить этот файл!
#include <functional>
#include <map>
#include <unordered_set>
#include <vector>

// Forward declarations
class StoreForwardPersistence;

/**
 * Structure to store message history
 */
class MessageHistory
{
  public:
    std::unordered_set<uint32_t> receivedMessageIds;
    uint32_t highestKnownId = 0;
    bool changed = false;

    void recordReceivedMessage(uint32_t messageId);
};

/**
 * Structure to store message history packets
 */
typedef struct {
    time_t time = 0;
    uint32_t to = 0;
    uint32_t from = 0;
    uint32_t id = 0;
    uint8_t channel = 0;
    uint32_t reply_id = 0;
    bool emoji = false;
    uint8_t payload_size = 0;
    uint8_t payload[meshtastic_Constants_DATA_PAYLOAD_LEN];
} PacketHistoryStruct;

/**
 * Store and Forward message handling for meshtastic
 */
class StoreForwardModule : public ProtobufModule<meshtastic_StoreAndForward>, public concurrency::OSThread
{
    uint8_t serverPageCount = 0; // The server sends this to clients for free

    // We use historyReturnWindow as a history window size in sendToMesh (below)
    // For now we assume a small window for all incoming messages (last 6 hours)
    uint32_t historyReturnWindow = 6 * 60 * 60;
    uint32_t historyReturnMax = 250; // Maximum number of packets to return 'at once'
    bool busy = false;               // don't start another request if we're already busy
    bool heartbeat = true;
    uint32_t requestCount = 0;    // How many messages have we already handled
    uint32_t packetTimeMax = 500; // Time between packets when there's no throttling
    uint32_t packetHistoryTotalCount = 0;
    uint32_t records = 0; // Number of available records
    uint32_t last_time = 0;
    uint32_t heartbeatInterval = 300;
    uint32_t lastHeartbeat = 0;
    NodeNum busyTo = 0;
    bool is_server = false;
    bool ignoreIncoming = false;

    // Map of specific missing messages for each client
    std::map<NodeNum, std::vector<uint32_t>> missingMessages;

  public:
    PacketHistoryStruct *packetHistory = nullptr;
    std::map<NodeNum, uint32_t> lastRequest;

    // Statistics counters
    uint32_t requestsReceived = 0; // Total number of SF requests received
    uint32_t historyRequests = 0;  // Number of history requests received
    uint32_t retryDelay = 0;       // Timestamp for retry after error/busy

    /** Constructor
     * name is for debugging output
     */
    StoreForwardModule();

    /**
     * Send a retransmission request for a missing message.
     * A router can respond to this request if available
     */
    void requestRouterRetransmission(NodeNum to, uint32_t messageId);

    /**
     * Send a store and forward router stats request to a specific node.
     * No request payload is needed.
     */
    void requestStatsFromRouter(NodeNum to);

    /**
     * Send a request to a store and forward router asking for messages
     * after the specified time. If seconds_back is 0, all known history is returned.
     */
    void historyRetrieve(uint32_t seconds_back = 0, uint32_t to = NODENUM_BROADCAST);

    /**
     * Stores a received packet in the history for later forwarding.
     * This is called by handleReceived()
     */
    void historyAdd(const meshtastic_MeshPacket &mp);

    /**
     * Sends messages from the message history to the specified recipient.
     */
    void historySend(uint32_t since_time, uint32_t to);

    /**
     * Get an incoming packet for phone or client to consume
     * This is only called by PhoneAPI.cpp
     */
    meshtastic_MeshPacket *getForPhone();

    // Handle a received message
    virtual ProcessMessage handleReceived(const meshtastic_MeshPacket &mp) override;

    /**
     * Gets module statistics for API consumption
     * Placeholder implementation to avoid undefined type issues
     */
    void getModuleStats(void *stats)
    {
        // Simplified implementation to avoid errors with undefined types
        // This will need proper implementation when the types are available
    }

    /**
     * @brief Set whether to ignore incoming messages
     *
     * @param ignore If true, ignore incoming messages
     */
    void setIgnoreIncoming(bool ignore);

  protected:
    friend class StoreForwardPersistence;

    /**
     * Return true if this packet is one we want to store/forward
     */
    virtual bool wantPacket(const meshtastic_MeshPacket *p) override
    {
        return p->decoded.portnum == meshtastic_PortNum_STORE_FORWARD_APP ||
               (p->decoded.portnum == meshtastic_PortNum_TEXT_MESSAGE_APP && is_server);
    }

    /**
     * @brief Handle a received protobuf message
     *
     * @param mp The received mesh packet
     * @param p The store and forward message
     * @return true if the message was handled
     */
    virtual bool handleReceivedProtobuf(const meshtastic_MeshPacket &mp, meshtastic_StoreAndForward *p) override;

    /**
     * Populates the PSRAM with data to be sent later when a device is out of range.
     */
    void populatePSRAM();

    /**
     * This implements the pure virtual function from MeshModule
     */
    virtual int32_t runOnce() override;

    /**
     * Returns the number of available packets in the message history for a specified destination node.
     */
    uint32_t getNumAvailablePackets(NodeNum dest, uint32_t last_time);

    /**
     * Send a Store and Forward message to the specified node.
     */
    void sendMessage(NodeNum dest, const meshtastic_StoreAndForward &payload);

    /**
     * Send a Store and Forward control message to the specified node.
     */
    void sendMessage(NodeNum dest, meshtastic_StoreAndForward_RequestResponse rr);

    /**
     * Sends a payload to a specified destination node using the store and forward mechanism.
     */
    bool sendPayload(NodeNum dest, uint32_t last_time);

    /**
     * Prepares a payload to be sent to a specified destination node from the S&F packet history.
     */
    meshtastic_MeshPacket *preparePayload(NodeNum dest, uint32_t last_time, bool local = false);
};

/**
 * Global instance of StoreForwardModule
 */
extern StoreForwardModule *storeForwardModule;