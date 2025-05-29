#pragma once

#include "NodeDB.h"
#include "mesh/generated/meshtastic/mesh.pb.h"
#include <string>
#include <vector>

/**
 * Interface for Store & Forward history management
 * This abstracts the storage and retrieval of message history for the S&F module
 */
class IStoreForwardHistoryManager
{
  public:
    virtual ~IStoreForwardHistoryManager() = default;

    /**
     * Determines if a packet should be stored in history
     * @param packet The packet to check
     * @return true if the packet should be stored
     */
    virtual bool shouldStore(const meshtastic_MeshPacket &packet) const = 0;

    /**
     * Check if the packet is a duplicate of one we've already seen
     * @param packet The packet to check
     * @return true if it's a duplicate
     */
    virtual bool isDuplicate(const meshtastic_MeshPacket &packet) const = 0;

    /**
     * Record a packet in the history
     * @param packet The packet to record
     */
    virtual void record(const meshtastic_MeshPacket &packet) = 0;

    /**
     * Get all stored messages for a specific node since a specific time
     * @param dest The destination node ID
     * @param sinceTime Only include messages newer than this time (unix timestamp)
     * @return Vector of matching messages
     */
    virtual std::vector<meshtastic_MeshPacket> getMessagesForNode(NodeNum dest, uint32_t sinceTime) const = 0;

    /**
     * Count how many packets are available for a node
     * @param dest The node to check
     * @param lastTime Only include messages newer than this time
     * @return Number of available packets
     */
    virtual uint32_t getNumAvailablePackets(NodeNum dest, uint32_t lastTime) const = 0;

    /**
     * Update the record of what messages we've already sent to a node
     * @param dest The node ID
     * @param index The new index position
     */
    virtual void updateLastRequest(NodeNum dest, uint32_t index) = 0;

    /**
     * Get the last request index for a node
     * @param dest The node ID
     * @return The index
     */
    virtual uint32_t getLastRequestIndex(NodeNum dest) const = 0;

    /**
     * Get total number of messages in history
     * @return The count
     */
    virtual uint32_t getTotalMessageCount() const = 0;

    /**
     * Get maximum number of records that can be stored
     * @return The maximum
     */
    virtual uint32_t getMaxRecords() const = 0;

    /**
     * Set maximum number of records that can be stored
     * @param maxRecords The new maximum
     */
    virtual void setMaxRecords(uint32_t maxRecords) = 0;

    /**
     * Get all stored messages
     * @return Reference to the vector of all stored messages
     */
    virtual const std::vector<meshtastic_MeshPacket> &getAllStoredMessages() const = 0;

    /**
     * Clear all stored messages and tracking data
     */
    virtual void clearStorage() = 0;

    /**
     * Get statistics in JSON format
     * @return JSON string with statistics
     */
    virtual std::string getStatisticsJson() const = 0;

    /**
     * Save history to persistent storage
     */
    virtual void saveToFlash() = 0;

    /**
     * Load history from persistent storage
     */
    virtual void loadFromFlash() = 0;
};
