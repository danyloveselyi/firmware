#include "StoreForwardPersistence.h"
#include "../StoreForwardModule.h" // Updated path after file was moved
#include "../core/StoreForwardHistoryManager.h"
#include "../core/StoreForwardProcessor.h"
#include "FSCommon.h"
#include "NodeDB.h"
#include "configuration.h"
#include <algorithm>

namespace StoreForwardPersistence
{
// Make lastSaveTime and messageCounter static as they're only used within this namespace
static unsigned long lastSaveTime = 0;
static uint32_t messageCounter = 0;

// Helper function to print message content in readable format
void logMessageContent(const meshtastic_MeshPacket *msg, int index)
{
    if (!msg || !msg->which_payload_variant)
        return;

    // Get sender name from node DB if available
    meshtastic_NodeInfoLite *senderNode = nodeDB->getMeshNode(msg->from);
    const char *senderName = (senderNode && senderNode->has_user && senderNode->user.long_name[0]) ? senderNode->user.long_name
                             : (senderNode && senderNode->has_user && senderNode->user.short_name[0])
                                 ? senderNode->user.short_name
                                 : "Unknown";

    // Get recipient name if direct message
    const char *recipientName = "BROADCAST";
    if (msg->to != NODENUM_BROADCAST) {
        meshtastic_NodeInfoLite *recipientNode = nodeDB->getMeshNode(msg->to);
        recipientName =
            (recipientNode && recipientNode->has_user && recipientNode->user.long_name[0])    ? recipientNode->user.long_name
            : (recipientNode && recipientNode->has_user && recipientNode->user.short_name[0]) ? recipientNode->user.short_name
                                                                                              : "Unknown";
    }

    // Log message info
    LOG_INFO("S&F: Message %d - from: %s (0x%x), to: %s (0x%x), time: %u", index, senderName, msg->from, recipientName, msg->to,
             msg->rx_time);
}

void saveToFlash(StoreForwardModule *module)
{
    // Update lastSaveTime when saving
    lastSaveTime = millis();

    if (module && module->getHistoryManager()) {
        // Try to downcast to concrete class if possible, otherwise use interface version
        StoreForwardHistoryManager *concreteManager = dynamic_cast<StoreForwardHistoryManager *>(module->getHistoryManager());

        if (concreteManager) {
            saveToFlash(concreteManager);
        } else {
            saveToFlash(module->getHistoryManager());
        }
    } else {
        LOG_INFO("S&F: No messages to save or module not initialized");
    }
}

void loadFromFlash(StoreForwardModule *module)
{
    LOG_INFO("S&F: Attempting to load messages from flash");

    if (module && module->getHistoryManager()) {
        // Try to downcast to concrete class if possible, otherwise use interface version
        StoreForwardHistoryManager *concreteManager = dynamic_cast<StoreForwardHistoryManager *>(module->getHistoryManager());

        if (concreteManager) {
            loadFromFlash(concreteManager);
        } else {
            loadFromFlash(module->getHistoryManager());
        }
    } else {
        LOG_WARN("S&F: Module not initialized, skipping history load");
    }
}

// Implementation for the concrete StoreForwardHistoryManager class
void saveToFlash(StoreForwardHistoryManager *manager)
{
    if (!manager) {
        LOG_WARN("S&F: Cannot save history - manager is null");
        return;
    }

    LOG_INFO("S&F: Saving history manager state to flash - %u messages", manager->getTotalMessageCount());

#ifdef FSCom
    LOG_INFO("S&F: Creating directory /history if needed");
    FSCom.mkdir("/history");

    LOG_INFO("S&F: Opening file /history/sf for writing");
    File storeAndForward = FSCom.open("/history/sf", FILE_O_WRITE);
    if (storeAndForward) {
        // Get the stored messages vector
        const auto &messages = manager->storedMessages;
        uint32_t totalSize = sizeof(meshtastic_MeshPacket) * messages.size();

        LOG_INFO("S&F: Writing %u bytes to flash (%u messages)", totalSize, (unsigned)messages.size());
        if (messages.size() > 0) {
            uint32_t written = storeAndForward.write((uint8_t *)messages.data(), totalSize);
            if (written == totalSize) {
                LOG_INFO("S&F: Successfully stored %u messages to flash", (unsigned)messages.size());
            } else {
                LOG_ERROR("S&F: Error writing messages to flash: %u of %u bytes written", written, totalSize);
            }
        }
        storeAndForward.close();

        // Save the lastRequest map to track what each user has already received
        LOG_INFO("S&F: Saving user request history");
        File userRequestsFile = FSCom.open("/history/sf_users", FILE_O_WRITE);
        if (userRequestsFile) {
            // Write number of entries first
            size_t entriesCount = manager->lastRequest.size();
            userRequestsFile.write((uint8_t *)&entriesCount, sizeof(entriesCount));

            // Write each user's last request position
            for (const auto &entry : manager->lastRequest) {
                userRequestsFile.write((uint8_t *)&entry.first, sizeof(entry.first));   // NodeNum
                userRequestsFile.write((uint8_t *)&entry.second, sizeof(entry.second)); // lastRequest index
            }
            userRequestsFile.close();
        }
    } else {
        LOG_ERROR("S&F: Could not open history file for writing");
    }
#else
    LOG_WARN("S&F: Filesystem not implemented, can't save messages");
#endif
}

// Implementation for the concrete StoreForwardHistoryManager class
void loadFromFlash(StoreForwardHistoryManager *manager)
{
    if (!manager) {
        LOG_WARN("S&F: Cannot load history - manager is null");
        return;
    }

#ifdef FSCom
    LOG_INFO("S&F: Checking if history file exists");
    if (FSCom.exists("/history/sf")) {
        LOG_INFO("S&F: Opening history file for reading");
        File storeAndForward = FSCom.open("/history/sf", FILE_O_READ);
        if (storeAndForward) {
            size_t fileSize = storeAndForward.size();
            uint32_t numRecords = fileSize / sizeof(meshtastic_MeshPacket);

            LOG_INFO("S&F: Found file with %u bytes (%u potential messages)", fileSize, numRecords);

            // Clear existing messages and set up storage
            manager->storedMessages.clear();
            manager->seenMessages.clear();

            if (numRecords > 0) {
                // Allocate enough space but respect max
                uint32_t recordsToLoad = std::min(numRecords, manager->maxRecords);
                manager->storedMessages.resize(recordsToLoad);

                // Read all messages at once
                uint32_t bytesRead = storeAndForward.read((uint8_t *)manager->storedMessages.data(),
                                                          sizeof(meshtastic_MeshPacket) * recordsToLoad);

                LOG_INFO("S&F: Loaded %u messages from flash (%u bytes)", recordsToLoad, bytesRead);

                // Rebuild the seen messages set
                for (const auto &msg : manager->storedMessages) {
                    manager->seenMessages.insert(manager->getPacketId(msg));
                }
            }
            storeAndForward.close();
        }
    }

    // Load the user request history
    LOG_INFO("S&F: Checking for user request history file");
    if (FSCom.exists("/history/sf_users")) {
        File userRequestsFile = FSCom.open("/history/sf_users", FILE_O_READ);
        if (userRequestsFile) {
            // Read number of entries
            size_t entriesCount = 0;
            userRequestsFile.read((uint8_t *)&entriesCount, sizeof(entriesCount));

            // Read each entry
            for (size_t i = 0; i < entriesCount; i++) {
                NodeNum nodeId;
                uint32_t lastIdx;

                userRequestsFile.read((uint8_t *)&nodeId, sizeof(nodeId));
                userRequestsFile.read((uint8_t *)&lastIdx, sizeof(lastIdx));

                // Store in our map
                manager->lastRequest[nodeId] = lastIdx;
            }
            userRequestsFile.close();
        }
    }
#else
    LOG_WARN("S&F: Filesystem not implemented, can't load messages");
#endif
}

// Implementation for the interface version - delegates to concrete version if possible
void saveToFlash(IStoreForwardHistoryManager *manager)
{
    // Try to downcast to concrete class
    StoreForwardHistoryManager *concreteManager = dynamic_cast<StoreForwardHistoryManager *>(manager);

    if (concreteManager) {
        saveToFlash(concreteManager);
    } else {
        LOG_WARN("S&F: Cannot save history - concrete implementation required");
    }
}

// Implementation for the interface version - delegates to concrete version if possible
void loadFromFlash(IStoreForwardHistoryManager *manager)
{
    // Try to downcast to concrete class
    StoreForwardHistoryManager *concreteManager = dynamic_cast<StoreForwardHistoryManager *>(manager);

    if (concreteManager) {
        loadFromFlash(concreteManager);
    } else {
        LOG_WARN("S&F: Cannot load history - concrete implementation required");
    }
}

// StoreForwardProcessor persistence functions
void saveToFlash(StoreForwardProcessor *processor)
{
    if (!processor) {
        LOG_WARN("S&F: Cannot save processor state - processor is null");
        return;
    }

    LOG_INFO("S&F: Saving processor state to flash - %u messages", processor->getTotalMessageCount());

#ifdef FSCom
    LOG_INFO("S&F: Creating directory /history if needed");
    FSCom.mkdir("/history");

    LOG_INFO("S&F: Opening file /history/sf_processor for writing");
    File storeAndForward = FSCom.open("/history/sf_processor", FILE_O_WRITE);
    if (storeAndForward) {
        // Get the stored messages
        const auto &messages = processor->storedMessages;
        uint32_t totalSize = sizeof(meshtastic_MeshPacket) * messages.size();

        LOG_INFO("S&F: Writing %u bytes to flash (%u messages)", totalSize, (unsigned)messages.size());
        if (messages.size() > 0) {
            uint32_t written = storeAndForward.write((uint8_t *)messages.data(), totalSize);
            if (written == totalSize) {
                LOG_INFO("S&F: Successfully stored %u messages to flash", (unsigned)messages.size());
            } else {
                LOG_ERROR("S&F: Error writing messages to flash: %u of %u bytes written", written, totalSize);
            }
        }
        storeAndForward.close();

        // Save the lastRequest map
        LOG_INFO("S&F: Saving user request history");
        File userRequestsFile = FSCom.open("/history/sf_processor_users", FILE_O_WRITE);
        if (userRequestsFile) {
            // Write number of entries first
            size_t entriesCount = processor->lastRequest.size();
            userRequestsFile.write((uint8_t *)&entriesCount, sizeof(entriesCount));

            // Write each user's last request position
            for (const auto &entry : processor->lastRequest) {
                userRequestsFile.write((uint8_t *)&entry.first, sizeof(entry.first));   // NodeNum
                userRequestsFile.write((uint8_t *)&entry.second, sizeof(entry.second)); // lastRequest index
            }
            userRequestsFile.close();
        }
    } else {
        LOG_ERROR("S&F: Could not open processor history file for writing");
    }
#else
    LOG_WARN("S&F: Filesystem not implemented, can't save processor state");
#endif
}

void loadFromFlash(StoreForwardProcessor *processor)
{
    if (!processor) {
        LOG_WARN("S&F: Cannot load processor state - processor is null");
        return;
    }

#ifdef FSCom
    LOG_INFO("S&F: Checking if processor history file exists");
    if (FSCom.exists("/history/sf_processor")) {
        LOG_INFO("S&F: Opening processor history file for reading");
        File storeAndForward = FSCom.open("/history/sf_processor", FILE_O_READ);
        if (storeAndForward) {
            size_t fileSize = storeAndForward.size();
            uint32_t numRecords = fileSize / sizeof(meshtastic_MeshPacket);

            LOG_INFO("S&F: Found file with %u bytes (%u potential messages)", fileSize, numRecords);

            if (numRecords > 0) {
                // Clear existing data
                processor->storedMessages.clear();
                processor->storedMessages.resize(numRecords);

                // Read all messages at once
                storeAndForward.read((uint8_t *)processor->storedMessages.data(), fileSize);

                LOG_INFO("S&F: Loaded %u messages for processor", numRecords);
            }
            storeAndForward.close();
        }
    }

    // Load the user request history
    LOG_INFO("S&F: Checking for processor user request history file");
    if (FSCom.exists("/history/sf_processor_users")) {
        File userRequestsFile = FSCom.open("/history/sf_processor_users", FILE_O_READ);
        if (userRequestsFile) {
            // Read number of entries
            size_t entriesCount = 0;
            userRequestsFile.read((uint8_t *)&entriesCount, sizeof(entriesCount));

            // Read each entry
            for (size_t i = 0; i < entriesCount; i++) {
                NodeNum nodeId;
                uint32_t lastIdx;

                userRequestsFile.read((uint8_t *)&nodeId, sizeof(nodeId));
                userRequestsFile.read((uint8_t *)&lastIdx, sizeof(lastIdx));

                // Store in our map
                processor->lastRequest[nodeId] = lastIdx;
            }
            userRequestsFile.close();
        }
    }
#else
    LOG_WARN("S&F: Filesystem not implemented, can't load processor state");
#endif
}

} // namespace StoreForwardPersistence
