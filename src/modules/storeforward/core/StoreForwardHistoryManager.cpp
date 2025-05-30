#include "StoreForwardHistoryManager.h"
#include "../storage/StoreForwardPersistence.h"
#include "NodeDB.h"
#include "RTC.h"
#include "configuration.h"
#include <algorithm>

StoreForwardHistoryManager::StoreForwardHistoryManager(ILogger &logger) : logger(logger)
{
    initStorage();
    loadFromFlash();
}

StoreForwardHistoryManager::~StoreForwardHistoryManager()
{
    if (packetHistory) {
        free(packetHistory);
        packetHistory = nullptr;
    }
}

void StoreForwardHistoryManager::initStorage()
{
    // Use the correct field name 'records' instead of 'records_max'
    records = moduleConfig.store_forward.records > 0 ? moduleConfig.store_forward.records : 3000;
    logger.info("S&F: Allocating space for %u packet records", records);

    if (packetHistory) {
        free(packetHistory);
    }

    packetHistory = (PacketHistoryStruct *)calloc(records, sizeof(PacketHistoryStruct));
    if (!packetHistory) {
        logger.error("S&F: Failed to allocate memory for packet history");
        records = 0;
    } else {
        logger.info("S&F: Successfully allocated memory for packet history");
    }
}

bool StoreForwardHistoryManager::shouldStore(const meshtastic_MeshPacket &packet) const
{
    // First, log all information about the packet for debugging
    logger.info("S&F: shouldStore - Examining packet from=0x%x to=0x%x, portnum=%d, size=%d", packet.from, packet.to,
                packet.which_payload_variant == meshtastic_MeshPacket_decoded_tag ? packet.decoded.portnum : -1,
                packet.which_payload_variant == meshtastic_MeshPacket_decoded_tag ? packet.decoded.payload.size : 0);

    // Only store text messages
    if (packet.which_payload_variant == meshtastic_MeshPacket_decoded_tag &&
        packet.decoded.portnum == meshtastic_PortNum_TEXT_MESSAGE_APP && packet.decoded.payload.size > 0) {

        // Always make a copy of the message content for logging
        char msgBuf[meshtastic_Constants_DATA_PAYLOAD_LEN + 1] = {0};
        memcpy(msgBuf, packet.decoded.payload.bytes,
               std::min((size_t)packet.decoded.payload.size, (size_t)meshtastic_Constants_DATA_PAYLOAD_LEN));

        logger.info("S&F: shouldStore - Text message content: \"%s\"", msgBuf);

        // Don't store SF commands
        if (strncmp(msgBuf, "SF", 2) == 0 && (packet.decoded.payload.size == 2 || msgBuf[2] == ' ' || msgBuf[2] == '\0')) {
            logger.info("S&F: Not storing SF command");
            return false;
        }

        // Check for duplicates
        bool duplicate = isDuplicate(packet);
        logger.info("S&F: shouldStore - isDuplicate=%d", duplicate);

        if (!duplicate) {
            logger.info("S&F: Will store message: \"%s\"", msgBuf);
            return true;
        }
    }

    logger.info("S&F: Will NOT store message - not a storable text message or is duplicate");
    return false;
}

bool StoreForwardHistoryManager::isDuplicate(const meshtastic_MeshPacket &packet) const
{
    // Check for duplicates by comparing with existing messages
    for (uint32_t i = 0; i < packetHistoryTotalCount; i++) {
        if (packetHistory[i].from == packet.from && packetHistory[i].to == packet.to &&
            packet.decoded.payload.size == packetHistory[i].payload_size) {

            // Compare message content
            if (memcmp(packet.decoded.payload.bytes, packetHistory[i].payload, packet.decoded.payload.size) == 0) {
                return true;
            }
        }
    }
    return false;
}

void StoreForwardHistoryManager::record(const meshtastic_MeshPacket &packet)
{
    logger.info("S&F: RECORDING MESSAGE from=0x%x to=0x%x", packet.from, packet.to);

    // Important: We need to make a deep copy of the packet to store
    meshtastic_MeshPacket packetCopy = packet;

    // Set the storage time
    packetCopy.rx_time = getTime();

    // Create a record for this packet
    if (packetHistoryTotalCount < records) {
        // Convert the MeshPacket to a PacketHistoryStruct
        PacketHistoryStruct *hist = &packetHistory[packetHistoryTotalCount];
        hist->time = packetCopy.rx_time;
        hist->from = packetCopy.from;
        hist->to = packetCopy.to;

        // Copy the payload data
        if (packetCopy.which_payload_variant == meshtastic_MeshPacket_decoded_tag) {
            memcpy(hist->payload, packetCopy.decoded.payload.bytes,
                   std::min((size_t)packetCopy.decoded.payload.size, sizeof(hist->payload)));
            hist->payload_size = packetCopy.decoded.payload.size;
        }

        packetHistoryTotalCount++;
        logger.info("S&F: Successfully stored message - total count: %u", packetHistoryTotalCount);

        // Save to flash if we're configured for immediate flushing
        if (flushImmediately) {
            logger.info("S&F: Immediate flush enabled, saving to flash");
            StoreForwardPersistence::saveToFlash(this);
        }
    } else {
        logger.error("S&F: History storage FULL! Can't store message");
    }
}

std::vector<meshtastic_MeshPacket> StoreForwardHistoryManager::getMessagesForNode(NodeNum dest, uint32_t sinceTime) const
{
    std::vector<meshtastic_MeshPacket> result;

    // Start from the last requested index to save processing time
    uint32_t startIdx = getLastRequestIndex(dest);

    for (uint32_t i = startIdx; i < packetHistoryTotalCount; i++) {
        const auto &storedPacket = packetHistory[i];

        // Include messages that are:
        // 1. Newer than the sinceTime parameter
        // 2. Not from the requesting node itself
        // 3. Either broadcasts or addressed to this node
        if (storedPacket.time > sinceTime && storedPacket.from != dest &&
            (storedPacket.to == NODENUM_BROADCAST || storedPacket.to == dest)) {

            // Convert PacketHistoryStruct to MeshPacket
            meshtastic_MeshPacket packet = {};
            packet.from = storedPacket.from;
            packet.to = storedPacket.to;
            packet.rx_time = storedPacket.time;
            packet.which_payload_variant = meshtastic_MeshPacket_decoded_tag;
            packet.decoded.portnum = meshtastic_PortNum_TEXT_MESSAGE_APP;
            packet.decoded.payload.size = storedPacket.payload_size;
            memcpy(packet.decoded.payload.bytes, storedPacket.payload, storedPacket.payload_size);

            result.push_back(packet);
        }
    }

    return result;
}

uint32_t StoreForwardHistoryManager::getNumAvailablePackets(NodeNum dest, uint32_t sinceTime) const
{
    uint32_t count = 0;
    uint32_t startIdx = getLastRequestIndex(dest);

    logger.debug("S&F: Checking for packets for node 0x%x since time %u, starting at index %u", dest, sinceTime, startIdx);

    if (!packetHistory || packetHistoryTotalCount == 0) {
        logger.debug("S&F: No packets in history (history=%p, count=%u)", packetHistory, packetHistoryTotalCount);
        return 0;
    }

    for (uint32_t i = 0; i < packetHistoryTotalCount; i++) {
        const auto &packet = packetHistory[i];

        bool shouldInclude = false;
        if (packet.time > sinceTime && packet.from != dest && (packet.to == NODENUM_BROADCAST || packet.to == dest)) {
            shouldInclude = true;
            count++;
        }

        logger.debug("S&F: Packet %u: from=0x%x, to=0x%x, time=%u, include=%d", i, packet.from, packet.to, packet.time,
                     shouldInclude);
    }

    logger.debug("S&F: Found %u packets matching criteria for node 0x%x", count, dest);
    return count;
}

void StoreForwardHistoryManager::updateLastRequest(NodeNum dest, uint32_t index)
{
    // Ensure index is valid
    if (index <= packetHistoryTotalCount) {
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
    return packetHistoryTotalCount;
}

uint32_t StoreForwardHistoryManager::getMaxRecords() const
{
    return records;
}

void StoreForwardHistoryManager::clearStorage()
{
    packetHistoryTotalCount = 0;
    lastRequest.clear();
    logger.info("S&F: Storage cleared, all messages and tracking data removed");
}

void StoreForwardHistoryManager::saveToFlash()
{
    logger.debug("S&F: saveToFlash - Starting save operation, packetHistoryTotalCount=%d", packetHistoryTotalCount);
    // Call the concrete implementation directly
    StoreForwardPersistence::saveToFlash(this);
}

void StoreForwardHistoryManager::loadFromFlash()
{
    // Call the concrete implementation directly
    StoreForwardPersistence::loadFromFlash(this);
}

std::string StoreForwardHistoryManager::getStatisticsJson() const
{
    char json[256];
    snprintf(json, sizeof(json), "{\"messages\":%u,\"max\":%u,\"clients\":%u}", (unsigned)packetHistoryTotalCount, records,
             (unsigned)lastRequest.size());
    return std::string(json);
}
