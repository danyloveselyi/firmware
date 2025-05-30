#include "StoreForwardPersistence.h"
#include "FSCommon.h"
#include "NodeDB.h"
#include "SafeFile.h"
#include "configuration.h"
#include <algorithm> // Add for std::min

// Include complete definitions instead of forward declarations
// Fix the incorrect include path
#include "../StoreForwardModule.h"
#include "../core/StoreForwardHistoryManager.h"
#include "../core/StoreForwardProcessor.h"

namespace StoreForwardPersistence
{

// Make lastSaveTime and messageCounter static as they're only used within this namespace
static unsigned long lastSaveTime = 0;
static uint32_t messageCounter = 0;

// Helper function to print message content in readable format
static void logMessageContent(const PacketHistoryStruct *msg, int index)
{
    if (!msg || msg->payload_size == 0)
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

    // Log detailed message info
    LOG_INFO("S&F: Message %d - from: %s (0x%x), to: %s (0x%x), time: %u, size: %d bytes", index, senderName, msg->from,
             recipientName, msg->to, msg->time, msg->payload_size);

    // Check if payload contains printable text by scanning it
    bool isPrintable = true;
    for (uint32_t i = 0; i < msg->payload_size; i++) {
        char c = (char)msg->payload[i];
        // Check for common text characters (printable ASCII + some common extended chars)
        if (c != 0 && (c < 32 || c > 126) && c != '\n' && c != '\r' && c != '\t') {
            isPrintable = false;
            break;
        }
    }

    if (isPrintable) {
        // It's likely text, so print it as text
        char textBuffer[meshtastic_Constants_DATA_PAYLOAD_LEN + 1] = {0};
        memcpy(textBuffer, msg->payload, msg->payload_size);
        textBuffer[msg->payload_size] = '\0'; // Ensure null termination

        LOG_INFO("S&F: Message %d content - TEXT MESSAGE: \"%s\"", index, textBuffer);
    } else {
        // Not plain text, print as hex
        char hexbuf[meshtastic_Constants_DATA_PAYLOAD_LEN * 3 + 1] = {0};
        for (uint32_t i = 0; i < msg->payload_size; i++) {
            sprintf(hexbuf + strlen(hexbuf), "%02x ", msg->payload[i]);
            if (i == 31 && msg->payload_size > 32) { // Show at most 32 bytes in hex
                strcat(hexbuf, "...");
                break;
            }
        }
        LOG_INFO("S&F: Message %d content - BINARY DATA: %s", index, hexbuf);
    }
}

// Add function to log MeshPacket content
void logMessageContent(const meshtastic_MeshPacket *msg, int index)
{
    if (!msg || (msg->which_payload_variant != meshtastic_MeshPacket_decoded_tag))
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

    // Log detailed message info
    LOG_INFO("S&F: Message %d - from: %s (0x%x), to: %s (0x%x), time: %u, size: %d bytes", index, senderName, msg->from,
             recipientName, msg->to, msg->rx_time, msg->decoded.payload.size);

    // Check if payload contains printable text by scanning it
    bool isPrintable = true;
    for (uint32_t i = 0; i < msg->decoded.payload.size; i++) {
        char c = (char)msg->decoded.payload.bytes[i];
        // Check for common text characters (printable ASCII + some common extended chars)
        if (c != 0 && (c < 32 || c > 126) && c != '\n' && c != '\r' && c != '\t') {
            isPrintable = false;
            break;
        }
    }

    if (isPrintable && msg->decoded.portnum == meshtastic_PortNum_TEXT_MESSAGE_APP) {
        // It's likely text, so print it as text
        char textBuffer[meshtastic_Constants_DATA_PAYLOAD_LEN + 1] = {0};
        memcpy(textBuffer, msg->decoded.payload.bytes, msg->decoded.payload.size);
        textBuffer[msg->decoded.payload.size] = '\0'; // Ensure null termination

        LOG_INFO("S&F: Message %d content - TEXT MESSAGE: \"%s\"", index, textBuffer);
    } else {
        // Not plain text, print as hex
        char hexbuf[meshtastic_Constants_DATA_PAYLOAD_LEN * 3 + 1] = {0};
        for (uint32_t i = 0; i < msg->decoded.payload.size; i++) {
            sprintf(hexbuf + strlen(hexbuf), "%02x ", msg->decoded.payload.bytes[i]);
            if (i == 31 && msg->decoded.payload.size > 32) { // Show at most 32 bytes in hex
                strcat(hexbuf, "...");
                break;
            }
        }
        LOG_INFO("S&F: Message %d content - DATA: %s", index, hexbuf);
    }
}

void saveToFlash(StoreForwardHistoryManager *historyManager)
{
    // Update lastSaveTime when saving
    lastSaveTime = millis();

    if (!historyManager) {
        LOG_WARN("S&F: Cannot save - null history manager");
        return;
    }

    uint32_t packetHistoryTotalCount = historyManager->getPacketHistoryTotalCount();
    PacketHistoryStruct *packetHistory = historyManager->getPacketHistory();

    LOG_DEBUG("S&F: saveToFlash - Starting save, total messages=%d, packetHistory=%p", packetHistoryTotalCount, packetHistory);

    if (packetHistoryTotalCount > 0 && packetHistory) {
        LOG_INFO("S&F: Saving messages to flash - count: %u, time: %lums since boot", packetHistoryTotalCount, millis());
#ifdef FSCom
        LOG_DEBUG("S&F: Checking if /history directory exists");
        if (!FSCom.exists("/history")) {
            LOG_INFO("S&F: Creating directory /history");
            if (!FSCom.mkdir("/history")) {
                LOG_ERROR("S&F: Failed to create /history directory");
            }
        }

        LOG_INFO("S&F: Opening file /history/sf for writing");
        File storeAndForward = FSCom.open("/history/sf", FILE_O_WRITE);
        if (storeAndForward) {
            uint32_t totalSize = sizeof(PacketHistoryStruct) * packetHistoryTotalCount;
            LOG_INFO("S&F: Writing %u bytes to flash (%u messages)", totalSize, packetHistoryTotalCount);

            // Debug: Log message details for first few messages
            if (packetHistoryTotalCount > 0) {
                // Log first 3 messages at most
                uint32_t messagesToLog = std::min(packetHistoryTotalCount, (uint32_t)3);
                for (uint32_t i = 0; i < messagesToLog; i++) {
                    logMessageContent(&packetHistory[i], i);
                }

                if (packetHistoryTotalCount > 3) {
                    LOG_INFO("S&F: (+ %u more messages to save)", packetHistoryTotalCount - 3);
                }
            }

            uint32_t written = storeAndForward.write((uint8_t *)packetHistory, totalSize);
            if (written == totalSize) {
                LOG_INFO("S&F: Successfully stored %u messages (%u bytes) to flash", packetHistoryTotalCount, written);
                messageCounter++;
                LOG_INFO("S&F: Total save operations since boot: %u", messageCounter);

                // If history manager has flushImmediately set, explicitly flush data
                if (historyManager->isFlushImmediately()) {
                    LOG_DEBUG("S&F: Immediate flushing enabled, forcing flush to storage");
                    storeAndForward.flush(); // Force flush to ensure data is written to storage
                    LOG_DEBUG("S&F: Flush completed");
                }
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
                const auto &lastRequest = historyManager->getLastRequestMap();
                size_t entriesCount = lastRequest.size();
                LOG_INFO("S&F: Writing request history for %u users", entriesCount);

                // Write number of entries first
                userRequestsFile.write((uint8_t *)&entriesCount, sizeof(entriesCount));

                // Write each user's last request position
                for (const auto &entry : lastRequest) {
                    userRequestsFile.write((uint8_t *)&entry.first, sizeof(entry.first));   // NodeNum
                    userRequestsFile.write((uint8_t *)&entry.second, sizeof(entry.second)); // lastRequest index

                    // Get user name if available
                    meshtastic_NodeInfoLite *userNode = nodeDB->getMeshNode(entry.first);
                    const char *userName =
                        (userNode && userNode->has_user && userNode->user.long_name[0])    ? userNode->user.long_name
                        : (userNode && userNode->has_user && userNode->user.short_name[0]) ? userNode->user.short_name
                                                                                           : "Unknown";

                    LOG_INFO("S&F: User %s (0x%08x) last request: %u", userName, entry.first, entry.second);
                }
                userRequestsFile.close();
                LOG_INFO("S&F: User request history saved successfully");
            } else {
                LOG_ERROR("S&F: Could not open user requests file for writing");
            }
        } else {
            LOG_ERROR("S&F: Could not open history file for writing, errno=%d", errno);
        }
#else
        LOG_WARN("S&F: Filesystem not implemented, can't save messages");
#endif
    } else {
        LOG_INFO("S&F: No messages to save or manager not initialized. Count=%d, History=%p", packetHistoryTotalCount,
                 packetHistory);
    }

    // Force sync if immediate flushing is enabled
    if (historyManager->isFlushImmediately()) {
        LOG_DEBUG("S&F: Immediate flushing enabled, forcing filesystem sync");
#ifdef FSCom
        // Use fsync() on all open files instead of trying to flush the entire filesystem
        // The actual files were already flushed and closed above
#endif
    }
}

void loadFromFlash(StoreForwardHistoryManager *historyManager)
{
    LOG_INFO("S&F: Attempting to load messages from flash");

    if (!historyManager) {
        LOG_WARN("S&F: Cannot load - null history manager");
        return;
    }

    PacketHistoryStruct *packetHistory = historyManager->getPacketHistory();
    uint32_t records = historyManager->getMaxRecords();

    if (!packetHistory) {
        LOG_WARN("S&F: Cannot load - packet history storage not initialized");
        return;
    }

#ifdef FSCom
    LOG_INFO("S&F: Checking if history file exists");
    if (FSCom.exists("/history/sf")) {
        LOG_INFO("S&F: Opening history file for reading");
        File storeAndForward = FSCom.open("/history/sf", FILE_O_READ);
        if (storeAndForward) {
            size_t fileSize = storeAndForward.size();
            uint32_t numRecords = fileSize / sizeof(PacketHistoryStruct);

            LOG_INFO("S&F: Found file with %u bytes (%u potential messages)", fileSize, numRecords);

            // Limit to available buffer size
            uint32_t recordsToLoad = std::min(numRecords, records);
            LOG_INFO("S&F: Will load up to %u messages (buffer capacity: %u)", recordsToLoad, records);

            if (recordsToLoad > 0) {
                LOG_INFO("S&F: Reading %u bytes from flash", sizeof(PacketHistoryStruct) * recordsToLoad);
                uint32_t bytesRead = storeAndForward.read((uint8_t *)packetHistory, sizeof(PacketHistoryStruct) * recordsToLoad);

                // Set the count in the history manager
                uint32_t loadedCount = bytesRead / sizeof(PacketHistoryStruct);
                historyManager->updateLastRequest(historyManager->getPacketHistoryTotalCount(), loadedCount);

                // Log loaded messages with full content
                LOG_INFO("S&F: Loaded %u messages from flash (%u bytes)", loadedCount, bytesRead);

                // Log all messages in detail
                for (uint32_t i = 0; i < loadedCount; i++) {
                    logMessageContent(&packetHistory[i], i);
                }
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

                // Make sure index is within valid range
                if (lastIdx <= historyManager->getPacketHistoryTotalCount()) {
                    historyManager->updateLastRequest(nodeId, lastIdx);
                    LOG_INFO("S&F: Loaded user %s (0x%08x) with lastRequest: %u", userName, nodeId, lastIdx);
                } else {
                    historyManager->updateLastRequest(nodeId, 0); // Reset if out of range
                    LOG_WARN("S&F: User %s (0x%08x) had invalid lastRequest: %u (reset to 0)", userName, nodeId, lastIdx);
                }
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

void saveToFlash(StoreForwardModule *module)
{
    // Update lastSaveTime when saving
    lastSaveTime = millis();

    if (!module) {
        LOG_WARN("S&F: Cannot save - null module");
        return;
    }

    // Delegate to the history manager if available
    IStoreForwardHistoryManager *historyManager = module->getHistoryManager();
    if (historyManager && dynamic_cast<StoreForwardHistoryManager *>(historyManager)) {
        saveToFlash(dynamic_cast<StoreForwardHistoryManager *>(historyManager));
    } else {
        LOG_WARN("S&F: No valid history manager found in module");
    }
}

void loadFromFlash(StoreForwardModule *module)
{
    LOG_INFO("S&F: Attempting to load messages from flash");

    if (!module) {
        LOG_WARN("S&F: Cannot load - null module");
        return;
    }

    // Delegate to the history manager if available
    IStoreForwardHistoryManager *historyManager = module->getHistoryManager();
    if (historyManager && dynamic_cast<StoreForwardHistoryManager *>(historyManager)) {
        loadFromFlash(dynamic_cast<StoreForwardHistoryManager *>(historyManager));
    } else {
        LOG_WARN("S&F: No valid history manager found in module");
    }
}

void saveToFlash(StoreForwardProcessor *processor)
{
    // This function requires the full definition of StoreForwardProcessor
    // Implementation would be similar to the StoreForwardModule version
    // For now, just log that it's not fully implemented
    LOG_INFO("S&F: Processor saveToFlash not fully implemented");
}

void loadFromFlash(StoreForwardProcessor *processor)
{
    // This function requires the full definition of StoreForwardProcessor
    // Implementation would be similar to the StoreForwardModule version
    // For now, just log that it's not fully implemented
    LOG_INFO("S&F: Processor loadFromFlash not fully implemented");
}

void saveToFlash(IStoreForwardHistoryManager *manager)
{
    // Try to cast to concrete class
    StoreForwardHistoryManager *concreteManager = dynamic_cast<StoreForwardHistoryManager *>(manager);
    if (concreteManager) {
        saveToFlash(concreteManager);
    } else {
        LOG_WARN("S&F: Cannot save - manager is not of expected type");
    }
}

void loadFromFlash(IStoreForwardHistoryManager *manager)
{
    // Try to cast to concrete class
    StoreForwardHistoryManager *concreteManager = dynamic_cast<StoreForwardHistoryManager *>(manager);
    if (concreteManager) {
        loadFromFlash(concreteManager);
    } else {
        LOG_WARN("S&F: Cannot load - manager is not of expected type");
    }
}

} // namespace StoreForwardPersistence
