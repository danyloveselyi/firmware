/**
 * @file StoreForwardModule.cpp
 * @brief Implementation of the StoreForwardModule class.
 */
#include "StoreForwardModule.h"
#include "MeshService.h"
#include "NodeDB.h"
#include "RTC.h"
#include "Router.h"
#include "StoreForwardPersistence.h"
#include "Throttle.h"
#include "airtime.h"
#include "configuration.h"
#include "memGet.h"
#include "mesh-pb-constants.h"
#include "mesh/generated/meshtastic/storeforward.pb.h"
#include "modules/ModuleDev.h"
#include <Arduino.h>
#include <iterator>
#include <map>

StoreForwardModule *storeForwardModule;

int32_t StoreForwardModule::runOnce()
{
#if defined(ARCH_ESP32) || defined(ARCH_PORTDUINO)
    if (moduleConfig.store_forward.enabled && is_server) {
        // Periodically log status information
        static uint32_t lastStatusLog = 0;
        if (millis() - lastStatusLog > 60000) { // Log status every minute
            lastStatusLog = millis();
            LOG_INFO("S&F Status - Server: %d, Client: %d, Busy: %d, WaitingForAck: %d, RetryCount: %d, PacketHistoryCount: %d",
                     is_server, is_client, busy, waitingForAck, messageRetryCount, packetHistoryTotalCount);
        }

        // Add debugging log to track waitingForAck state
        LOG_DEBUG("S&F - runOnce: waitingForAck=%d, busy=%d, messageRetryCount=%d, lastMessageId=0x%08x", waitingForAck, busy,
                  messageRetryCount, lastMessageId);

        // Handle message retries if we're waiting for an ACK
        if (waitingForAck && (millis() - lastSendTime > retryTimeoutMs)) {
            // Log client name for better debugging
            meshtastic_NodeInfoLite *node = nodeDB->getMeshNode(busyTo);
            const char *clientName = (node && node->has_user && node->user.long_name[0])    ? node->user.long_name
                                     : (node && node->has_user && node->user.short_name[0]) ? node->user.short_name
                                                                                            : "Unknown";

            if (messageRetryCount < maxRetryCount) {
                // Retry sending the same message
                LOG_INFO("S&F - Retrying message to %s (0x%x), attempt %d of %d", clientName, busyTo, messageRetryCount + 1,
                         maxRetryCount);

                // Check if node is still in the NodeDB
                if (!nodeDB->getMeshNode(busyTo)) {
                    LOG_WARN("S&F - Target node 0x%x is not in NodeDB! This may cause delivery issues", busyTo);
                }

                // Log routing information - without using getNexthopNum
                LOG_INFO("S&F - Attempting to retry message to 0x%x", busyTo);

                // Prepare and send the same message again (don't increment lastRequest counter)
                if (sendPayload(this->busyTo, this->last_time, true)) {
                    messageRetryCount++;
                    lastSendTime = millis();

                    // Increase the retry timeout for the next attempt (exponential backoff)
                    retryTimeoutMs = retryTimeoutMs * 2;
                    LOG_INFO("S&F - Next retry scheduled in %d ms", retryTimeoutMs);
                }
            } else {
                // We've tried enough times, give up
                LOG_INFO("S&F - Max retries reached, giving up on message to node %s (0x%x)", clientName, this->busyTo);
                waitingForAck = false;
                messageRetryCount = 0;
                this->busy = false;

                // Reset retry timeout for next message
                retryTimeoutMs = 5000; // Reset to default value
            }

            return (this->packetTimeMax);
        }

        // Send out the message queue only if not waiting for an ACK
        if (this->busy && !waitingForAck) {
            // Add more detailed logging about transmission conditions
            LOG_INFO("S&F - Transmission check: busy=%d, waitingForAck=%d, channelUtil=%.2f%%, requestCount=%d/%d", this->busy,
                     waitingForAck, airTime->channelUtilizationPercent(), this->requestCount, this->historyReturnMax);

            // Only send packets if the channel is less than 40% utilized (increased from 25%)
            // and until historyReturnMax
            if (airTime->isTxAllowedChannelUtil(false) && this->requestCount < this->historyReturnMax) {
                LOG_INFO("S&F - Attempting to send payload to client 0x%x", this->busyTo);
                if (!sendPayload(this->busyTo, this->last_time, false)) {
                    this->requestCount = 0;
                    this->busy = false;
                    LOG_INFO("S&F - Finished sending to client 0x%x", this->busyTo);
                }
            } else {
                LOG_WARN("S&F - Transmission blocked: channelUtil=%.2f%%, requestCount=%d/%d",
                         airTime->channelUtilizationPercent(), this->requestCount, this->historyReturnMax);

                LOG_INFO("S&F - Transmission state details: busy=%d, waitingForAck=%d, lastMessageId=0x%08x, lastSendTime=%lu, "
                         "now=%lu",
                         this->busy, this->waitingForAck, this->lastMessageId, this->lastSendTime, millis());

                LOG_INFO("S&F - Retry count: %d, Retry timeout: %d ms", this->messageRetryCount, this->retryTimeoutMs);

                if (!waitingForAck && this->requestCount >= this->historyReturnMax) {
                    LOG_WARN("S&F - Max requests reached without waitingForAck. Forcing reset.");
                    this->requestCount = 0;
                    this->busy = false;
                }
            }
        } else if (this->heartbeat && (!Throttle::isWithinTimespanMs(lastHeartbeat, heartbeatInterval * 1000)) &&
                   airTime->isTxAllowedChannelUtil(false)) {
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
    } else if (moduleConfig.store_forward.enabled) {
        // If we're a client, log status periodically
        static uint32_t lastClientStatusLog = 0;
        if (millis() - lastClientStatusLog > 300000) { // Every 5 minutes
            lastClientStatusLog = millis();
            LOG_INFO("S&F Client Status - Running: %d, Client mode: %d", moduleConfig.store_forward.enabled, is_client);
        }
    }
#endif
    return disable();
}

// Add implementation of sendPayload to properly send messages
bool StoreForwardModule::sendPayload(NodeNum dest, uint32_t last_time, bool isRetry)
{
    // Log destination node information
    meshtastic_NodeInfoLite *node = nodeDB->getMeshNode(dest);
    const char *clientName = (node && node->has_user && node->user.long_name[0])    ? node->user.long_name
                             : (node && node->has_user && node->user.short_name[0]) ? node->user.short_name
                                                                                    : "Unknown";

    LOG_INFO("S&F - sendPayload: Preparing to send to %s (0x%x), last_time=%u, isRetry=%d", clientName, dest, last_time, isRetry);

    // Add logging to show channel utilization before attempting to send
    LOG_INFO("S&F - Channel utilization: %.2f%%, TX allowed: %s", airTime->channelUtilizationPercent(),
             airTime->isTxAllowedChannelUtil(false) ? "yes" : "no");

    // Log node status from NodeDB
    if (node) {
        LOG_INFO("S&F - Target node 0x%x is known in NodeDB", dest);
    } else {
        LOG_WARN("S&F - Target node 0x%x is not in NodeDB! This may cause delivery issues", dest);
    }

    // Prepare the payload from message history
    meshtastic_MeshPacket *p = preparePayload(dest, last_time, false, isRetry);

    if (p) {
        // Log success - we have a message to send
        LOG_INFO("S&F - Sending payload to %s (0x%x), id=0x%08x", clientName, dest, p->id);

        // Make sure want_response is false to avoid unnecessary responses
        p->decoded.want_response = false;

        // Store the message ID for ACK tracking
        lastMessageId = p->id;

        // Enable ACK request for store & forward messages
        p->want_ack = true;
        LOG_INFO("S&F - Setting want_ack=true for message id=0x%08x", p->id);

        // Set flag to indicate we're waiting for an ACK (only for non-retry sends)
        if (!isRetry) {
            waitingForAck = true;
            messageRetryCount = 0;
            LOG_INFO("S&F - Set waitingForAck=true for message id=0x%08x", p->id);
        }

        // Record the time when we sent this message
        lastSendTime = millis();

        // Try to send the message
        LOG_INFO("S&F - Before sendToMesh: want_ack=%d, waitingForAck=%d for message id=0x%08x", p->want_ack ? 1 : 0,
                 waitingForAck ? 1 : 0, p->id);

        service->sendToMesh(p);

        LOG_INFO("S&F - Sent packet to %s (0x%x) (attempt %d), waitingForAck=%d", clientName, dest, messageRetryCount + 1,
                 waitingForAck);

        // Ensure the waitingForAck flag sticks by explicitly setting it again
        if (!isRetry) {
            waitingForAck = true; // Reinforce in case it got changed during sendToMesh
            LOG_INFO("S&F - Confirming waitingForAck=true after send");
        }

        // Increment the request counter for tracking how many messages we've sent
        this->requestCount++;
        return true;
    }

    LOG_INFO("S&F - No payload prepared for client %s (0x%x), nothing to send", clientName, dest);
    return false;
}

/**
 * Populates the PSRAM with data to be sent later when a device is out of range.
 */
void StoreForwardModule::populatePSRAM()
{
    LOG_INFO("S&F - Populating PSRAM storage...");

    /*
    For PSRAM usage, see:
        https://learn.upesy.com/en/programmation/psram.html#psram-tab
    */
    LOG_INFO("Before PSRAM init: heap %d/%d PSRAM %d/%d", memGet.getFreeHeap(), memGet.getHeapSize(), memGet.getFreePsram(),
             memGet.getPsramSize());

    /* Use a maximum of 3/4 the available PSRAM unless otherwise specified.
        Note: This needs to be done after every thing that would use PSRAM
    */
    uint32_t numberOfPackets =
        (this->records ? this->records : (((memGet.getFreePsram() / 4) * 3) / sizeof(PacketHistoryStruct)));
    this->records = numberOfPackets;
    LOG_INFO("S&F - Allocating space for %d packet records", numberOfPackets);

#if defined(ARCH_ESP32)
    LOG_INFO("S&F - Using ESP32 ps_calloc for PSRAM allocation");
    this->packetHistory = static_cast<PacketHistoryStruct *>(ps_calloc(numberOfPackets, sizeof(PacketHistoryStruct)));
#elif defined(ARCH_PORTDUINO)
    LOG_INFO("S&F - Using standard calloc for allocation (Portduino)");
    this->packetHistory = static_cast<PacketHistoryStruct *>(calloc(numberOfPackets, sizeof(PacketHistoryStruct)));
#endif

    if (this->packetHistory == nullptr) {
        LOG_ERROR("S&F - FAILED to allocate memory for packet history!");
    } else {
        LOG_INFO("S&F - Successfully allocated memory for packet history");
    }

    LOG_INFO("After PSRAM init: heap %d/%d PSRAM %d/%d", memGet.getFreeHeap(), memGet.getHeapSize(), memGet.getFreePsram(),
             memGet.getPsramSize());
    LOG_INFO("numberOfPackets for packetHistory - %u", numberOfPackets);

    // Add loading history from flash memory
    LOG_INFO("S&F - Loading history from flash storage...");
    StoreForwardPersistence::loadFromFlash(this);
    LOG_INFO("S&F - Finished loading history, packetHistoryTotalCount=%d", this->packetHistoryTotalCount);
}

/**
 * Handles a received mesh packet, potentially storing it for later forwarding.
 *
 * @param mp The received mesh packet.
 * @return A `ProcessMessage` indicating whether the packet was successfully handled.
 */
ProcessMessage StoreForwardModule::handleReceived(const meshtastic_MeshPacket &mp)
{
#if defined(ARCH_ESP32) || defined(ARCH_PORTDUINO)
    if (moduleConfig.store_forward.enabled) {
        // Log more information about all received packets for debugging
        LOG_INFO("S&F - handleReceived: from=0x%x, to=0x%x, id=0x%08x, channel=%d", mp.from, mp.to, mp.id, mp.channel);

        if (mp.which_payload_variant == meshtastic_MeshPacket_decoded_tag) {
            LOG_INFO("S&F - Received decoded packet: portnum=%d, payload_size=%d", mp.decoded.portnum, mp.decoded.payload.size);

            // Check specifically for TEXT_MESSAGE_APP packets that might contain SF commands
            if (mp.decoded.portnum == meshtastic_PortNum_TEXT_MESSAGE_APP) {
                auto &p = mp.decoded;

                // Make the payload null-terminated for string operations
                char payload[252];
                memcpy(payload, p.payload.bytes, p.payload.size);
                payload[p.payload.size] = 0;

                LOG_INFO("S&F - Text message received: '%s'", payload);

                // Check for SF commands
                if (isToUs(&mp)) {
                    NodeNum clientNodeNum = getFrom(&mp);
                    meshtastic_NodeInfoLite *node = nodeDB->getMeshNode(clientNodeNum);
                    const char *clientName = (node && node->has_user && node->user.long_name[0])    ? node->user.long_name
                                             : (node && node->has_user && node->user.short_name[0]) ? node->user.short_name
                                                                                                    : "Unknown";

                    LOG_INFO("S&F - Message is addressed to us from %s (0x%x)", clientName, clientNodeNum);

                    // Check for SF reset command
                    if ((p.payload.size >= 8) &&
                        (strncmp((char *)p.payload.bytes, "SF reset", 8) == 0 ||
                         (p.payload.size >= 9 && strncmp((char *)p.payload.bytes, "SF reset ", 9) == 0))) {

                        LOG_INFO("S&F - 'SF reset' command detected from %s (0x%x)", clientName, clientNodeNum);
                        resetClientHistoryPosition(clientNodeNum);
                        return ProcessMessage::STOP; // We've handled this message
                    }
                    // Check for simple SF command to retrieve messages
                    else if (p.payload.size >= 2 && p.payload.bytes[0] == 'S' && p.payload.bytes[1] == 'F' &&
                             (p.payload.size == 2 || p.payload.bytes[2] == 0)) {

                        LOG_INFO("S&F - 'SF' command detected from %s (0x%x) on channel %d", clientName, clientNodeNum,
                                 mp.channel);

                        // Store the channel this SF command came in on
                        clientChannels[clientNodeNum] = mp.channel;

                        // Process the SF command to send messages
                        if (is_server) {
                            if (this->busy) {
                                LOG_INFO("S&F - Server busy, sending error message");
                                sendErrorTextMessage(clientNodeNum, mp.decoded.want_response);
                            } else {
                                LOG_INFO("S&F - Sending message history to client");
                                // Send the last 60 minutes of messages
                                uint32_t windowSeconds = historyReturnWindow * 60;
                                uint32_t numPackets = getNumAvailablePackets(
                                    clientNodeNum, getTime() < windowSeconds ? 0 : getTime() - windowSeconds);

                                LOG_INFO("S&F - Will send %d messages from last %d minutes to %s (0x%x)", numPackets,
                                         historyReturnWindow, clientName, clientNodeNum);

                                if (numPackets > 0) {
                                    historySend(windowSeconds, clientNodeNum);
                                } else {
                                    // No messages to send, inform the user
                                    meshtastic_MeshPacket *pr = allocDataPacket();
                                    pr->to = clientNodeNum;
                                    pr->priority = meshtastic_MeshPacket_Priority_BACKGROUND;
                                    pr->want_ack = false;
                                    pr->decoded.want_response = false;
                                    pr->decoded.portnum = meshtastic_PortNum_TEXT_MESSAGE_APP;

                                    // Always use channel 0
                                    pr->channel = 0;
                                    LOG_INFO("S&F - Sending 'no messages' notification on channel 0 to node 0x%x", clientNodeNum);

                                    const char *str = "S&F - No messages available in your history window.";
                                    memcpy(pr->decoded.payload.bytes, str, strlen(str));
                                    pr->decoded.payload.size = strlen(str);
                                    service->sendToMesh(pr);
                                }
                            }
                        } else {
                            LOG_INFO("S&F - This node is not a server, ignoring SF command");
                        }
                        return ProcessMessage::STOP; // We've handled this message
                    }
                }
            }
            // Check if this is a ROUTING_APP packet that might be an ACK
            else if (mp.decoded.portnum == meshtastic_PortNum_ROUTING_APP) {
                LOG_INFO("S&F - Received routing packet, might be an ACK");
                if (waitingForAck && mp.to == nodeDB->getNodeNum() && mp.from == busyTo) {
                    LOG_INFO("S&F - This appears to be an ACK we're waiting for");
                    if (mp.decoded.request_id == lastMessageId) {
                        LOG_INFO("S&F - Confirmed! This is the ACK for our message 0x%08x", lastMessageId);
                        waitingForAck = false;
                        messageRetryCount = 0;
                        retryTimeoutMs = 5000; // Reset to default

                        // Continue sending the next message in the queue
                        setIntervalFromNow(50); // Short delay before sending next message
                    }
                }
            }
            // Check if this is a STORE_FORWARD_APP packet
            else if (mp.decoded.portnum == meshtastic_PortNum_STORE_FORWARD_APP) {
                LOG_INFO("S&F - Received a Store & Forward protocol message");
                auto &p = mp.decoded;
                meshtastic_StoreAndForward scratch;

                if (pb_decode_from_bytes(p.payload.bytes, p.payload.size, &meshtastic_StoreAndForward_msg, &scratch)) {
                    LOG_INFO("S&F - Successfully decoded S&F protobuf message, rr=%d", scratch.rr);
                    return handleReceivedProtobuf(mp, &scratch) ? ProcessMessage::STOP : ProcessMessage::CONTINUE;
                } else {
                    LOG_ERROR("S&F - Error decoding S&F protobuf message!");
                    return ProcessMessage::STOP;
                }
            }
        } else if (mp.which_payload_variant == meshtastic_MeshPacket_encrypted_tag) {
            LOG_INFO("S&F - Received encrypted message, storing for forwarding");
            // For encrypted messages, just store them for forwarding
            historyAdd(mp);
        }
    }
#endif
    return ProcessMessage::CONTINUE; // Let others look at this message also if they want
}

/**
 * Handles a received protobuf message for the Store and Forward module.
 *
 * @param mp The received MeshPacket to handle.
 * @param p A pointer to the StoreAndForward object.
 * @return True if the message was successfully handled, false otherwise.
 */
bool StoreForwardModule::handleReceivedProtobuf(const meshtastic_MeshPacket &mp, meshtastic_StoreAndForward *p)
{
    // ...existing code...
    return false; // RoutingModule sends it to the phone
}

// Enhance constructor with more detailed logging
StoreForwardModule::StoreForwardModule()
    : concurrency::OSThread("StoreForward"),
      ProtobufModule("StoreForward", meshtastic_PortNum_STORE_FORWARD_APP, &meshtastic_StoreAndForward_msg)
{
#if defined(ARCH_ESP32) || defined(ARCH_PORTDUINO)
    LOG_INFO("Initializing Store & Forward Module...");
    isPromiscuous = true; // Brown chicken brown cow

    if (StoreForward_Dev) {
        LOG_INFO("S&F - Development mode detected");
        moduleConfig.store_forward.enabled = 1;
    }

    LOG_INFO("S&F - Module enabled status: %d", moduleConfig.store_forward.enabled);

    if (moduleConfig.store_forward.enabled) {
        // Router
        bool isRouter = (config.device.role == meshtastic_Config_DeviceConfig_Role_ROUTER);
        bool isServerConfig = moduleConfig.store_forward.is_server;
        LOG_INFO("S&F - Device role: %s, Server config: %s", isRouter ? "ROUTER" : "CLIENT", isServerConfig ? "YES" : "NO");

        if (isRouter || isServerConfig) {
            LOG_INFO("Init Store & Forward Module in Server mode");

            // Log PSRAM info
            LOG_INFO("S&F - PSRAM Size: %d, Free PSRAM: %d", memGet.getPsramSize(), memGet.getFreePsram());

            if (memGet.getPsramSize() > 0) {
                if (memGet.getFreePsram() >= 1024 * 1024) {
                    // Do the startup here
                    LOG_INFO("S&F - Sufficient PSRAM available for operation");

                    // Maximum number of records to return.
                    if (moduleConfig.store_forward.history_return_max) {
                        this->historyReturnMax = moduleConfig.store_forward.history_return_max;
                        LOG_INFO("S&F - History return max set to %d", this->historyReturnMax);
                    }

                    // Maximum time window for records to return (in minutes)
                    if (moduleConfig.store_forward.history_return_window) {
                        this->historyReturnWindow = moduleConfig.store_forward.history_return_window;
                        LOG_INFO("S&F - History return window set to %d minutes", this->historyReturnWindow);
                    }

                    // Maximum number of records to store in memory
                    if (moduleConfig.store_forward.records) {
                        this->records = moduleConfig.store_forward.records;
                        LOG_INFO("S&F - Maximum records set to %d", this->records);
                    }

                    // send heartbeat advertising?
                    if (moduleConfig.store_forward.heartbeat) {
                        this->heartbeat = moduleConfig.store_forward.heartbeat;
                        LOG_INFO("S&F - Heartbeat enabled");
                    } else {
                        this->heartbeat = false;
                        LOG_INFO("S&F - Heartbeat disabled");
                    }

                    // Initialize retry parameters (use default values since the config doesn't have these fields yet)
                    this->maxRetryCount = 7;
                    this->retryTimeoutMs = 5000;
                    LOG_INFO("S&F - Retry parameters: max=%d, timeout=%dms", this->maxRetryCount, this->retryTimeoutMs);

                    // Popupate PSRAM with our data structures.
                    LOG_INFO("S&F - Initializing PSRAM storage...");
                    this->populatePSRAM();
                    LOG_INFO("S&F - PSRAM storage initialized successfully");

                    is_server = true;
                    LOG_INFO("S&F - Server mode activated successfully");
                } else {
                    LOG_WARN("S&F - Not enough free PSRAM available (need at least 1MB)");
                    LOG_INFO("S&F: not enough PSRAM free, Disable");
                }
            } else {
                LOG_WARN("S&F - Device does not have PSRAM");
                LOG_INFO("S&F: device doesn't have PSRAM, Disable");
            }
        } else {
            is_client = true;
            LOG_INFO("Init Store & Forward Module in Client mode");
            LOG_INFO("S&F - Client mode activated successfully");
        }
    } else {
        LOG_INFO("S&F - Module disabled in configuration");
        disable();
        LOG_INFO("S&F - Module disabled");
    }

    LOG_INFO("Store & Forward Module initialization complete");
#endif
}

// Add a destructor to save history when destroyed
StoreForwardModule::~StoreForwardModule()
{
    // Save history to flash when module is destroyed
    StoreForwardPersistence::saveToFlash(this);
}

// Improved implementation for the channel detection method
uint8_t StoreForwardModule::findBestChannelForNode(NodeNum nodeNum)
{
    // Always use channel 0 for all Store & Forward communications
    LOG_INFO("S&F - Forcing channel 0 for all communications with node 0x%x", nodeNum);
    return 0;
}

// Modify the sendMessage function to enforce channel 0
void StoreForwardModule::sendMessage(NodeNum dest, const meshtastic_StoreAndForward &payload)
{
    meshtastic_MeshPacket *p = allocDataProtobuf(payload);

    p->to = dest;
    p->priority = meshtastic_MeshPacket_Priority_BACKGROUND;
    p->want_ack = false;
    p->decoded.want_response = false;

    // Always use channel 0
    p->channel = 0;
    LOG_INFO("S&F - Forcing channel 0 for response message to node 0x%x", dest);

    service->sendToMesh(p);
}

// Send a store-and-forward message with just a request/response code
void StoreForwardModule::sendMessage(NodeNum dest, meshtastic_StoreAndForward_RequestResponse rr)
{
    meshtastic_StoreAndForward sf = meshtastic_StoreAndForward_init_zero;
    sf.rr = rr;
    sendMessage(dest, sf);
}

// Modify the sendErrorTextMessage to enforce channel 0
void StoreForwardModule::sendErrorTextMessage(NodeNum dest, bool want_response)
{
    meshtastic_MeshPacket *pr = allocDataPacket();
    pr->to = dest;
    pr->priority = meshtastic_MeshPacket_Priority_BACKGROUND;
    pr->want_ack = false;
    pr->decoded.want_response = false;
    pr->decoded.portnum = meshtastic_PortNum_TEXT_MESSAGE_APP;

    // Always use channel 0
    pr->channel = 0;
    LOG_INFO("S&F - Forcing channel 0 for error message to node 0x%x", dest);

    const char *str;
    if (this->busy) {
        str = "S&F - Busy. Try again shortly.";
    } else {
        str = "S&F not permitted on the public channel.";
    }
    LOG_WARN("%s", str); // This was already WARN level, which is appropriate
    memcpy(pr->decoded.payload.bytes, str, strlen(str));
    pr->decoded.payload.size = strlen(str);
    if (want_response) {
        ignoreRequest = true; // This text message counts as response.
    }
    service->sendToMesh(pr);
}

// Modify the resetClientHistoryPosition function to enforce channel 0
void StoreForwardModule::resetClientHistoryPosition(NodeNum clientNodeNum)
{
    // Get client's name from the database for better logs
    meshtastic_NodeInfoLite *node = nodeDB->getMeshNode(clientNodeNum);
    const char *clientName = (node && node->has_user && node->user.long_name[0])    ? node->user.long_name
                             : (node && node->has_user && node->user.short_name[0]) ? node->user.short_name
                                                                                    : "Unknown";

    // Reset the client's history position
    if (lastRequest.find(clientNodeNum) != lastRequest.end()) {
        lastRequest[clientNodeNum] = 0;
        LOG_INFO("S&F - Reset history position for %s (0x%x)", clientName, clientNodeNum);

        // Save the updated state to flash immediately
        StoreForwardPersistence::saveToFlash(this);

        // Send confirmation message to client
        meshtastic_MeshPacket *pr = allocDataPacket();
        pr->to = clientNodeNum;
        pr->priority = meshtastic_MeshPacket_Priority_BACKGROUND;
        pr->want_ack = false;
        pr->decoded.want_response = false;
        pr->decoded.portnum = meshtastic_PortNum_TEXT_MESSAGE_APP;

        // Always use channel 0
        pr->channel = 0;
        LOG_INFO("S&F - Forcing channel 0 for reset confirmation to node 0x%x", clientNodeNum);

        const char *str = "S&F - History reset successful. Use 'SF' to receive all messages.";
        memcpy(pr->decoded.payload.bytes, str, strlen(str));
        pr->decoded.payload.size = strlen(str);
        service->sendToMesh(pr);
    } else {
        LOG_INFO("S&F - No history position found for %s (0x%x)", clientName, clientNodeNum);

        // Send message indicating no history exists yet
        meshtastic_MeshPacket *pr = allocDataPacket();
        pr->to = clientNodeNum;
        pr->priority = meshtastic_MeshPacket_Priority_BACKGROUND;
        pr->want_ack = false;
        pr->decoded.want_response = false;
        pr->decoded.portnum = meshtastic_PortNum_TEXT_MESSAGE_APP;

        // Always use channel 0
        pr->channel = 0;
        LOG_INFO("S&F - Forcing channel 0 for reset confirmation to node 0x%x", clientNodeNum);

        const char *str = "S&F - No history found to reset. Use 'SF' to begin receiving messages.";
        memcpy(pr->decoded.payload.bytes, str, strlen(str));
        pr->decoded.payload.size = strlen(str);
        service->sendToMesh(pr);
    }
}

// Modify preparePayload to force channel 0 for outgoing messages
meshtastic_MeshPacket *StoreForwardModule::preparePayload(NodeNum dest, uint32_t last_time, bool local, bool isRetry)
{
    // If this is a retry, we want to send the same message at lastRequest[dest]-1
    uint32_t startIndex = isRetry && lastRequest[dest] > 0 ? lastRequest[dest] - 1 : lastRequest[dest];

    LOG_INFO("S&F - Preparing payload for client 0x%x from index %d, last_time=%u, isRetry=%d", dest, startIndex, last_time,
             isRetry);

    for (uint32_t i = startIndex; i < this->packetHistoryTotalCount; i++) {
        if (this->packetHistory[i].time && (this->packetHistory[i].time > last_time)) {
            /*  Copy the messages that were received by the server in the last msAgo
                to the packetHistoryTXQueue structure.
                Include all messages that are broadcasts or addressed to this client */
            if (this->packetHistory[i].to == NODENUM_BROADCAST || this->packetHistory[i].to == dest) {
                LOG_INFO("S&F - Preparing message at index %d: from=0x%x, to=0x%x, time=%u", i, this->packetHistory[i].from,
                         this->packetHistory[i].to, this->packetHistory[i].time);

                meshtastic_MeshPacket *p = allocDataPacket();

                // For non-local delivery (sending to mesh), set 'to' to destination
                // For local delivery (to phone), use original 'to' field
                p->to = local ? this->packetHistory[i].to : dest;

                // Use our node ID as the sender for retries to improve routing chances
                if (isRetry) {
                    p->from = nodeDB->getNodeNum(); // Use our ID for retries
                    LOG_INFO("S&F - Using our node ID for retry message");
                } else {
                    p->from = this->packetHistory[i].from;
                }

                // For retries, generate new message ID
                if (isRetry) {
                    // Generate a new random ID for retry attempts
                    uint32_t originalId = this->packetHistory[i].id;
                    p->id = random(0, UINT32_MAX);
                    LOG_INFO("S&F - Generated new message ID for retry: 0x%08x (original was 0x%08x)", p->id, originalId);
                    p->decoded.request_id = originalId;
                } else {
                    p->id = this->packetHistory[i].id;
                }

                p->decoded.reply_id = this->packetHistory[i].reply_id;
                p->rx_time = this->packetHistory[i].time;
                p->decoded.emoji = (uint32_t)this->packetHistory[i].emoji;

                // For S&F server messages, request ACKs to track delivery
                p->want_ack = !local;

                // Set higher priority for retry attempts
                if (isRetry) {
                    p->priority = meshtastic_MeshPacket_Priority_RELIABLE;
                    LOG_INFO("S&F - Setting priority to RELIABLE for retry");
                }

                if (local) {
                    p->decoded.portnum = meshtastic_PortNum_TEXT_MESSAGE_APP;
                    memcpy(p->decoded.payload.bytes, this->packetHistory[i].payload, this->packetHistory[i].payload_size);
                    p->decoded.payload.size = this->packetHistory[i].payload_size;
                } else {
                    meshtastic_StoreAndForward sf = meshtastic_StoreAndForward_init_zero;
                    sf.which_variant = meshtastic_StoreAndForward_text_tag;
                    sf.variant.text.size = this->packetHistory[i].payload_size;
                    memcpy(sf.variant.text.bytes, this->packetHistory[i].payload, this->packetHistory[i].payload_size);

                    // For S&F messages, preserve the original message type (broadcast/direct)
                    if (this->packetHistory[i].to == NODENUM_BROADCAST) {
                        sf.rr = meshtastic_StoreAndForward_RequestResponse_ROUTER_TEXT_BROADCAST;
                    } else {
                        sf.rr = meshtastic_StoreAndForward_RequestResponse_ROUTER_TEXT_DIRECT;
                    }

                    // Explicitly set want_response to false for S&F messages to avoid unnecessary responses
                    p->decoded.want_response = false;

                    // FORCE CHANNEL 0 for all S&F communications
                    p->channel = 0;
                    LOG_INFO("S&F - Forcing channel 0 for message to node 0x%x", dest);

                    p->decoded.payload.size = pb_encode_to_bytes(p->decoded.payload.bytes, sizeof(p->decoded.payload.bytes),
                                                                 &meshtastic_StoreAndForward_msg, &sf);
                }

                // Only update the index if this is not a retry
                if (!isRetry) {
                    lastRequest[dest] = i + 1; // Update the last request index for the client device
                    LOG_INFO("S&F - Updated lastRequest for client 0x%x to %d", dest, lastRequest[dest]);
                }

                return p;
            }
        }
    }

    LOG_INFO("S&F - No more messages found for client 0x%x from index %d", dest, startIndex);
    return nullptr;
}

/**
 * Add a complete implementation of historyAdd to ensure messages are stored properly
 */
void StoreForwardModule::historyAdd(const meshtastic_MeshPacket &mp)
{
    if (!is_server) {
        LOG_INFO("S&F - Not a server, not storing message");
        return;
    }

    LOG_INFO("S&F - Adding message to history: from=0x%x, to=0x%x, id=0x%08x", mp.from, mp.to, mp.id);

    // Check if we need to wrap around
    if (this->packetHistoryTotalCount >= this->records) {
        LOG_INFO("S&F - History buffer full, wrapping around (total=%d, max=%d)", this->packetHistoryTotalCount, this->records);
        this->packetHistoryTotalCount = 0;

        // Reset all client positions since we're wrapping
        for (auto &i : lastRequest) {
            i.second = 0;
            LOG_INFO("S&F - Reset history position for client 0x%x due to buffer wrap", i.first);
        }
    }

    // Store message data
    this->packetHistory[this->packetHistoryTotalCount].time = getTime();
    this->packetHistory[this->packetHistoryTotalCount].to = mp.to;
    this->packetHistory[this->packetHistoryTotalCount].from = getFrom(&mp);
    this->packetHistory[this->packetHistoryTotalCount].id = mp.id;

    // Store the channel explicitly
    this->packetHistory[this->packetHistoryTotalCount].channel = mp.channel;

    if (mp.which_payload_variant == meshtastic_MeshPacket_decoded_tag) {
        const auto &p = mp.decoded;
        this->packetHistory[this->packetHistoryTotalCount].reply_id = p.reply_id;
        this->packetHistory[this->packetHistoryTotalCount].emoji = (bool)p.emoji;
        this->packetHistory[this->packetHistoryTotalCount].payload_size = p.payload.size;
        memcpy(this->packetHistory[this->packetHistoryTotalCount].payload, p.payload.bytes,
               meshtastic_Constants_DATA_PAYLOAD_LEN);
    } else if (mp.which_payload_variant == meshtastic_MeshPacket_encrypted_tag) {
        // For encrypted messages, we can only store limited info
        this->packetHistory[this->packetHistoryTotalCount].reply_id = 0;
        this->packetHistory[this->packetHistoryTotalCount].emoji = false;

        // Store encrypted payload
        size_t size = std::min((size_t)mp.encrypted.size, (size_t)meshtastic_Constants_DATA_PAYLOAD_LEN);
        this->packetHistory[this->packetHistoryTotalCount].payload_size = size;
        memcpy(this->packetHistory[this->packetHistoryTotalCount].payload, mp.encrypted.bytes, size);
    }

    LOG_INFO("S&F - Stored message in history at index %d (channel=%d)", this->packetHistoryTotalCount,
             this->packetHistory[this->packetHistoryTotalCount].channel);

    this->packetHistoryTotalCount++;

    // Save history to flash
    StoreForwardPersistence::saveToFlash(this);

    LOG_INFO("S&F - History now contains %d messages", this->packetHistoryTotalCount);
}

/**
 * Add a proper implementation of historySend
 */
void StoreForwardModule::historySend(uint32_t secAgo, uint32_t to)
{
    if (!is_server) {
        LOG_INFO("S&F - Not a server, cannot send history");
        return;
    }

    // Don't accept new history requests while waiting for ACKs
    if (waitingForAck) {
        LOG_INFO("S&F - Busy waiting for ACKs from previous message, ignoring new request");
        meshtastic_StoreAndForward sf = meshtastic_StoreAndForward_init_zero;
        sf.rr = meshtastic_StoreAndForward_RequestResponse_ROUTER_BUSY;
        sendMessage(to, sf);
        return;
    }

    // Get client's name for better logs
    meshtastic_NodeInfoLite *node = nodeDB->getMeshNode(to);
    const char *clientName = (node && node->has_user && node->user.long_name[0])    ? node->user.long_name
                             : (node && node->has_user && node->user.short_name[0]) ? node->user.short_name
                                                                                    : "Unknown";

    // Create the destination entry if it doesn't exist
    if (lastRequest.find(to) == lastRequest.end()) {
        lastRequest.emplace(to, 0);
        LOG_INFO("S&F - Created new history position for client %s (0x%x)", clientName, to);
    }

    // Calculate time threshold
    uint32_t timeThreshold = getTime() < secAgo ? 0 : getTime() - secAgo;
    LOG_INFO("S&F - Sending history to %s (0x%x) from time %u (threshold=%u)", clientName, to, timeThreshold, timeThreshold);

    // Count how many messages we'll send
    uint32_t queueSize = getNumAvailablePackets(to, timeThreshold);
    LOG_INFO("S&F - Found %d messages for client %s (0x%x)", queueSize, clientName, to);

    if (queueSize > 0) {
        if (queueSize > this->historyReturnMax) {
            queueSize = this->historyReturnMax;
            LOG_INFO("S&F - Limiting to %d messages (historyReturnMax)", queueSize);
        }

        // Tell the client we're sending history
        meshtastic_StoreAndForward sf = meshtastic_StoreAndForward_init_zero;
        sf.rr = meshtastic_StoreAndForward_RequestResponse_ROUTER_HISTORY;
        sf.which_variant = meshtastic_StoreAndForward_history_tag;
        sf.variant.history.history_messages = queueSize;
        sf.variant.history.window = secAgo * 1000;
        sf.variant.history.last_request = lastRequest[to];

        meshtastic_MeshPacket *p = allocDataProtobuf(sf);
        p->to = to;
        p->priority = meshtastic_MeshPacket_Priority_BACKGROUND;
        p->want_ack = false;
        p->decoded.want_response = false;

        // Always use channel 0
        p->channel = 0;
        LOG_INFO("S&F - Sending ROUTER_HISTORY notification on channel 0 to node 0x%x", to);

        service->sendToMesh(p);

        // Mark that we're busy and who we're sending to
        this->busy = true;
        this->busyTo = to;
        this->last_time = timeThreshold;
        this->requestCount = 0;

        LOG_INFO("S&F - Set busy=true, busyTo=0x%x, requestCount=0", to);

        // Let runOnce handle sending the actual messages
        setIntervalFromNow(this->packetTimeMax);
        LOG_INFO("S&F - Scheduled message sending to begin in %d ms", this->packetTimeMax);
    } else {
        LOG_INFO("S&F - No messages to send to %s (0x%x)", clientName, to);

        // Send a notification that there are no messages
        meshtastic_MeshPacket *pr = allocDataPacket();
        pr->to = to;
        pr->priority = meshtastic_MeshPacket_Priority_BACKGROUND;
        pr->want_ack = false;
        pr->decoded.want_response = false;
        pr->decoded.portnum = meshtastic_PortNum_TEXT_MESSAGE_APP;

        // Always use channel 0
        pr->channel = 0;
        LOG_INFO("S&F - Sending 'no messages' notification on channel 0 to node 0x%x", to);

        const char *str = "S&F - No messages available in your history window.";
        memcpy(pr->decoded.payload.bytes, str, strlen(str));
        pr->decoded.payload.size = strlen(str);
        service->sendToMesh(pr);
    }
}

/**
 * Add a proper implementation of getNumAvailablePackets
 */
uint32_t StoreForwardModule::getNumAvailablePackets(NodeNum dest, uint32_t last_time)
{
    uint32_t count = 0;

    // Create entry if it doesn't exist
    if (lastRequest.find(dest) == lastRequest.end()) {
        lastRequest.emplace(dest, 0);
    }

    // When a client has been reset (lastRequest[dest] == 0), we need to scan
    // all packets in the history, not just from the last request position
    uint32_t startIndex = (lastRequest[dest] == 0) ? 0 : lastRequest[dest];

    LOG_INFO("S&F - Counting messages for client 0x%x from index %d, last_time=%u", dest, startIndex, last_time);

    for (uint32_t i = startIndex; i < this->packetHistoryTotalCount; i++) {
        if (this->packetHistory[i].time && (this->packetHistory[i].time > last_time)) {
            // Include all messages in broadcasts or addressed to this client
            if (this->packetHistory[i].to == NODENUM_BROADCAST || this->packetHistory[i].to == dest) {
                count++;
                LOG_DEBUG("S&F - Found eligible message at index %d: from=0x%x, to=0x%x, time=%u", i, this->packetHistory[i].from,
                          this->packetHistory[i].to, this->packetHistory[i].time);
            }
        }
    }

    LOG_INFO("S&F - Found %d available packets for client 0x%x (starting from index %d)", count, dest, startIndex);
    return count;
}