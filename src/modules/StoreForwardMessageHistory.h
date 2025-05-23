#pragma once

#include <cstddef>
#include <stdint.h>
#include <unordered_set>
#include <vector>

#if defined(ESP32)
#include <Preferences.h>
#endif

namespace meshtastic
{

/**
 * @brief Class for tracking received message IDs in the Store & Forward module
 *
 * This class manages saving and loading of received message history for the client.
 * It allows the client to remember which messages have already been received to avoid duplicates.
 */
class StoreForwardMessageHistory
{
  public:
    /**
     * Initializes the message history manager
     */
    StoreForwardMessageHistory();

    /**
     * Saves the current message history to flash memory
     */
    void saveToFlash();

    /**
     * Loads the message history from flash memory
     */
    void loadFromFlash();

    /**
     * Checks if a message was already received
     *
     * @param messageId Message ID to check
     * @return true if the message has already been received
     */
    bool hasMessageBeenReceived(uint32_t messageId) const;

    /**
     * Records a received message in the history
     *
     * @param messageId Message ID to record
     */
    void recordReceivedMessage(uint32_t messageId);

    /**
     * Gets the latest N message IDs to send to the server
     *
     * @param maxIds Maximum number of IDs to return
     * @return Vector with message IDs
     */
    std::vector<uint32_t> getRecentMessageIds(std::size_t maxIds) const;

    /**
     * Checks if there have been changes in the history since the last save
     */
    bool hasChanges() const { return changed; }

    /**
     * Checks if there are any message IDs stored in history
     *
     * @return true if there are message IDs in history
     */
    bool hasMessageIds() const { return !receivedMessageIds.empty(); }

    /**
     * Gets the highest known message ID
     *
     * @return The highest message ID known to this client, or 0 if none
     */
    uint32_t getHighestKnownId() const
    {
        if (receivedMessageIds.empty()) {
            return 0;
        }

        // Find the highest ID
        uint32_t highest = 0;
        for (uint32_t id : receivedMessageIds) {
            if (id > highest) {
                highest = id;
            }
        }
        return highest;
    }

  private:
    std::unordered_set<uint32_t> receivedMessageIds;
    bool changed = false;

#if defined(ESP32)
    Preferences preferences;
#endif
};

} // namespace meshtastic
