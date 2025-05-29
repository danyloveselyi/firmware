#include "StoreForwardPersistence.h"
#include "FSCommon.h"
#include "NodeDB.h"
#include "StoreForwardHistoryManager.h"
#include "StoreForwardModule.h" // Include the full header instead of just forward declaration
#include "StoreForwardProcessor.h"
#include "configuration.h"
#include <algorithm> // Add for std::min

namespace StoreForwardPersistence
{
// Make lastSaveTime and messageCounter static as they're only used within this namespace
static unsigned long lastSaveTime = 0;
static uint32_t messageCounter = 0;

// Implementation of the function declared in the header
void logMessageContent(const meshtastic_MeshPacket *msg, int index)
{
    if (!msg)
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

    // Log message info based on whether it's decoded or encrypted
    if (msg->which_payload_variant == meshtastic_MeshPacket_decoded_tag) {
        LOG_INFO("S&F: Message %d - from: %s (0x%x), to: %s (0x%x), time: %u, size: %d bytes", index, senderName, msg->from,
                 recipientName, msg->to, msg->rx_time, msg->decoded.payload.size);
    } else {
        LOG_INFO("S&F: Message %d - from: %s (0x%x), to: %s (0x%x), time: %u, encrypted", index, senderName, msg->from,
                 recipientName, msg->to, msg->rx_time);
    }
}

// Implementation for StoreForwardModule
void saveToFlash(StoreForwardModule *module)
{
    // Update lastSaveTime when saving
    lastSaveTime = millis();

    if (!module) {
        LOG_ERROR("S&F: Cannot save - module is null");
        return;
    }

    LOG_INFO("S&F: Saving module state to flash");

    // Use the history manager for saving - handle as interface
    if (module->getHistoryManager()) {
        saveToFlash(static_cast<IStoreForwardHistoryManager *>(module->getHistoryManager()));
    } else {
        LOG_ERROR("S&F: Cannot save - history manager is null");
    }
}

void loadFromFlash(StoreForwardModule *module)
{
    LOG_INFO("S&F: Loading module state from flash");

    if (!module) {
        LOG_ERROR("S&F: Cannot load - module is null");
        return;
    }

    // Use the history manager for loading - handle as interface
    if (module->getHistoryManager()) {
        loadFromFlash(static_cast<IStoreForwardHistoryManager *>(module->getHistoryManager()));
    } else {
        LOG_ERROR("S&F: Cannot load - history manager is null");
    }
}

// Implementation for IStoreForwardHistoryManager
void saveToFlash(IStoreForwardHistoryManager *manager)
{
    if (!manager) {
        LOG_ERROR("S&F: Cannot save - history manager is null");
        return;
    }

    // Update lastSaveTime when saving
    lastSaveTime = millis();

    LOG_INFO("S&F: Saving history manager state to flash - %u messages", manager->getTotalMessageCount());

#ifdef FSCom
    LOG_INFO("S&F: Creating directory /history if needed");
    FSCom.mkdir("/history");

    LOG_INFO("S&F: Opening file /history/sf for writing");
    File storeAndForward = FSCom.open("/history/sf", FILE_O_WRITE);
    if (storeAndForward) {
        const auto &messages = manager->getAllStoredMessages();
        uint32_t totalSize = sizeof(meshtastic_MeshPacket) * messages.size();
        LOG_INFO("S&F: Writing %u bytes to flash (%u messages)", totalSize, (unsigned)messages.size());

        // Debug: Log message details for first few messages
        if (!messages.empty()) {
            // Log first 3 messages at most
            uint32_t messagesToLog = std::min(messages.size(), (size_t)3);
            for (uint32_t i = 0; i < messagesToLog; i++) {
                logMessageContent(&messages[i], i);
            }

            if (messages.size() > 3) {
                LOG_INFO("S&F: (+ %u more messages to save)", (unsigned)messages.size() - 3);
            }
        }

        uint32_t written = storeAndForward.write((uint8_t *)messages.data(), totalSize);
        if (written == totalSize) {
            LOG_INFO("S&F: Successfully stored %u messages (%u bytes) to flash", (unsigned)messages.size(), written);
            messageCounter++;
            LOG_INFO("S&F: Total save operations since boot: %u", messageCounter);
        } else {
            LOG_ERROR("S&F: Error writing messages to flash: %u of %u bytes written", written, totalSize);
        }
        storeAndForward.close();
        LOG_INFO("S&F: File closed");

        // Save the lastRequest map to track what each user has already received
        LOG_INFO("S&F: Saving user request history");
        File userRequestsFile = FSCom.open("/history/sf_users", FILE_O_WRITE);
        if (userRequestsFile) {
            // Format: [NodeNum (4 bytes)][lastRequestIndex (4 bytes)] for each entry
            // This will need to be modified to use the interface's API
            std::vector<std::pair<NodeNum, uint32_t>> requestMap;

            // Get all nodes with last request indices
            for (NodeNum n = 0; n < NODENUM_BROADCAST; n++) {
                uint32_t idx = manager->getLastRequestIndex(n);
                if (idx > 0) {
                    requestMap.push_back(std::make_pair(n, idx));
                }
            }

            size_t entriesCount = requestMap.size();
            LOG_INFO("S&F: Writing request history for %u users", (unsigned)entriesCount);

            // Write number of entries first
            userRequestsFile.write((uint8_t *)&entriesCount, sizeof(entriesCount));

            // Write each user's last request position
            for (const auto &entry : requestMap) {
                userRequestsFile.write((uint8_t *)&entry.first, sizeof(entry.first));   // NodeNum
                userRequestsFile.write((uint8_t *)&entry.second, sizeof(entry.second)); // lastRequest index

                // Get user name if available
                meshtastic_NodeInfoLite *userNode = nodeDB->getMeshNode(entry.first);
                const char *userName = (userNode && userNode->has_user && userNode->user.long_name[0]) ? userNode->user.long_name
                                       : (userNode && userNode->has_user && userNode->user.short_name[0])
                                           ? userNode->user.short_name
                                           : "Unknown";

                LOG_INFO("S&F: User %s (0x%08x) last request: %u", userName, entry.first, entry.second);
            }
            userRequestsFile.close();
            LOG_INFO("S&F: User request history saved successfully");
        } else {
            LOG_ERROR("S&F: Could not open user requests file for writing");
        }
    } else {
        LOG_ERROR("S&F: Could not open history file for writing");
    }
#else
    LOG_WARN("S&F: Filesystem not implemented, can't save messages");
#endif
}

void loadFromFlash(IStoreForwardHistoryManager *manager)
{
    if (!manager) {
        LOG_ERROR("S&F: Cannot load - history manager is null");
        return;
    }

    LOG_INFO("S&F: Attempting to load messages from flash");

#ifdef FSCom
    LOG_INFO("S&F: Checking if history file exists");
    if (FSCom.exists("/history/sf")) {
        LOG_INFO("S&F: Opening history file for reading");
        File storeAndForward = FSCom.open("/history/sf", FILE_O_READ);
        if (storeAndForward) {
            size_t fileSize = storeAndForward.size();
            uint32_t numRecords = fileSize / sizeof(meshtastic_MeshPacket);

            LOG_INFO("S&F: Found file with %u bytes (%u potential messages)", fileSize, numRecords);

            if (numRecords > 0) {
                std::vector<meshtastic_MeshPacket> messages;
                messages.resize(numRecords);

                // Read directly into the vector
                uint32_t bytesRead = storeAndForward.read((uint8_t *)messages.data(), fileSize);
                LOG_INFO("S&F: Read %u bytes from flash", bytesRead);

                // Add messages to history manager
                for (const auto &msg : messages) {
                    manager->record(msg);
                }

                LOG_INFO("S&F: Loaded %u messages into history manager", numRecords);
            } else {
                LOG_INFO("S&F: No records to load from history file");
            }
            storeAndForward.close();
            LOG_INFO("S&F: File closed");
        } else {
            LOG_ERROR("S&F: Could not open history file for reading");
        }
    } else {
        LOG_INFO("S&F: No history file found, starting with empty history");
    }

    // Load the user request history
    LOG_INFO("S&F: Checking for user request history file");
    if (FSCom.exists("/history/sf_users")) {
        File userRequestsFile = FSCom.open("/history/sf_users", FILE_O_READ);
        if (userRequestsFile) {
            LOG_INFO("S&F: Loading user request history");

            // Read number of entries
            size_t entriesCount = 0;
            userRequestsFile.read((uint8_t *)&entriesCount, sizeof(entriesCount));
            LOG_INFO("S&F: Found request history for %u users", entriesCount);

            // Read each entry
            for (size_t i = 0; i < entriesCount; i++) {
                NodeNum nodeId;
                uint32_t lastIdx;

                userRequestsFile.read((uint8_t *)&nodeId, sizeof(nodeId));
                userRequestsFile.read((uint8_t *)&lastIdx, sizeof(lastIdx));

                // Get user name if available
                meshtastic_NodeInfoLite *userNode = nodeDB->getMeshNode(nodeId);
                const char *userName = (userNode && userNode->has_user && userNode->user.long_name[0]) ? userNode->user.long_name
                                       : (userNode && userNode->has_user && userNode->user.short_name[0])
                                           ? userNode->user.short_name
                                           : "Unknown";

                // Update the last request position
                manager->updateLastRequest(nodeId, lastIdx);
                LOG_INFO("S&F: Loaded user %s (0x%08x) with lastRequest: %u", userName, nodeId, lastIdx);
            }
            userRequestsFile.close();
            LOG_INFO("S&F: User request history loaded successfully");
        } else {
            LOG_ERROR("S&F: Could not open user requests file for reading");
        }
    } else {
        LOG_INFO("S&F: No user request history file found");
    }
#else
    LOG_WARN("S&F: Filesystem not implemented, can't load messages");
#endif
}

// Legacy implementations that cast to the interface version
void saveToFlash(StoreForwardHistoryManager *manager)
{
    if (manager) {
        saveToFlash(static_cast<IStoreForwardHistoryManager *>(manager));
    }
}

void loadFromFlash(StoreForwardHistoryManager *manager)
{
    if (manager) {
        loadFromFlash(static_cast<IStoreForwardHistoryManager *>(manager));
    }
}

// Implementation for StoreForwardProcessor
void saveToFlash(StoreForwardProcessor *processor)
{
    if (!processor) {
        LOG_ERROR("S&F: Cannot save - processor is null");
        return;
    }

    LOG_INFO("S&F: Saving processor state to flash - %u messages", processor->getTotalMessageCount());

#ifdef FSCom
    LOG_INFO("S&F: Creating directory /history if needed");
    FSCom.mkdir("/history");

    LOG_INFO("S&F: Opening file /history/sf for writing");
    File storeAndForward = FSCom.open("/history/sf", FILE_O_WRITE);
    if (storeAndForward) {
        const auto &messages = processor->storedMessages;
        uint32_t totalSize = sizeof(meshtastic_MeshPacket) * messages.size();
        LOG_INFO("S&F: Writing %u bytes to flash (%u messages)", totalSize, (unsigned)messages.size());

        uint32_t written = storeAndForward.write((uint8_t *)messages.data(), totalSize);
        if (written == totalSize) {
            LOG_INFO("S&F: Successfully stored %u messages (%u bytes) to flash", (unsigned)messages.size(), written);
        } else {
            LOG_ERROR("S&F: Error writing messages to flash: %u of %u bytes written", written, totalSize);
        }
        storeAndForward.close();
        LOG_INFO("S&F: File closed");

        // Save the lastRequest map
        File userRequestsFile = FSCom.open("/history/sf_users", FILE_O_WRITE);
        if (userRequestsFile) {
            size_t entriesCount = processor->lastRequest.size();
            userRequestsFile.write((uint8_t *)&entriesCount, sizeof(entriesCount));

            for (const auto &entry : processor->lastRequest) {
                userRequestsFile.write((uint8_t *)&entry.first, sizeof(entry.first));
                userRequestsFile.write((uint8_t *)&entry.second, sizeof(entry.second));
            }
            userRequestsFile.close();
        }
    }
#else
    LOG_WARN("S&F: Filesystem not implemented, can't save messages");
#endif
}

void loadFromFlash(StoreForwardProcessor *processor)
{
    if (!processor) {
        LOG_ERROR("S&F: Cannot load - processor is null");
        return;
    }

    LOG_INFO("S&F: Loading processor state from flash");

#ifdef FSCom
    if (FSCom.exists("/history/sf")) {
        File storeAndForward = FSCom.open("/history/sf", FILE_O_READ);
        if (storeAndForward) {
            size_t fileSize = storeAndForward.size();
            uint32_t numRecords = fileSize / sizeof(meshtastic_MeshPacket);

            // Clear existing messages
            processor->storedMessages.clear();
            processor->storedMessages.resize(numRecords);

            storeAndForward.read((uint8_t *)processor->storedMessages.data(), fileSize);
            storeAndForward.close();

            LOG_INFO("S&F: Loaded %u messages from flash", numRecords);
        }
    }

    // Load the lastRequest map
    if (FSCom.exists("/history/sf_users")) {
        File userRequestsFile = FSCom.open("/history/sf_users", FILE_O_READ);
        if (userRequestsFile) {
            size_t entriesCount = 0;
            userRequestsFile.read((uint8_t *)&entriesCount, sizeof(entriesCount));

            for (size_t i = 0; i < entriesCount; i++) {
                NodeNum nodeId;
                uint32_t lastIdx;

                userRequestsFile.read((uint8_t *)&nodeId, sizeof(nodeId));
                userRequestsFile.read((uint8_t *)&lastIdx, sizeof(lastIdx));

                processor->lastRequest[nodeId] = lastIdx;
            }
            userRequestsFile.close();
        }
    }
#else
    LOG_WARN("S&F: Filesystem not implemented, can't load messages");
#endif
}

} // namespace StoreForwardPersistence
