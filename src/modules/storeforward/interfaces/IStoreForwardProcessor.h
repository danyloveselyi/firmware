#pragma once

#include "NodeDB.h"
#include "mesh/generated/meshtastic/mesh.pb.h"
#include <string>
#include <vector>

/**
 * Interface for basic Store & Forward packet processing
 * This can be used as a simpler alternative to IStoreForwardHistoryManager
 * when full history management isn't needed.
 */
class IStoreForwardProcessor
{
  public:
    virtual ~IStoreForwardProcessor() = default;

    // Core message handling
    virtual bool shouldStore(const meshtastic_MeshPacket &packet) const = 0;
    virtual bool isDuplicate(const meshtastic_MeshPacket &packet) const = 0;
    virtual void record(const meshtastic_MeshPacket &packet) = 0;

    // Message retrieval
    virtual std::vector<meshtastic_MeshPacket> getMessagesForNode(NodeNum dest, uint32_t sinceTime) const = 0;
    virtual uint32_t getNumAvailablePackets(NodeNum dest, uint32_t lastTime) const = 0;

    // Request tracking
    virtual void updateLastRequest(NodeNum dest, uint32_t index) = 0;
    virtual uint32_t getLastRequestIndex(NodeNum dest) const = 0;

    // Statistics and management
    virtual uint32_t getTotalMessageCount() const = 0;
    virtual uint32_t getMaxRecords() const = 0; // Add missing method
    virtual const std::vector<meshtastic_MeshPacket> &getAllStoredMessages() const = 0;
    virtual void clearStorage() = 0;
    virtual std::string getStatisticsJson() const = 0;

    // Persistence
    virtual void saveToFlash() = 0;
    virtual void loadFromFlash() = 0;
};
