/**
 * @file StoreForwardModule.cpp
 * @brief Implementation of the StoreForwardModule class.
 */

// Define that we DO NOT want to use the real FS classes
#define USE_REAL_FS_CLASSES 0

// Include our FS stub before any other includes
#include "StubFS.h"

// Now safely include Arduino.h
#include <Arduino.h>

// Standard C++ headers
#include <algorithm>     // For std::sort
#include <atomic>        // For std::atomic
#include <cstddef>       // For size_t, ptrdiff_t, etc.
#include <ctime>         // For time_t
#include <functional>    // For std::function
#include <iterator>      // For iterators
#include <map>           // For std::map
#include <memory>        // For std::unique_ptr
#include <mutex>         // For std::mutex
#include <string>        // For std::string
#include <thread>        // For std::thread
#include <unordered_map> // For std::unordered_map
#include <unordered_set> // For std::unordered_set
#include <vector>        // For std::vector

// Disable warnings about possible issues with file system includes
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wredundant-decls"

// Include the rest of the module headers
#include "MeshService.h"
#include "NodeDB.h"
#include "RTC.h"
#include "Router.h"
#include "StoreForwardModule.h"
#include "StoreForwardPersistence.h"
#include "StoreForwardProtobufUtils.h"
#include "Throttle.h"
#include "airtime.h"
#include "configuration.h"
#include "memGet.h"
#include "mesh-pb-constants.h"
#include "modules/ModuleDev.h"

// Only after including all headers, add files related to the file system
#include "FSCommon.h"
#include "SafeFile.h"

// Restore warnings
#pragma GCC diagnostic pop

// Make sure LittleFS buffer cache is flushed to prevent data loss
#ifdef ARCH_STM32WL
// Override just what we need from LittleFS
namespace InternalFS
{
void flushCache();
}
#define FLUSH_FS_CACHE() InternalFS::flushCache()
#else
#define FLUSH_FS_CACHE() /* Not needed */
#endif

StoreForwardModule *storeForwardModule;

/**
 * Constructor
 */
StoreForwardModule::StoreForwardModule()
    : ProtobufModule("storeforward", meshtastic_PortNum_STORE_FORWARD_APP, &meshtastic_StoreAndForward_msg),
      concurrency::OSThread("StoreForward")
{
    // Initialize the module as a server if enabled in configuration
    is_server = moduleConfig.store_forward.enabled;
    setIntervalFromNow(10 * 1000); // Check every 10 seconds initially
}

/**
 * Record a received message ID to avoid duplicates in future
 * @param messageId The message ID to record
 */
void MessageHistory::recordReceivedMessage(uint32_t messageId)
{
    if (receivedMessageIds.find(messageId) == receivedMessageIds.end()) {
        receivedMessageIds.insert(messageId);
        changed = true;

        // Track highest ID for better history management
        if (messageId > highestKnownId) {
            highestKnownId = messageId;
        }

        // Prune old message IDs when the history gets too large to manage memory efficiently
        if (receivedMessageIds.size() > 1000) {
            // Use a smarter pruning strategy that respects message ID continuity
            std::vector<uint32_t> sortedIds(receivedMessageIds.begin(), receivedMessageIds.end());
            std::sort(sortedIds.begin(), sortedIds.end());

            // Keep only the highest ~25% of IDs to maintain recent history
            size_t keepCount = sortedIds.size() / 4;
            keepCount = std::max(keepCount, (size_t)1); // Always keep at least one ID

            receivedMessageIds.clear();
            for (size_t i = sortedIds.size() - keepCount; i < sortedIds.size(); i++) {
                receivedMessageIds.insert(sortedIds[i]);
            }

            // Always keep the highest known ID
            receivedMessageIds.insert(highestKnownId);

            LOG_INFO("S&F: Message history pruned, kept %d recent IDs including highest ID %u", (int)receivedMessageIds.size(),
                     highestKnownId);
        }
    }
}

int32_t StoreForwardModule::runOnce()
{
#if defined(ARCH_ESP32) || defined(ARCH_PORTDUINO)
    if (moduleConfig.store_forward.enabled && is_server) {
        // Send out the message queue.
        if (this->busy) {
            // Only send packets when channel utilization is below threshold to avoid congestion
            // Also respect the maximum number of historical messages we want to send at once
            if (airTime->isTxAllowedChannelUtil(true) && this->requestCount < this->historyReturnMax) {
                if (!storeForwardModule->sendPayload(this->busyTo, this->last_time)) {
                    // No more messages to send, reset state
                    this->requestCount = 0;
                    this->busy = false;
                }
            }
        } else if (this->heartbeat && (!Throttle::isWithinTimespanMs(lastHeartbeat, heartbeatInterval * 1000)) &&
                   airTime->isTxAllowedChannelUtil(true)) {
            // Send periodic heartbeat so clients know this S&F node is active
            lastHeartbeat = millis();
            LOG_INFO("Send heartbeat");
            meshtastic_StoreAndForward sf = meshtastic_StoreAndForward_init_zero;
            sf.rr = meshtastic_StoreAndForward_RequestResponse_ROUTER_HEARTBEAT;
            sf.which_variant = meshtastic_StoreAndForward_heartbeat_tag;
            sf.variant.heartbeat.period = heartbeatInterval;
            sf.variant.heartbeat.secondary = 0; // TODO we always have one primary router for now
            storeForwardModule->sendMessage(NODENUM_BROADCAST, sf);
        }
        return (this->packetTimeMax);
    }
#endif
    return disable();
}

/**
 * Populates the PSRAM with data to be sent later when a device is out of range.
 * This is a key function for S&F - allocates memory for message storage.
 */
void StoreForwardModule::populatePSRAM()
{
    /*
    For PSRAM usage, see:
        https://learn.upesy.com/en/programmation/psram.html#psram-tab
    */

    LOG_DEBUG("Before PSRAM init: heap %d/%d PSRAM %d/%d", memGet.getFreeHeap(), memGet.getHeapSize(), memGet.getFreePsram(),
              memGet.getPsramSize());

    /* Calculate maximum message storage capacity - use 3/4 of available PSRAM
       This ensures we have enough memory for messages while leaving room for other operations
    */
    uint32_t numberOfPackets =
        (this->records ? this->records : (((memGet.getFreePsram() / 4) * 3) / sizeof(PacketHistoryStruct)));
    this->records = numberOfPackets;
#if defined(ARCH_ESP32)
    // Use special PSRAM allocation on ESP32 for more efficient memory usage
    this->packetHistory = static_cast<PacketHistoryStruct *>(ps_calloc(numberOfPackets, sizeof(PacketHistoryStruct)));
#elif defined(ARCH_PORTDUINO)
    // Standard allocation for other platforms
    this->packetHistory = static_cast<PacketHistoryStruct *>(calloc(numberOfPackets, sizeof(PacketHistoryStruct)));

#endif

    LOG_DEBUG("After PSRAM init: heap %d/%d PSRAM %d/%d", memGet.getFreeHeap(), memGet.getHeapSize(), memGet.getFreePsram(),
              memGet.getPsramSize());
    LOG_DEBUG("numberOfPackets for packetHistory - %u", numberOfPackets);

    // Load previously saved message history from flash to restore state after reboot
    StoreForwardPersistence::loadFromFlash(this);
}

/**
 * Sends messages from the message history to the specified recipient.
 * This function initiates the S&F history transfer process.
 *
 * @param sAgo The number of seconds ago from which to start sending messages.
 * @param to The recipient ID to send the messages to.
 */
void StoreForwardModule::historySend(uint32_t secAgo, uint32_t to)
{
    // Calculate cutoff time for historical message retrieval
    this->last_time = getTime() < secAgo ? 0 : getTime() - secAgo;

    // Count how many relevant messages we have for this client
    uint32_t queueSize = getNumAvailablePackets(to, last_time);

    // Cap number of messages to our configured maximum
    if (queueSize > this->historyReturnMax)
        queueSize = this->historyReturnMax;

    if (queueSize) {
        LOG_INFO("S&F - Send %u message(s)", queueSize);
        this->busy = true; // Mark as busy so runOnce() will handle message sending
        this->busyTo = to;
    } else {
        LOG_INFO("S&F - No history");
    }

    // Prepare and send history metadata response to client
    meshtastic_StoreAndForward sf = meshtastic_StoreAndForward_init_zero;
    sf.rr = meshtastic_StoreAndForward_RequestResponse_ROUTER_HISTORY;
    sf.which_variant = meshtastic_StoreAndForward_history_tag;
    sf.variant.history.history_messages = queueSize;
    sf.variant.history.window = secAgo * 1000;
    sf.variant.history.last_request = lastRequest[to];

    storeForwardModule->sendMessage(to, sf);
    setIntervalFromNow(this->packetTimeMax); // Delay start of sending payloads
}

/**
 * Returns the number of available packets in the message history for a specified destination node.
 * This function determines how many messages should be sent to a client.
 *
 * @param dest The destination node number.
 * @param last_time The relative time to start sending messages from.
 * @return The number of available packets in the message history.
 */
uint32_t StoreForwardModule::getNumAvailablePackets(NodeNum dest, uint32_t last_time)
{
    uint32_t count = 0;

    // Initialize tracking for this client if it's their first request
    if (lastRequest.find(dest) == lastRequest.end()) {
        lastRequest.emplace(dest, 0);
    }

    // Count messages that match our criteria
    for (uint32_t i = lastRequest[dest]; i < this->packetHistoryTotalCount; i++) {
        if (this->packetHistory[i].time && (this->packetHistory[i].time > last_time)) {
            // Only count messages not from the requesting client AND
            // either broadcast messages or messages specifically for this client
            if (this->packetHistory[i].from != dest &&
                (this->packetHistory[i].to == NODENUM_BROADCAST || this->packetHistory[i].to == dest)) {
                count++;
            }
        }
    }
    return count;
}

/**
 * Allocates a mesh packet for sending to the phone.
 * This is how stored messages are delivered to the local phone app.
 *
 * @return A pointer to the allocated mesh packet or nullptr if none is available.
 */
meshtastic_MeshPacket *StoreForwardModule::getForPhone()
{
    if (moduleConfig.store_forward.enabled && is_server) {
        NodeNum to = nodeDB->getNodeNum(); // Local node number

        // Start a new message delivery session if not already busy
        if (!this->busy) {
            // Check if we have any historical messages to send to the phone
            uint32_t histSize = getNumAvailablePackets(to, 0); // No time limit for phone
            if (histSize) {
                this->busy = true;
                this->busyTo = to;
            } else {
                return nullptr;
            }
        }

        // Continue delivery of messages to phone if we're in a session
        if (this->busy && this->busyTo == to) {
            meshtastic_MeshPacket *p = preparePayload(to, 0, true); // Special flag for phone delivery
            if (!p)                                                 // No more messages to send
                this->busy = false;
            return p;
        }
    }
    return nullptr;
}

/**
 * Adds a mesh packet to the history buffer for store-and-forward functionality.
 * This is the core storage function that saves messages for later forwarding.
 *
 * @param mp The mesh packet to add to the history buffer.
 */
void StoreForwardModule::historyAdd(const meshtastic_MeshPacket &mp)
{
    // Special logging for encrypted messages
    if (mp.which_payload_variant == meshtastic_MeshPacket_encrypted_tag) {
        LOG_INFO("SF storing encrypted message from=0x%08x, to=0x%08x, id=0x%08x, size=%d bytes", mp.from, mp.to, mp.id,
                 mp.encrypted.size);

        // Debug logging to show message content in hex format
        char hexbuf[48] = {0};
        for (int i = 0; i < min(16, (int)mp.encrypted.size); i++) {
            sprintf(hexbuf + strlen(hexbuf), "%02x ", mp.encrypted.bytes[i]);
        }
        LOG_DEBUG("SF stored encrypted content: %s%s", hexbuf, mp.encrypted.size > 16 ? "..." : "");
    }
    const auto &p = mp.decoded;

    // Handle buffer wraparound when storage is full
    if (this->packetHistoryTotalCount == this->records) {
        LOG_WARN("S&F - PSRAM Full. Starting overwrite");
        this->packetHistoryTotalCount = 0;
        // Reset all client tracking to prevent access to outdated indices
        for (auto &i : lastRequest) {
            i.second = 0; // Clear the last request index for each client device
        }
    }

    // Store message details in our history buffer
    this->packetHistory[this->packetHistoryTotalCount].time = getTime();
    this->packetHistory[this->packetHistoryTotalCount].to = mp.to;
    this->packetHistory[this->packetHistoryTotalCount].channel = mp.channel;
    this->packetHistory[this->packetHistoryTotalCount].from = getFrom(&mp);
    this->packetHistory[this->packetHistoryTotalCount].id = mp.id;
    this->packetHistory[this->packetHistoryTotalCount].reply_id = p.reply_id;
    this->packetHistory[this->packetHistoryTotalCount].emoji = (bool)p.emoji;
    this->packetHistory[this->packetHistoryTotalCount].payload_size = p.payload.size;
    memcpy(this->packetHistory[this->packetHistoryTotalCount].payload, p.payload.bytes, meshtastic_Constants_DATA_PAYLOAD_LEN);

    this->packetHistoryTotalCount++;

    // Save immediately to flash after each message
    LOG_INFO("S&F: Saving message immediately to flash");
    StoreForwardPersistence::saveToFlash(this);
    FLUSH_FS_CACHE(); // Ensure data is physically written to flash
}

/**
 * Sends a payload to a specified destination node using the store and forward mechanism.
 * Used to send a single historical message from the buffer.
 *
 * @param dest The destination node number.
 * @param last_time The relative time to start sending messages from.
 * @return True if a packet was successfully sent, false otherwise.
 */
bool StoreForwardModule::sendPayload(NodeNum dest, uint32_t last_time)
{
    // Attempt to prepare a historical message for sending
    meshtastic_MeshPacket *p = preparePayload(dest, last_time);
    if (p) {
        LOG_INFO("Send S&F Payload");
        service->sendToMesh(p);
        this->requestCount++; // Track how many messages we've sent in this batch
        return true;
    }
    return false; // No more messages to send
}

/**
 * Prepares a payload to be sent to a specified destination node from the S&F packet history.
 * This complex function handles retrieval from storage with deduplication logic.
 *
 * @param dest The destination node number.
 * @param last_time The relative time to start sending messages from.
 * @param local If true, prepare for local delivery (to phone), otherwise for mesh network. Default is false.
 * @return A pointer to the prepared mesh packet or nullptr if none is available.
 */
meshtastic_MeshPacket *StoreForwardModule::preparePayload(NodeNum dest, uint32_t last_time, bool local)
{
    // Track message IDs already sent to each client to avoid duplicates
    static std::unordered_map<NodeNum, std::unordered_set<uint32_t>> sentMessagesIds;

    // Priority handling: If we have specifically tracked missing messages for this client, use those first
    if (!local && missingMessages.find(dest) != missingMessages.end() && !missingMessages[dest].empty()) {
        // Get the next message index from the missing messages list
        uint32_t msgIndex = missingMessages[dest].front();
        missingMessages[dest].erase(missingMessages[dest].begin());

        // Note when we've sent the last missing message
        if (missingMessages[dest].empty()) {
            LOG_INFO("S&F: Last missing message sent to client 0x%x", dest);
        }

        // Create a packet with the message data
        uint32_t uniqueMessageId = this->packetHistory[msgIndex].id;

        // Extra safety check to avoid duplicates
        if (sentMessagesIds.find(dest) != sentMessagesIds.end() &&
            sentMessagesIds[dest].find(uniqueMessageId) != sentMessagesIds[dest].end()) {
            LOG_DEBUG("S&F: Skipping duplicate message id %u to node 0x%x", uniqueMessageId, dest);
            return preparePayload(dest, last_time, local); // Recursively try next message
        }

        // Record that we've sent this message to prevent future duplicates
        if (sentMessagesIds.find(dest) == sentMessagesIds.end()) {
            sentMessagesIds[dest] = std::unordered_set<uint32_t>();
        }
        sentMessagesIds[dest].insert(uniqueMessageId);

        // Create and return the packet
        meshtastic_MeshPacket *p = router->allocForSending();
        p->to = dest;
        p->from = this->packetHistory[msgIndex].from;
        p->id = uniqueMessageId;
        p->channel = this->packetHistory[msgIndex].channel;
        p->decoded.reply_id = this->packetHistory[msgIndex].reply_id;
        p->rx_time = this->packetHistory[msgIndex].time;
        p->decoded.emoji = (uint32_t)this->packetHistory[msgIndex].emoji;
        p->want_ack = true; // Request acknowledgment for missing messages for reliability
        p->decoded.want_response = false;

        // Prepare the payload using Store & Forward protocol format
        meshtastic_StoreAndForward sf = meshtastic_StoreAndForward_init_zero;
        sf.which_variant = meshtastic_StoreAndForward_text_tag;
        sf.variant.text.size = this->packetHistory[msgIndex].payload_size;

        // Fix calls to copyToProtobufBytes function by removing the address operator &
        if (this->packetHistory[msgIndex].payload_size > 0) {
            copyToProtobufBytes(sf.variant.text.bytes, this->packetHistory[msgIndex].payload,
                                this->packetHistory[msgIndex].payload_size);
        }

        // Set appropriate routing flag depending on original message type
        if (this->packetHistory[msgIndex].to == NODENUM_BROADCAST) {
            sf.rr = meshtastic_StoreAndForward_RequestResponse_ROUTER_TEXT_BROADCAST;
        } else {
            sf.rr = meshtastic_StoreAndForward_RequestResponse_ROUTER_TEXT_DIRECT;
        }

        // Encode the StoreAndForward wrapper message
        p->decoded.payload.size =
            pb_encode_to_bytes(p->decoded.payload.bytes, sizeof(p->decoded.payload.bytes), &meshtastic_StoreAndForward_msg, &sf);

        LOG_DEBUG("S&F: Sending missing message %u to node 0x%x (%d remaining)", uniqueMessageId, dest,
                  (int)missingMessages[dest].size());

        return p;
    }

    // Standard flow for regular history or phone requests
    // Track known message IDs for each client to avoid sending duplicates
    static std::unordered_map<NodeNum, std::unordered_set<uint32_t>> clientKnownMessages;

    // Initialize tracking for new clients
    if (sentMessagesIds.find(dest) == sentMessagesIds.end()) {
        sentMessagesIds[dest] = std::unordered_set<uint32_t>();
    }

    // Scan through history starting from client's last known position
    for (uint32_t i = lastRequest[dest]; i < this->packetHistoryTotalCount; i++) {
        if (this->packetHistory[i].time && (this->packetHistory[i].time > last_time)) {
            // Client is only interested in packets not from itself and only in broadcast packets or packets addressed to it
            if (this->packetHistory[i].from != dest &&
                (this->packetHistory[i].to == NODENUM_BROADCAST || this->packetHistory[i].to == dest)) {

                // Avoid sending duplicate messages to the same client
                uint32_t uniqueMessageId = this->packetHistory[i].id;
                if (sentMessagesIds[dest].find(uniqueMessageId) != sentMessagesIds[dest].end()) {
                    LOG_DEBUG("S&F: Skipping duplicate message id %u to node 0x%x", uniqueMessageId, dest);
                    continue; // Skip duplicate
                }

                // Check if client already knows this message (from their history request)
                if (clientKnownMessages.find(dest) != clientKnownMessages.end() &&
                    clientKnownMessages[dest].find(uniqueMessageId) != clientKnownMessages[dest].end()) {
                    LOG_DEBUG("S&F: Skipping message id %u already known to client 0x%x", uniqueMessageId, dest);
                    sentMessagesIds[dest].insert(uniqueMessageId); // Mark as sent so we don't check again
                    continue;                                      // Skip known message
                }

                // Add the ID to the set of sent messages
                sentMessagesIds[dest].insert(uniqueMessageId);

                // Memory management: If tracking too many message IDs, clean up old entries
                const size_t MAX_TRACKING_IDS = 1000;
                if (sentMessagesIds[dest].size() > MAX_TRACKING_IDS) {
                    LOG_INFO("S&F: Cleaning message history tracking for node 0x%x", dest);

                    // Smart cleanup: keep most recent entries instead of clearing all
                    if (sentMessagesIds[dest].size() > MAX_TRACKING_IDS * 0.8) { // Keep 80% newest entries
                        std::vector<uint32_t> sortedIds;
                        sortedIds.reserve(sentMessagesIds[dest].size());
                        for (const auto &id : sentMessagesIds[dest]) {
                            sortedIds.push_back(id);
                        }

                        // Sort IDs, assuming newer IDs are larger values
                        std::sort(sortedIds.begin(), sortedIds.end(), std::greater<uint32_t>());

                        // Keep only the newest entries (80% of max capacity)
                        size_t keepCount = MAX_TRACKING_IDS * 0.8;
                        std::unordered_set<uint32_t> newSet;
                        for (size_t i = 0; i < keepCount && i < sortedIds.size(); i++) {
                            newSet.insert(sortedIds[i]);
                        }

                        sentMessagesIds[dest] = std::move(newSet);
                        LOG_INFO("S&F: Kept %u newest message IDs for node 0x%x", (unsigned)sentMessagesIds[dest].size(), dest);
                    } else {
                        sentMessagesIds[dest].clear(); // Fall back to clearing if vectorization would be inefficient
                    }
                }

                // Create the packet for sending the historical message
                meshtastic_MeshPacket *p = router->allocForSending();
                p->to = local ? this->packetHistory[i].to : dest; // PhoneAPI can handle original `to`
                p->from = this->packetHistory[i].from;
                p->id = this->packetHistory[i].id;
                p->channel = this->packetHistory[i].channel;
                p->decoded.reply_id = this->packetHistory[i].reply_id;
                p->rx_time = this->packetHistory[i].time;
                p->decoded.emoji = (uint32_t)this->packetHistory[i].emoji;

                // Enable acknowledgment for network delivery but not for local phone delivery
                p->want_ack = !local;
                p->decoded.want_response = false;

                // Phone gets raw message, network gets S&F protocol format
                if (local) { // PhoneAPI gets normal TEXT_MESSAGE_APP
                    p->decoded.portnum = meshtastic_PortNum_TEXT_MESSAGE_APP;
                    memcpy(p->decoded.payload.bytes, this->packetHistory[i].payload, this->packetHistory[i].payload_size);
                    p->decoded.payload.size = this->packetHistory[i].payload_size;
                } else {
                    meshtastic_StoreAndForward sf = meshtastic_StoreAndForward_init_zero;
                    sf.which_variant = meshtastic_StoreAndForward_text_tag;
                    sf.variant.text.size = this->packetHistory[i].payload_size;

                    // Fix calls to copyToProtobufBytes function by removing the address operator &
                    if (this->packetHistory[i].payload_size > 0) {
                        copyToProtobufBytes(sf.variant.text.bytes, this->packetHistory[i].payload,
                                            this->packetHistory[i].payload_size);
                    }

                    // Set appropriate routing flag
                    if (this->packetHistory[i].to == NODENUM_BROADCAST) {
                        sf.rr = meshtastic_StoreAndForward_RequestResponse_ROUTER_TEXT_BROADCAST;
                    } else {
                        sf.rr = meshtastic_StoreAndForward_RequestResponse_ROUTER_TEXT_DIRECT;
                    }

                    // Encode the StoreAndForward wrapper message
                    p->decoded.payload.size = pb_encode_to_bytes(p->decoded.payload.bytes, sizeof(p->decoded.payload.bytes),
                                                                 &meshtastic_StoreAndForward_msg, &sf);
                }

                // Update client's position in history
                lastRequest[dest] = i + 1; // Update the last request index for the client device
                LOG_INFO("S&F: Prepared message %u for node 0x%x with ack enabled", uniqueMessageId, dest);

                return p;
            }
        }
    }
    return nullptr; // No suitable messages found
}

/**
 * Sends a message to a specified destination node using the store and forward protocol.
 * Used for sending control messages or wrapper messages.
 *
 * @param dest The destination node number.
 * @param payload The message payload to be sent.
 */
void StoreForwardModule::sendMessage(NodeNum dest, const meshtastic_StoreAndForward &payload)
{
    // Create a mesh packet containing the S&F protobuf message
    meshtastic_MeshPacket *p = allocDataProtobuf(payload);
    p->to = dest;
    p->priority = meshtastic_MeshPacket_Priority_BACKGROUND; // Low priority to avoid interfering with critical traffic

    // Let's assume that if the server received the S&F request that the client is in range.
    //   TODO: Make this configurable.
    p->want_ack = false;
    p->decoded.want_response = false;

    service->sendToMesh(p);
}

/**
 * Sends a store-and-forward message to the specified destination node.
 * Specialized version for sending simple request/response messages.
 *
 * @param dest The destination node number.
 * @param rr The store-and-forward request/response message to send.
 */
void StoreForwardModule::sendMessage(NodeNum dest, meshtastic_StoreAndForward_RequestResponse rr)
{
    // Create minimal S&F message with just the request/response code
    meshtastic_StoreAndForward sf = meshtastic_StoreAndForward_init_zero;
    sf.rr = rr;

    // Selectively enable acknowledgments only for critical control messages
    bool requireAck = false;

    switch (rr) {
    case meshtastic_StoreAndForward_RequestResponse_ROUTER_ERROR:
    case meshtastic_StoreAndForward_RequestResponse_ROUTER_BUSY:
    case meshtastic_StoreAndForward_RequestResponse_ROUTER_HISTORY:
        requireAck = true; // These messages are critical to deliver
        break;
    default:
        requireAck = false; // Other messages can be lost
        break;
    }

    meshtastic_MeshPacket *p = allocDataProtobuf(sf);
    p->to = dest;
    p->priority = meshtastic_MeshPacket_Priority_BACKGROUND;
    p->want_ack = requireAck;
    p->decoded.want_response = false;

    service->sendToMesh(p);
}

/**
 * Makes the module temporarily ignore incoming messages, used during certain operations
 * like sending history to avoid processing our own outgoing messages.
 *
 * @param ignore If true, ignore incoming messages; otherwise, process them normally.
 */
void StoreForwardModule::setIgnoreIncoming(bool ignore)
{
    // Set flag to temporarily block message processing
    // This is important when sending large message batches to prevent reprocessing
    this->ignoreIncoming = ignore;
}

/**
 * Handles a received mesh packet, potentially storing it for later forwarding.
 * This is the main entry point for processing incoming messages.
 *
 * @param mp The received mesh packet.
 * @return A `ProcessMessage` indicating whether the packet was successfully handled.
 */
ProcessMessage StoreForwardModule::handleReceived(const meshtastic_MeshPacket &mp)
{
    // Skip processing if we're currently ignoring messages (during bulk operations)
    if (ignoreIncoming) {
        LOG_DEBUG("S&F: Ignoring incoming message from node 0x%x", getFrom(&mp));
        return ProcessMessage::STOP; // Ignore this message
    }

    // Process based on message type
    if (mp.decoded.portnum == meshtastic_PortNum_STORE_FORWARD_APP) {
        // Store S&F protocol messages for later forwarding
        storeForwardModule->historyAdd(mp);
        LOG_INFO("S&F stored. Message history contains %u records now", this->packetHistoryTotalCount);
    } else if (mp.decoded.portnum == meshtastic_PortNum_TEXT_MESSAGE_APP) {
        // Also handle regular text messages
        LOG_INFO("Received direct text message from 0x%x", mp.from);
    }

    return ProcessMessage::CONTINUE; // Let other modules process the message as well
}

/**
 * Handles a received protobuf message for the Store and Forward module.
 * Processes control messages like history requests, responses, etc.
 *
 * @param mp The received MeshPacket to handle.
 * @param p A pointer to the StoreAndForward object.
 * @return True if the message was successfully handled, false otherwise.
 */
bool StoreForwardModule::handleReceivedProtobuf(const meshtastic_MeshPacket &mp, meshtastic_StoreAndForward *p)
{
    if (!moduleConfig.store_forward.enabled) {
        LOG_INFO("S&F: Ignoring message (module disabled)");
        return false;
    }

    // Increment statistics counter for total requests received
    this->requestsReceived++;
    NodeNum fromNode = getFrom(&mp);
    LOG_INFO("S&F: Received command %u from node 0x%x", p->rr, fromNode);

    // Determine if we're in client mode - we are a client if we're not a server
    bool isClient = !is_server;

    switch (p->rr) {
    case meshtastic_StoreAndForward_RequestResponse_CLIENT_ERROR:
    case meshtastic_StoreAndForward_RequestResponse_CLIENT_ABORT:
        if (is_server) {
            // Client wants to abort or has an error, stop sending messages
            if ((this->busy) && (this->busyTo == fromNode)) {
                LOG_INFO("S&F: Client 0x%x requested abort/error (%u), stopping message transmission", fromNode, p->rr);
                this->requestCount = 0;
                this->busy = false;
            }
        }
        break;

    case meshtastic_StoreAndForward_RequestResponse_CLIENT_HISTORY:
        if (is_server) {
            this->historyRequests++;
            LOG_INFO("S&F: Received history request from node 0x%x", fromNode);

            // Check if we can process this request
            if (this->busy) {
                LOG_INFO("S&F: Server busy, sending error message to 0x%x", fromNode);
                storeForwardModule->sendMessage(fromNode, meshtastic_StoreAndForward_RequestResponse_ROUTER_BUSY);
            } else if (channels.isDefaultChannel(mp.channel)) {
                LOG_INFO("S&F: Request on default channel not allowed, sending error to 0x%x", fromNode);
                storeForwardModule->sendMessage(fromNode, meshtastic_StoreAndForward_RequestResponse_ROUTER_ERROR);
            } else {
                // Process the history request
                uint32_t window = historyReturnWindow * 60; // Default window in seconds

                if ((p->which_variant == meshtastic_StoreAndForward_history_tag) && (p->variant.history.window > 0)) {
                    // Client provided a custom window (in minutes)
                    window = p->variant.history.window * 60;
                    LOG_INFO("S&F: Using client-requested window of %u minutes", p->variant.history.window);
                }

                LOG_INFO("S&F: Starting history send to node 0x%x with %u second window", fromNode, window);
                storeForwardModule->historySend(window, fromNode);
            }
        }
        break;

    case meshtastic_StoreAndForward_RequestResponse_CLIENT_PING:
        if (is_server) {
            LOG_INFO("S&F: Received PING from 0x%x, responding with PONG", fromNode);
            storeForwardModule->sendMessage(fromNode, meshtastic_StoreAndForward_RequestResponse_ROUTER_PONG);
        }
        break;

    case meshtastic_StoreAndForward_RequestResponse_CLIENT_STATS:
        if (is_server) {
            LOG_INFO("S&F: Received STATS request from 0x%x", fromNode);
            if (this->busy) {
                LOG_INFO("S&F: Server busy, sending busy message to 0x%x", fromNode);
                storeForwardModule->sendMessage(fromNode, meshtastic_StoreAndForward_RequestResponse_ROUTER_BUSY);
            } else {
                LOG_INFO("S&F: Sending stats to 0x%x", fromNode);
                // Prepare stats response
                meshtastic_StoreAndForward sf = meshtastic_StoreAndForward_init_zero;
                sf.rr = meshtastic_StoreAndForward_RequestResponse_ROUTER_STATS;
                sf.which_variant = meshtastic_StoreAndForward_stats_tag;
                sf.variant.stats.messages_total = this->records;
                sf.variant.stats.messages_saved = this->packetHistoryTotalCount;
                sf.variant.stats.messages_max = this->records;
                sf.variant.stats.up_time = millis() / 1000;
                sf.variant.stats.requests = this->requestsReceived;
                sf.variant.stats.requests_history = this->historyRequests;
                sf.variant.stats.heartbeat = this->heartbeat;
                sf.variant.stats.return_max = this->historyReturnMax;
                sf.variant.stats.return_window = this->historyReturnWindow;

                storeForwardModule->sendMessage(fromNode, sf);
            }
        }
        break;

    // Handle responses from a router if we're a client
    case meshtastic_StoreAndForward_RequestResponse_ROUTER_PONG:
    case meshtastic_StoreAndForward_RequestResponse_ROUTER_HEARTBEAT:
        if (isClient) {
            // Register heartbeat info
            if (p->which_variant == meshtastic_StoreAndForward_heartbeat_tag) {
                heartbeatInterval = p->variant.heartbeat.period;
                LOG_INFO("S&F: Received heartbeat from router 0x%x with period %u seconds", fromNode, heartbeatInterval);
            } else {
                LOG_INFO("S&F: Received pong/heartbeat from router 0x%x", fromNode);
            }
            lastHeartbeat = millis();
        }
        break;

    case meshtastic_StoreAndForward_RequestResponse_ROUTER_HISTORY:
        if (isClient) {
            if (p->which_variant == meshtastic_StoreAndForward_history_tag) {
                LOG_INFO("S&F: Router 0x%x is sending %u messages from the last %u minutes", fromNode,
                         p->variant.history.history_messages, p->variant.history.window / 60000);
                // Save window info for future reference
                this->historyReturnWindow = p->variant.history.window / 60000;
            } else {
                LOG_INFO("S&F: Router 0x%x is sending messages (details not provided)", fromNode);
            }
        }
        break;

    case meshtastic_StoreAndForward_RequestResponse_ROUTER_STATS:
        if (isClient && p->which_variant == meshtastic_StoreAndForward_stats_tag) {
            LOG_INFO("S&F: Received stats from router 0x%x:", fromNode);
            LOG_INFO("  - Messages: %u/%u (saved/max)", p->variant.stats.messages_saved, p->variant.stats.messages_max);
            LOG_INFO("  - Uptime: %u seconds", p->variant.stats.up_time);
            LOG_INFO("  - Window: %u minutes, Max return: %u messages", p->variant.stats.return_window,
                     p->variant.stats.return_max);

            // Store stats data for client use
            this->records = p->variant.stats.messages_max;
            this->requestsReceived = p->variant.stats.requests;
            this->historyRequests = p->variant.stats.requests_history;
            this->heartbeat = p->variant.stats.heartbeat;
            this->historyReturnMax = p->variant.stats.return_max;
            this->historyReturnWindow = p->variant.stats.return_window;
        }
        break;

    case meshtastic_StoreAndForward_RequestResponse_ROUTER_ERROR:
    case meshtastic_StoreAndForward_RequestResponse_ROUTER_BUSY:
        if (isClient) {
            const char *errType = (p->rr == meshtastic_StoreAndForward_RequestResponse_ROUTER_ERROR) ? "ERROR" : "BUSY";
            LOG_INFO("S&F: Router 0x%x reported %s", fromNode, errType);
            // Calculate backoff time for retry based on server load
            this->retryDelay = millis() + getNumAvailablePackets(this->busyTo, this->last_time) * packetTimeMax *
                                              (p->rr == meshtastic_StoreAndForward_RequestResponse_ROUTER_ERROR ? 2 : 1);
            LOG_INFO("S&F: Will retry in %u ms", (unsigned)(this->retryDelay - millis()));
        }
        break;

    case meshtastic_StoreAndForward_RequestResponse_ROUTER_TEXT_DIRECT:
    case meshtastic_StoreAndForward_RequestResponse_ROUTER_TEXT_BROADCAST:
        // Handle historical messages being delivered to client
        if (isClient && p->which_variant == meshtastic_StoreAndForward_text_tag) {
            const char *msgType =
                (p->rr == meshtastic_StoreAndForward_RequestResponse_ROUTER_TEXT_BROADCAST) ? "broadcast" : "direct";
            LOG_INFO("S&F: Received historical %s message from router 0x%x (%d bytes)", msgType, fromNode, p->variant.text.size);
        }
        break;

    default:
        LOG_INFO("S&F: Received unhandled command type %u from 0x%x", p->rr, fromNode);
        break;
    }

    return true; // We've processed this message
}