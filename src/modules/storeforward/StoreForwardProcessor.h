#pragma once

#include "NodeDB.h"
#include "RTC.h" // Include for getTime()
#include "interfaces/ILogger.h"
#include "interfaces/IStorageBackend.h"
#include "interfaces/IStoreForwardHistoryManager.h"
#include "interfaces/ITimeProvider.h"
#include <cstdint>
#include <unordered_map>
#include <unordered_set>
#include <vector>

// Forward declare class before namespace
class StoreForwardProcessor;

// Forward declaration for StoreForwardPersistence namespace
namespace StoreForwardPersistence
{
void saveToFlash(StoreForwardProcessor *processor);
void loadFromFlash(StoreForwardProcessor *processor);
} // namespace StoreForwardPersistence

class StoreForwardProcessor : public IStoreForwardHistoryManager
{
  public:
    /**
     * Constructor with dependency injection
     * @param storageBackend The storage backend to use
     * @param timeProvider The time provider to use
     * @param logger The logger to use
     */
    StoreForwardProcessor(IStorageBackend &storageBackend, ITimeProvider &timeProvider, ILogger &logger);

    // IStoreForwardHistoryManager interface implementation
    bool shouldStore(const meshtastic_MeshPacket &packet) const override;
    bool isDuplicate(const meshtastic_MeshPacket &packet) const override;
    void record(const meshtastic_MeshPacket &packet) override;
    std::vector<meshtastic_MeshPacket> getMessagesForNode(NodeNum dest, uint32_t sinceTime) const override;
    uint32_t getNumAvailablePackets(NodeNum dest, uint32_t lastTime) const override;
    void updateLastRequest(NodeNum dest, uint32_t index) override;
    uint32_t getLastRequestIndex(NodeNum dest) const override;
    uint32_t getTotalMessageCount() const override;
    uint32_t getMaxRecords() const override;
    const std::vector<meshtastic_MeshPacket> &getAllStoredMessages() const override;
    void clearStorage() override;
    std::string getStatisticsJson() const override;
    void saveToFlash() override;
    void loadFromFlash() override;

  private:
    IStorageBackend &storageBackend;
    ITimeProvider &timeProvider;
    ILogger &logger;

    // Storage for messages and tracking info
    std::vector<meshtastic_MeshPacket> storedMessages;
    mutable std::unordered_set<uint32_t> seenMessages;
    std::unordered_map<NodeNum, uint32_t> lastRequest;
    uint32_t maxRecords = 3000; // Default capacity

    // Constants
    static constexpr uint32_t SAVE_INTERVAL_MESSAGES = 10; // Save after this many new messages

    // Helper for detailed logging
    void logMessageDetails(const meshtastic_MeshPacket &packet) const;

    // Internal helper methods
    uint32_t getPacketId(const meshtastic_MeshPacket &packet) const;

    // Grant access to StoreForwardPersistence functions
    friend void StoreForwardPersistence::saveToFlash(StoreForwardProcessor *processor);
    friend void StoreForwardPersistence::loadFromFlash(StoreForwardProcessor *processor);
};
