#include "StoreForwardHistoryManager.h"
#include "NodeDB.h"
#include "RTC.h"
#include "StoreForwardPersistence.h"
#include "configuration.h"
#include <algorithm>

StoreForwardHistoryManager::StoreForwardHistoryManager(ILogger &logger) : logger(logger)
{
    loadFromFlash();
}

bool StoreForwardHistoryManager::shouldStore(const meshtastic_MeshPacket &packet) const
{
    // Store text messages and store & forward app messages
    switch (packet.decoded.portnum) {
    case meshtastic_PortNum_TEXT_MESSAGE_APP:
    case meshtastic_PortNum_STORE_FORWARD_APP:
        return !isDuplicate(packet);
    default:
        return false;
    }
}

bool StoreForwardHistoryManager::isDuplicate(const meshtastic_MeshPacket &packet) const
{
    return seenMessages.find(getPacketId(packet)) != seenMessages.end();
}

void StoreForwardHistoryManager::record(const meshtastic_MeshPacket &packet)
{
    // Add to seen messages to prevent duplicates
    seenMessages.insert(getPacketId(packet));

    // Store a copy of the packet
    meshtastic_MeshPacket packetCopy = packet;
    packetCopy.rx_time = getTime(); // Set received time to now
    storedMessages.push_back(packetCopy);

    // Limit storage to maxRecords
    if (storedMessages.size() > maxRecords) {
        size_t numToRemove = storedMessages.size() - maxRecords;
        storedMessages.erase(storedMessages.begin(), storedMessages.begin() + numToRemove);
        logger.info("S&F: Removed %u old messages to stay within limit", (unsigned)numToRemove);
    }

    // Save to flash when we reach a reasonable number of new messages
    if (storedMessages.size() % SAVE_INTERVAL_MESSAGES == 0) {
        saveToFlash();
    }
}

uint32_t StoreForwardHistoryManager::getPacketId(const meshtastic_MeshPacket &packet) const
{
    return packet.id;
}

std::vector<meshtastic_MeshPacket> StoreForwardHistoryManager::getMessagesForNode(NodeNum dest, uint32_t sinceTime) const
{
    std::vector<meshtastic_MeshPacket> result;

    // Start from the last requested index to save processing time
    uint32_t startIdx = getLastRequestIndex(dest);

    for (uint32_t i = startIdx; i < storedMessages.size(); i++) {
        const auto &packet = storedMessages[i];

        // Include messages that are:
        // 1. Newer than the sinceTime parameter
        // 2. Not from the requesting node itself
        // 3. Either broadcasts or addressed to this node
        if (packet.rx_time > sinceTime && packet.from != dest && (packet.to == NODENUM_BROADCAST || packet.to == dest)) {
            result.push_back(packet);
        }
    }

    return result;
}

uint32_t StoreForwardHistoryManager::getNumAvailablePackets(NodeNum dest, uint32_t lastTime) const
{
    uint32_t count = 0;
    uint32_t startIdx = getLastRequestIndex(dest);

    for (uint32_t i = startIdx; i < storedMessages.size(); i++) {
        const auto &packet = storedMessages[i];
        if (packet.rx_time > lastTime && packet.from != dest && (packet.to == NODENUM_BROADCAST || packet.to == dest)) {
            count++;
        }
    }

    return count;
}

void StoreForwardHistoryManager::updateLastRequest(NodeNum dest, uint32_t index)
{
    // Ensure index is valid
    if (index <= storedMessages.size()) {
        lastRequest[dest] = index;
    }
}

uint32_t StoreForwardHistoryManager::getLastRequestIndex(NodeNum dest) const
{
    auto it = lastRequest.find(dest);
    return (it != lastRequest.end()) ? it->second : 0;
}

uint32_t StoreForwardHistoryManager::getTotalMessageCount() const
{
    return storedMessages.size();
}

uint32_t StoreForwardHistoryManager::getMaxRecords() const
{
    return maxRecords;
}

void StoreForwardHistoryManager::setMaxRecords(uint32_t newMaxRecords)
{
    maxRecords = newMaxRecords;

    // If we're over the new limit, trim messages
    if (storedMessages.size() > maxRecords) {
        size_t numToRemove = storedMessages.size() - maxRecords;
        storedMessages.erase(storedMessages.begin(), storedMessages.begin() + numToRemove);
        logger.info("S&F: Removed %u old messages after reducing max records", (unsigned)numToRemove);
    }
}

std::string StoreForwardHistoryManager::getStatisticsJson() const
{
    char json[256];
    snprintf(json, sizeof(json), "{\"messages\":%u,\"max\":%u,\"clients\":%u,\"duplicates\":%u}", (unsigned)storedMessages.size(),
             maxRecords, (unsigned)lastRequest.size(), (unsigned)seenMessages.size());
    return std::string(json);
}

void StoreForwardHistoryManager::saveToFlash()
{
    // Call the persistence helper
    StoreForwardPersistence::saveToFlash(this);
}

void StoreForwardHistoryManager::loadFromFlash()
{
    // Call the persistence helper
    StoreForwardPersistence::loadFromFlash(this);
}

void StoreForwardHistoryManager::clearStorage()
{
    storedMessages.clear();
    seenMessages.clear();
    lastRequest.clear();
    logger.info("S&F: Storage cleared, all messages and tracking data removed");
}

const std::vector<meshtastic_MeshPacket> &StoreForwardHistoryManager::getAllStoredMessages() const
{
    return storedMessages;
}
