#pragma once

#include "NodeDB.h"
#include "RTC.h"
#include "interfaces/ILogger.h"
#include "interfaces/IStoreForwardHistoryManager.h"
#include "mesh/generated/meshtastic/mesh.pb.h"
#include <cstdint>
#include <unordered_map>
#include <unordered_set>
#include <vector>

// Forward declaration for StoreForwardHistoryManager
class StoreForwardHistoryManager;

// Forward declare the namespace functions that need to be friends
namespace StoreForwardPersistence
{
void saveToFlash(StoreForwardHistoryManager *manager);
void loadFromFlash(StoreForwardHistoryManager *manager);
void saveToFlash(IStoreForwardHistoryManager *manager);
void loadFromFlash(IStoreForwardHistoryManager *manager);
} // namespace StoreForwardPersistence

/**
 * Implementation of IStoreForwardHistoryManager responsible for storing and managing
 * message history for the Store & Forward module.
 */
class StoreForwardHistoryManager : public IStoreForwardHistoryManager
{
  public:
    /**
     * Constructor
     * @param logger The logger to use for logging
     */
    explicit StoreForwardHistoryManager(ILogger &logger);

    // Core history management
    bool shouldStore(const meshtastic_MeshPacket &packet) const override;
    bool isDuplicate(const meshtastic_MeshPacket &packet) const override;
    void record(const meshtastic_MeshPacket &packet) override;
    void clearStorage() override;

    // Message retrieval functions
    std::vector<meshtastic_MeshPacket> getMessagesForNode(NodeNum dest, uint32_t sinceTime) const override;
    uint32_t getNumAvailablePackets(NodeNum dest, uint32_t lastTime) const override;
    const std::vector<meshtastic_MeshPacket> &getAllStoredMessages() const override;

    // Client request tracking
    void updateLastRequest(NodeNum dest, uint32_t index) override;
    uint32_t getLastRequestIndex(NodeNum dest) const override;

    // Statistics and configuration
    uint32_t getTotalMessageCount() const override;
    uint32_t getMaxRecords() const override;
    void setMaxRecords(uint32_t maxRecords) override;
    std::string getStatisticsJson() const override;

    // Persistence operations
    void saveToFlash() override;
    void loadFromFlash() override;

  private:
    ILogger &logger;

    // Constants
    static constexpr uint32_t DEFAULT_MAX_RECORDS = 3000;
    static constexpr uint32_t SAVE_INTERVAL_MESSAGES = 10; // Save after this many new messages

    // Storage for messages and tracking info
    std::vector<meshtastic_MeshPacket> storedMessages;
    mutable std::unordered_set<uint32_t> seenMessages;
    std::unordered_map<NodeNum, uint32_t> lastRequest;
    uint32_t maxRecords = DEFAULT_MAX_RECORDS;

    // Helper methods
    uint32_t getPacketId(const meshtastic_MeshPacket &packet) const;

    // Give the persistence functions access to our private members
    friend void StoreForwardPersistence::saveToFlash(StoreForwardHistoryManager *manager);
    friend void StoreForwardPersistence::loadFromFlash(StoreForwardHistoryManager *manager);
    friend void StoreForwardPersistence::saveToFlash(IStoreForwardHistoryManager *manager);
    friend void StoreForwardPersistence::loadFromFlash(IStoreForwardHistoryManager *manager);
};
