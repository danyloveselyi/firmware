#pragma once

#include "NodeDB.h" // Include for NodeNum type
#include "mesh/generated/meshtastic/mesh.pb.h"
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

/**
 * Interface for persistent storage operations
 * Abstracts the underlying storage mechanism (filesystem, PSRAM, EEPROM, etc.)
 */
class IStorageBackend
{
  public:
    virtual ~IStorageBackend() = default;

    /**
     * Save messages to persistent storage
     * @param messages The messages to save
     * @return true if successful
     */
    virtual bool saveMessages(const std::vector<meshtastic_MeshPacket> &messages) = 0;

    /**
     * Load messages from persistent storage
     * @return The loaded messages
     */
    virtual std::vector<meshtastic_MeshPacket> loadMessages() = 0;

    /**
     * Save user request history to persistent storage
     * @param lastRequests Map of node IDs to their last request indices
     * @return true if successful
     */
    virtual bool saveRequestHistory(const std::unordered_map<NodeNum, uint32_t> &lastRequests) = 0;

    /**
     * Load user request history from persistent storage
     * @return Map of node IDs to their last request indices
     */
    virtual std::unordered_map<NodeNum, uint32_t> loadRequestHistory() = 0;

    /**
     * Check if storage is available
     * @return true if storage is available
     */
    virtual bool isAvailable() const = 0;
};
