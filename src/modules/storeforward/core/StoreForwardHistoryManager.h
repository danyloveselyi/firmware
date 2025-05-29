#pragma once

#include "../interfaces/ILogger.h"
#include "../interfaces/IStoreForwardHistoryManager.h"
#include "../interfaces/ITimeProvider.h"
#include "NodeDB.h"
#include <unordered_map>
#include <unordered_set>
#include <vector>

// Forward declaration of the class before the namespace
class StoreForwardHistoryManager;

// Forward declaration of the persistence namespace functions
namespace StoreForwardPersistence
{
void saveToFlash(StoreForwardHistoryManager *manager);
void loadFromFlash(StoreForwardHistoryManager *manager);
} // namespace StoreForwardPersistence

class StoreForwardHistoryManager : public IStoreForwardHistoryManager
{
  public:
    explicit StoreForwardHistoryManager(ILogger &logger);

    // Implementation of IStoreForwardHistoryManager methods
    bool shouldStore(const meshtastic_MeshPacket &packet) const override;
    bool isDuplicate(const meshtastic_MeshPacket &packet) const override;
    void record(const meshtastic_MeshPacket &packet) override;
    std::vector<meshtastic_MeshPacket> getMessagesForNode(NodeNum dest, uint32_t sinceTime) const override;
    uint32_t getNumAvailablePackets(NodeNum dest, uint32_t lastTime) const override;
    void updateLastRequest(NodeNum dest, uint32_t index) override;
    uint32_t getLastRequestIndex(NodeNum dest) const override;
    uint32_t getTotalMessageCount() const override;
    uint32_t getMaxRecords() const override;
    void setMaxRecords(uint32_t maxRecords) override;
    const std::vector<meshtastic_MeshPacket> &getAllStoredMessages() const override;
    void clearStorage() override;
    std::string getStatisticsJson() const override;
    void saveToFlash() override;
    void loadFromFlash() override;

    // Additional methods specific to this implementation
    uint32_t getPacketId(const meshtastic_MeshPacket &packet) const;

  private:
    ILogger &logger;
    std::vector<meshtastic_MeshPacket> storedMessages;
    std::unordered_set<uint32_t> seenMessages;
    std::unordered_map<NodeNum, uint32_t> lastRequest;
    uint32_t maxRecords = 3000;

    // Constants
    static constexpr uint32_t SAVE_INTERVAL_MESSAGES = 10; // Save after this many new messages

    // Grant access to persistence functions
    friend void StoreForwardPersistence::saveToFlash(StoreForwardHistoryManager *manager);
    friend void StoreForwardPersistence::loadFromFlash(StoreForwardHistoryManager *manager);
};
