#pragma once

#include "../interfaces/ILogger.h"
#include "../interfaces/IStoreForwardHistoryManager.h"
#include <cstdint>
#include <unordered_map>
#include <unordered_set>
#include <vector>

/**
 * Structure for storing message history
 */
struct PacketHistoryStruct {
    NodeNum from;
    NodeNum to;
    uint32_t time;
    uint8_t payload[meshtastic_Constants_DATA_PAYLOAD_LEN];
    uint8_t payload_size;
};

/**
 * Implementation of the Store & Forward history manager
 * Manages message storage and tracking
 */
class StoreForwardHistoryManager : public IStoreForwardHistoryManager
{
  public:
    explicit StoreForwardHistoryManager(ILogger &logger);
    ~StoreForwardHistoryManager();

    // IStoreForwardHistoryManager interface implementation
    bool shouldStore(const meshtastic_MeshPacket &packet) const override;
    void record(const meshtastic_MeshPacket &packet) override;
    std::vector<meshtastic_MeshPacket> getMessagesForNode(NodeNum dest, uint32_t sinceTime) const override;
    uint32_t getNumAvailablePackets(NodeNum dest, uint32_t lastTime) const override;
    void updateLastRequest(NodeNum dest, uint32_t index) override;
    uint32_t getLastRequestIndex(NodeNum dest) const override;
    uint32_t getTotalMessageCount() const override;
    uint32_t getMaxRecords() const override;
    void clearStorage() override;
    void saveToFlash() override;
    void loadFromFlash() override;

    // Implement missing virtual methods from the interface
    void setMaxRecords(uint32_t maxRecords) override { records = maxRecords; }
    const std::vector<meshtastic_MeshPacket> &getAllStoredMessages() const override { return storedMessages; }
    std::string getStatisticsJson() const override;

    /**
     * Enable or disable immediate flushing to persistent storage
     * When enabled, each message is immediately written to storage
     * @param value True to enable immediate flushing, false to use buffered storage
     */
    void setFlushImmediately(bool value) { flushImmediately = value; }

    /**
     * Check if immediate flushing is enabled
     * @return True if immediate flushing is enabled
     */
    bool isFlushImmediately() const { return flushImmediately; }

    // Direct access for persistence layer
    PacketHistoryStruct *getPacketHistory() { return packetHistory; }
    uint32_t getPacketHistoryTotalCount() const { return packetHistoryTotalCount; }
    const std::unordered_map<NodeNum, uint32_t> &getLastRequestMap() const { return lastRequest; }

  private:
    ILogger &logger;

    // Storage for messages and tracking info
    PacketHistoryStruct *packetHistory = nullptr;
    uint32_t packetHistoryTotalCount = 0;
    uint32_t records = 3000; // Default capacity
    std::unordered_map<NodeNum, uint32_t> lastRequest;
    std::vector<meshtastic_MeshPacket> storedMessages; // For storing decoded messages

    // Flag to control immediate flushing behavior
    bool flushImmediately = false;

    // Helper methods
    bool isDuplicate(const meshtastic_MeshPacket &packet) const;
    uint32_t getPacketId(const meshtastic_MeshPacket &packet) const;
    void initStorage();
};
