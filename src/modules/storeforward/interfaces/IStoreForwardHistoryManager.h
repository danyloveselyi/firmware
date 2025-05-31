#pragma once

#include "MeshTypes.h"
#include <cstdint>
#include <string>
#include <vector>

/**
 * Interface for store & forward history management
 */
class IStoreForwardHistoryManager
{
  public:
    virtual ~IStoreForwardHistoryManager() = default;

    // Message handling
    virtual bool shouldStore(const meshtastic_MeshPacket &packet) const = 0;
    virtual bool isDuplicate(const meshtastic_MeshPacket &packet) const = 0;
    virtual void record(const meshtastic_MeshPacket &packet) = 0;

    // Query methods
    virtual std::vector<meshtastic_MeshPacket> getMessagesForNode(NodeNum dest, uint32_t sinceTime) const = 0;
    virtual uint32_t getNumAvailablePackets(NodeNum dest, uint32_t lastTime) const = 0;
    virtual const std::vector<meshtastic_MeshPacket> &getAllStoredMessages() const = 0;

    // Tracking
    virtual void updateLastRequest(NodeNum dest, uint32_t index) = 0;
    virtual uint32_t getLastRequestIndex(NodeNum dest) const = 0;

    // Storage management
    virtual uint32_t getTotalMessageCount() const = 0;
    virtual uint32_t getMaxRecords() const = 0;
    virtual void setMaxRecords(uint32_t maxRecords) = 0; // Add the missing method
    virtual void clearStorage() = 0;

    // Statistics
    virtual std::string getStatisticsJson() const = 0;

    // Persistence
    virtual void saveToFlash() = 0;
    virtual void loadFromFlash() = 0;
};
