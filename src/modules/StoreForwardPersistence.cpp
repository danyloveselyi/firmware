#include "modules/StoreForwardPersistence.h"
#include "FSCommon.h"
#include "SafeFile.h"
#include "configuration.h"

// Names of files for storing Store & Forward data
#define STORE_FORWARD_FILENAME "/store_forward.bin"    // Main file for message history
#define STORE_FORWARD_MSGIDS_FILENAME "/sf_msgids.bin" // File for storing known message IDs

/**
 * Save the Store & Forward message history to flash
 * @param module Pointer to the module whose history should be saved
 * @return true if successful, false otherwise
 */
bool StoreForwardPersistence::saveToFlash(StoreForwardModule *module)
{
    // Check for empty data - don't save if there are no messages
    if (!module || module->packetHistoryTotalCount == 0) {
        LOG_DEBUG("S&F: No messages to save to flash");
        return true;
    }

    LOG_INFO("S&F: Saving %u messages to flash", module->packetHistoryTotalCount);

    // Open file with safe write (prevents corruption during power failure)
    SafeFile file(STORE_FORWARD_FILENAME);
    if (!file.isOpen()) {
        LOG_ERROR("S&F: Failed to open file for writing");
        return false;
    }

    // Write header (version and record count)
    uint8_t version = 1;
    if (!file.write(version)) {
        LOG_ERROR("S&F: Error writing file version");
        file.close();
        return false;
    }

    if (!file.write((uint8_t *)&module->packetHistoryTotalCount, sizeof(module->packetHistoryTotalCount))) {
        LOG_ERROR("S&F: Error writing message count");
        file.close();
        return false;
    }

    // Write each message record
    for (uint32_t i = 0; i < module->packetHistoryTotalCount; i++) {
        if (!file.write((uint8_t *)&module->packetHistory[i], sizeof(PacketHistoryStruct))) {
            LOG_ERROR("S&F: Error writing message %u", i);
            file.close();
            return false;
        }
    }

    // Save lastRequest map for resumable sending
    size_t entriesCount = module->lastRequest.size();
    if (!file.write((uint8_t *)&entriesCount, sizeof(entriesCount))) {
        LOG_ERROR("S&F: Error writing lastRequest map size");
        file.close();
        return false;
    }

    // Write each key-value pair from lastRequest map
    for (const auto &entry : module->lastRequest) {
        if (!file.write((uint8_t *)&entry.first, sizeof(entry.first)) ||
            !file.write((uint8_t *)&entry.second, sizeof(entry.second))) {
            LOG_ERROR("S&F: Error writing lastRequest entry");
            file.close();
            return false;
        }
    }

    // Close file with successful completion check
    if (!file.close()) {
        LOG_ERROR("S&F: Error closing message history file");
        return false;
    }

    LOG_INFO("S&F: Successfully saved message history to flash");
    return true;
}

/**
 * Load the Store & Forward message history from flash
 * @param module Pointer to the module whose history should be loaded
 * @return true if successful, false otherwise
 */
bool StoreForwardPersistence::loadFromFlash(StoreForwardModule *module)
{
    // Check file existence and module pointer validity
    if (!module) {
        LOG_ERROR("S&F: Invalid module pointer");
        return false;
    }

    if (!FSCom.exists(STORE_FORWARD_FILENAME)) {
        LOG_DEBUG("S&F: No saved message history found");
        return false;
    }

    // Open file for reading
    File file = FSCom.open(STORE_FORWARD_FILENAME, FILE_O_READ);
    if (!file) {
        LOG_ERROR("S&F: Failed to open message history file");
        return false;
    }

    // Check file format version
    uint8_t version = file.read();
    if (version != 1) {
        LOG_ERROR("S&F: Unsupported history file version: %d", version);
        file.close();
        return false;
    }

    // Read message count
    uint32_t count;
    if (file.read((uint8_t *)&count, sizeof(count)) != sizeof(count)) {
        LOG_ERROR("S&F: Error reading message count");
        file.close();
        return false;
    }

    // Safety check - don't exceed allocated memory
    if (count > module->records) {
        LOG_WARN("S&F: Saved history has %u messages, but only %u slots allocated", count, module->records);
        count = module->records; // Limit the number of messages to read
    }

    // Read all message records
    for (uint32_t i = 0; i < count; i++) {
        if (file.read((uint8_t *)&module->packetHistory[i], sizeof(PacketHistoryStruct)) != sizeof(PacketHistoryStruct)) {
            LOG_ERROR("S&F: Error reading message %u from file", i);
            file.close();
            module->packetHistoryTotalCount = i; // Save what we managed to read
            return false;
        }
    }

    // Update total message count
    module->packetHistoryTotalCount = count;

    // Try to read lastRequest map if present in the file
    // Fault tolerance: it's okay if this part fails
    size_t entriesCount = 0;
    if (file.read((uint8_t *)&entriesCount, sizeof(entriesCount)) == sizeof(entriesCount)) {
        module->lastRequest.clear();

        // Read each key-value pair
        for (size_t i = 0; i < entriesCount; i++) {
            NodeNum nodeId;
            uint32_t lastIdx;

            if (file.read((uint8_t *)&nodeId, sizeof(nodeId)) != sizeof(nodeId) ||
                file.read((uint8_t *)&lastIdx, sizeof(lastIdx)) != sizeof(lastIdx)) {
                LOG_WARN("S&F: Error reading lastRequest entry %u", (unsigned)i);
                break;
            }

            // Verify index is within valid range
            if (lastIdx <= module->packetHistoryTotalCount) {
                module->lastRequest[nodeId] = lastIdx;
            } else {
                // Reset index if out of valid range
                module->lastRequest[nodeId] = 0;
                LOG_WARN("S&F: lastRequest index out of range for node %u, reset to 0", nodeId);
            }
        }
        LOG_INFO("S&F: Loaded %u lastRequest entries", (unsigned)module->lastRequest.size());
    }

    file.close();

    LOG_INFO("S&F: Loaded %u messages from flash", count);
    return true;
}

/**
 * Remove the Store & Forward message history file
 * @return true if successful or if file doesn't exist, false otherwise
 */
bool StoreForwardPersistence::clearFlash()
{
    // Check if file exists before attempting deletion
    if (!FSCom.exists(STORE_FORWARD_FILENAME)) {
        LOG_DEBUG("S&F: History file already doesn't exist");
        return true; // File already doesn't exist, consider it a success
    }

    // Delete the file
    bool result = FSCom.remove(STORE_FORWARD_FILENAME);
    if (result) {
        LOG_INFO("S&F: Message history successfully deleted from flash");
    } else {
        LOG_ERROR("S&F: Failed to delete message history from flash");
    }

    // Also delete message IDs file if it exists
    if (FSCom.exists(STORE_FORWARD_MSGIDS_FILENAME)) {
        bool msgIdsResult = FSCom.remove(STORE_FORWARD_MSGIDS_FILENAME);
        if (msgIdsResult) {
            LOG_INFO("S&F: Message IDs file successfully deleted");
        } else {
            LOG_WARN("S&F: Failed to delete message IDs file");
        }
    }

    return result;
}