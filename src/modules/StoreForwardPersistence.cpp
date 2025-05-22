#include "StoreForwardPersistence.h"
#include "NodeDB.h"
#include "configuration.h"
#include "FSCommon.h"
#include <algorithm>  // Add for std::min

namespace StoreForwardPersistence {

// Make lastSaveTime and messageCounter static as they're only used within this namespace
static unsigned long lastSaveTime = 0;
static uint32_t messageCounter = 0;

// Helper function to print message content in readable format
static void logMessageContent(const PacketHistoryStruct *msg, const char* prefix) {
    if (!msg || msg->payload_size == 0) return;
    
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
        
        LOG_INFO("%s TEXT MESSAGE: \"%s\"", prefix, textBuffer);
    } else {
        // Not plain text, print as hex
        char hexbuf[meshtastic_Constants_DATA_PAYLOAD_LEN * 3 + 1] = {0};
        for (uint32_t i = 0; i < msg->payload_size; i++) {
            sprintf(hexbuf + strlen(hexbuf), "%02x ", msg->payload[i]);
            if (i == 31 && msg->payload_size > 32) {  // Show at most 32 bytes in hex
                strcat(hexbuf, "...");
                break;
            }
        }
        LOG_INFO("%s BINARY DATA: %s", prefix, hexbuf);
    }
}

void saveToFlash(StoreForwardModule *module)
{
    // Update lastSaveTime when saving
    lastSaveTime = millis();
    
    if (module && module->packetHistoryTotalCount > 0) {
        LOG_INFO("S&F: Saving messages to flash - count: %u, time: %lums since boot", 
                 module->packetHistoryTotalCount, millis());
#ifdef FSCom
        LOG_INFO("S&F: Creating directory /history if needed");
        FSCom.mkdir("/history");
        
        LOG_INFO("S&F: Opening file /history/sf for writing");
        File storeAndForward = FSCom.open("/history/sf", FILE_O_WRITE);
        if (storeAndForward) {
            uint32_t totalSize = sizeof(PacketHistoryStruct) * module->packetHistoryTotalCount;
            LOG_INFO("S&F: Writing %u bytes to flash (%u messages)", 
                     totalSize, module->packetHistoryTotalCount);
            
            // Debug: Print first message details with full content
            if (module->packetHistoryTotalCount > 0) {
                PacketHistoryStruct *firstMsg = &module->packetHistory[0];
                LOG_INFO("S&F: First message - from: 0x%08x, to: 0x%08x, time: %u, size: %u bytes", 
                         firstMsg->from, firstMsg->to, firstMsg->time, firstMsg->payload_size);
                
                // Log full message content in appropriate format
                logMessageContent(firstMsg, "S&F: First message content -");
            }
            
            uint32_t written = storeAndForward.write((uint8_t *)module->packetHistory, totalSize);
            if (written == totalSize) {
                LOG_INFO("S&F: Successfully stored %u messages (%u bytes) to flash", 
                         module->packetHistoryTotalCount, written);
                messageCounter++;
                LOG_INFO("S&F: Total save operations since boot: %u", messageCounter);
            } else {
                LOG_ERROR("S&F: Error writing messages to flash: %u of %u bytes written", 
                          written, totalSize);
            }
            storeAndForward.close();
            LOG_INFO("S&F: File closed");
            
            // Save the lastRequest map to track what each user has already received
            LOG_INFO("S&F: Saving user request history");
            File userRequestsFile = FSCom.open("/history/sf_users", FILE_O_WRITE);
            if (userRequestsFile) {
                // Format: [NodeNum (4 bytes)][lastRequestIndex (4 bytes)] for each entry
                size_t entriesCount = module->lastRequest.size();
                LOG_INFO("S&F: Writing request history for %u users", entriesCount);
                
                // Write number of entries first
                userRequestsFile.write((uint8_t*)&entriesCount, sizeof(entriesCount));
                
                // Write each user's last request position
                for (const auto &entry : module->lastRequest) {
                    userRequestsFile.write((uint8_t*)&entry.first, sizeof(entry.first));      // NodeNum
                    userRequestsFile.write((uint8_t*)&entry.second, sizeof(entry.second));    // lastRequest index
                    LOG_INFO("S&F: User 0x%08x last request: %u", entry.first, entry.second);
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
    } else {
        LOG_INFO("S&F: No messages to save or module not initialized");
    }
}

void loadFromFlash(StoreForwardModule *module)
{
    LOG_INFO("S&F: Attempting to load messages from flash");
    
    if (!module || !module->packetHistory) {
        LOG_WARN("S&F: Module not initialized, skipping history load");
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
            
            LOG_INFO("S&F: Found file with %u bytes (%u potential messages)", 
                     fileSize, numRecords);
            
            // Limit to available buffer size - use std::min instead of MIN macro
            uint32_t recordsToLoad = std::min(numRecords, module->records);
            LOG_INFO("S&F: Will load up to %u messages (buffer capacity: %u)", 
                     recordsToLoad, module->records);
            
            if (recordsToLoad > 0) {
                LOG_INFO("S&F: Reading %u bytes from flash", 
                         sizeof(PacketHistoryStruct) * recordsToLoad);
                uint32_t bytesRead = storeAndForward.read((uint8_t *)module->packetHistory, 
                                                          sizeof(PacketHistoryStruct) * recordsToLoad);
                                                          
                module->packetHistoryTotalCount = recordsToLoad;
                
                // Debug: Print details of loaded messages with full content
                LOG_INFO("S&F: Loaded %u messages from flash (%u bytes)", recordsToLoad, bytesRead);
                if (recordsToLoad > 0) {
                    for (uint32_t i = 0; i < std::min((uint32_t)3, recordsToLoad); i++) {
                        PacketHistoryStruct *msg = &module->packetHistory[i];
                        LOG_INFO("S&F: Message %u - from: 0x%08x, to: 0x%08x, time: %u, size: %u bytes", 
                                i, msg->from, msg->to, msg->time, msg->payload_size);
                        
                        // Log full message content in appropriate format
                        char prefix[40];
                        snprintf(prefix, sizeof(prefix), "S&F: Message %u content -", i);
                        logMessageContent(msg, prefix);
                    }
                    
                    if (recordsToLoad > 3) {
                        LOG_INFO("S&F: (+ %u more messages)", recordsToLoad - 3);
                    }
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
            userRequestsFile.read((uint8_t*)&entriesCount, sizeof(entriesCount));
            LOG_INFO("S&F: Found request history for %u users", entriesCount);
            
            // Read each entry
            for (size_t i = 0; i < entriesCount; i++) {
                NodeNum nodeId;
                uint32_t lastIdx;
                
                userRequestsFile.read((uint8_t*)&nodeId, sizeof(nodeId));
                userRequestsFile.read((uint8_t*)&lastIdx, sizeof(lastIdx));
                
                // Make sure index is within valid range
                if (lastIdx <= module->packetHistoryTotalCount) {
                    module->lastRequest[nodeId] = lastIdx;
                    LOG_INFO("S&F: Loaded user 0x%08x with lastRequest: %u", nodeId, lastIdx);
                } else {
                    module->lastRequest[nodeId] = 0; // Reset if out of range
                    LOG_WARN("S&F: User 0x%08x had invalid lastRequest: %u (reset to 0)", nodeId, lastIdx);
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

} // namespace StoreForwardPersistence