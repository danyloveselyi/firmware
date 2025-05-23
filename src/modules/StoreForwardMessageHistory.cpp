#include "StoreForwardMessageHistory.h"
#include "configuration.h"
#include <algorithm>

namespace meshtastic
{

StoreForwardMessageHistory::StoreForwardMessageHistory()
{
    // Initialize with empty set
}

bool StoreForwardMessageHistory::hasMessageBeenReceived(uint32_t messageId) const
{
    return receivedMessageIds.find(messageId) != receivedMessageIds.end();
}

void StoreForwardMessageHistory::recordReceivedMessage(uint32_t messageId)
{
    // Only add if not already present
    if (receivedMessageIds.find(messageId) == receivedMessageIds.end()) {
        receivedMessageIds.insert(messageId);
        changed = true;

        // If we have too many entries, remove oldest ones
        if (receivedMessageIds.size() > 10000) {
            LOG_INFO("S&F: Client received message history too large, pruning");

            // Create a temporary vector for sorting
            std::vector<uint32_t> temp(receivedMessageIds.begin(), receivedMessageIds.end());

            // Sort by ID (newer messages typically have higher IDs)
            std::sort(temp.begin(), temp.end());

            // Keep only the newer half
            std::size_t halfSize = temp.size() / 2;
            receivedMessageIds.clear();
            for (std::size_t i = halfSize; i < temp.size(); i++) {
                receivedMessageIds.insert(temp[i]);
            }
        }
    }
}

std::vector<uint32_t> StoreForwardMessageHistory::getRecentMessageIds(std::size_t maxIds) const
{
    std::vector<uint32_t> result(receivedMessageIds.begin(), receivedMessageIds.end());

    // Sort in descending order so newest are first
    std::sort(result.begin(), result.end(), std::greater<uint32_t>());

    // Limit the number
    if (result.size() > maxIds) {
        result.resize(maxIds);
    }

    return result;
}

void StoreForwardMessageHistory::saveToFlash()
{
    // Only save if we have any message IDs
    if (receivedMessageIds.empty()) {
        LOG_DEBUG("S&F: No message IDs to save");
        return;
    }

    LOG_INFO("S&F: Saving received message history - %d message IDs", receivedMessageIds.size());

    // Client device storage implementation priority:
    // 1. ESP32: NVS Preferences
    // 2. NRF52: LittleFS
    // 3. NRF52: FlashStorage
    // 4. FSCom fallback only if nothing else available

#if defined(ESP32)
    // Best option for ESP32 clients: Preferences NVS
    preferences.begin("sf-history", false);

    // Save up to 100 most recent message IDs
    std::vector<uint32_t> recentIds = getRecentMessageIds(100);

    preferences.putBytes("msg_ids", recentIds.data(), recentIds.size() * sizeof(uint32_t));
    preferences.putUShort("id_count", recentIds.size());

    preferences.end();
    changed = false;
    LOG_INFO("S&F: Saved %d message IDs to ESP32 NVS storage", recentIds.size());

#elif defined(ARCH_NRF52) && defined(LittleFSSupported)
    // Best option for NRF52 clients: LittleFS internal storage
    if (!LittleFS.begin()) {
        LOG_ERROR("S&F: LittleFS initialization failed");
        return;
    }

    File file = LittleFS.open("/sf_received.dat", "w");
    if (file) {
        // Save up to 50 most recent message IDs (NRF52 has less storage)
        std::vector<uint32_t> recentIds = getRecentMessageIds(50);

        // Write number of IDs
        uint16_t numIds = recentIds.size();
        file.write((uint8_t *)&numIds, sizeof(numIds));

        // Write each ID
        for (auto id : recentIds) {
            file.write((uint8_t *)&id, sizeof(id));
        }

        file.close();
        changed = false;
        LOG_INFO("S&F: Saved %d message IDs to LittleFS", numIds);
    } else {
        LOG_ERROR("S&F: Failed to open LittleFS file for writing");
    }

#elif defined(ARCH_NRF52) && defined(FlashStorageSupported)
    // Fallback for NRF52 clients: FlashStorage
    // Save up to 50 most recent message IDs (NRF52 has less storage)
    std::vector<uint32_t> recentIds = getRecentMessageIds(50);

    // On NRF52 devices, FlashStorage implementation varies
    // This is a simplified approach - actual implementation depends on specific library
    uint32_t storage[52];    // 50 IDs + 2 for count and header
    storage[0] = 0xABCD1234; // Magic header
    storage[1] = recentIds.size();

    for (size_t i = 0; i < recentIds.size() && i < 50; i++) {
        storage[i + 2] = recentIds[i];
    }

// Write to flash - implementation depends on specific FlashStorage library
#ifdef FLASH_STORAGE_SAMD
    FlashStorage.write(FLASH_STORAGE_START_ADDRESS, storage, sizeof(storage));
#else
    // Generic FlashStorage API call - adjust based on actual implementation
    writeFlash(0, storage, sizeof(storage));
#endif

    changed = false;
    LOG_INFO("S&F: Saved %d message IDs to NRF52 FlashStorage", recentIds.size());

#elif defined(FSCom)
    // Last resort fallback to filesystem if available
    static const char *filename = "/history/sf_received";

    // Create directory if needed
    if (!FSCom.exists("/history")) {
        if (!FSCom.mkdir("/history")) {
            LOG_WARN("S&F: Could not create history directory");
        }
    }

    File receivedFile = FSCom.open(filename, FILE_O_WRITE);
    if (receivedFile) {
        // Limit to 100 IDs to save space
        std::vector<uint32_t> recentIds = getRecentMessageIds(100);
        size_t numIds = recentIds.size();

        receivedFile.write((uint8_t *)&numIds, sizeof(numIds));

        // Save each ID from the vector
        for (auto id : recentIds) {
            receivedFile.write((uint8_t *)&id, sizeof(id));
        }

        receivedFile.close();
        changed = false;
        LOG_INFO("S&F: Saved %d message IDs to filesystem", numIds);
    } else {
        LOG_WARN("S&F: Could not open file for writing");
    }
#else
    LOG_WARN("S&F: No suitable storage available on this platform, message history not saved");
#endif
}

void StoreForwardMessageHistory::loadFromFlash()
{
    bool loaded = false;

#if defined(ESP32)
    // Best option for ESP32 clients: Preferences NVS
    preferences.begin("sf-history", true); // read-only mode

    size_t idCount = preferences.getUShort("id_count", 0);

    if (idCount > 0) {
        std::vector<uint32_t> savedIds(idCount);
        size_t bytesRead = preferences.getBytes("msg_ids", savedIds.data(), idCount * sizeof(uint32_t));

        if (bytesRead > 0) {
            // Calculate how many complete IDs we read
            size_t idsRead = bytesRead / sizeof(uint32_t);

            receivedMessageIds.clear();
            for (size_t i = 0; i < idsRead; i++) {
                receivedMessageIds.insert(savedIds[i]);
            }

            LOG_INFO("S&F: Loaded %d message IDs from ESP32 NVS storage", idsRead);
            loaded = true;
        }
    }

    preferences.end();

#elif defined(ARCH_NRF52) && defined(LittleFSSupported)
    // Best option for NRF52 clients: LittleFS internal storage
    if (LittleFS.begin()) {
        if (LittleFS.exists("/sf_received.dat")) {
            File file = LittleFS.open("/sf_received.dat", "r");
            if (file) {
                // Read number of IDs
                uint16_t numIds;
                file.read((uint8_t *)&numIds, sizeof(numIds));

                receivedMessageIds.clear();
                for (uint16_t i = 0; i < numIds; i++) {
                    uint32_t id;
                    file.read((uint8_t *)&id, sizeof(id));
                    receivedMessageIds.insert(id);
                }

                file.close();
                LOG_INFO("S&F: Loaded %d message IDs from LittleFS", numIds);
                loaded = true;
            }
        }
    }

#elif defined(ARCH_NRF52) && defined(FlashStorageSupported)
    // Fallback for NRF52 clients: FlashStorage
    uint32_t storage[52]; // 50 IDs + 2 for count and header

// Read from flash - implementation depends on specific FlashStorage library
#ifdef FLASH_STORAGE_SAMD
    FlashStorage.read(FLASH_STORAGE_START_ADDRESS, storage, sizeof(storage));
#else
    // Generic FlashStorage API call - adjust based on actual implementation
    readFlash(0, storage, sizeof(storage));
#endif

    // Check magic header
    if (storage[0] == 0xABCD1234) {
        uint16_t numIds = storage[1];

        receivedMessageIds.clear();
        for (uint16_t i = 0; i < numIds && i < 50; i++) {
            receivedMessageIds.insert(storage[i + 2]);
        }

        LOG_INFO("S&F: Loaded %d message IDs from NRF52 FlashStorage", numIds);
        loaded = true;
    }

#elif defined(FSCom)
    // Last resort fallback to filesystem if available
    if (FSCom.exists("/history/sf_received")) {
        File receivedFile = FSCom.open("/history/sf_received", FILE_O_READ);
        if (receivedFile) {
            size_t numIds = 0;
            receivedFile.read((uint8_t *)&numIds, sizeof(numIds));

            receivedMessageIds.clear();
            for (size_t i = 0; i < numIds; i++) {
                uint32_t id;
                receivedFile.read((uint8_t *)&id, sizeof(id));
                receivedMessageIds.insert(id);
            }

            receivedFile.close();
            LOG_INFO("S&F: Loaded %d message IDs from filesystem", numIds);
            loaded = true;
        }
    }
#endif

    if (!loaded) {
        LOG_INFO("S&F: No saved message history found or failed to load");
        receivedMessageIds.clear();
    }

    changed = false;
}

} // namespace meshtastic
