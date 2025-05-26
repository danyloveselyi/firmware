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
#include "pb_encode.h"
#include <Arduino.h>
#include <iterator>
#include <map>

StoreForwardModule *storeForwardModule;

static NodeNum pendingNoMsgNotification = 0;
static uint32_t pendingNoMsgTime = 0;
static NodeNum pendingResetConfirmation = 0;
static uint32_t pendingResetTime = 0;
static NodeNum pendingResetNotification = 0;
static uint32_t pendingResetNotifTime = 0;

StoreForwardModule::StoreForwardModule()
    : concurrency::OSThread("StoreForward"),
      ProtobufModule("StoreForward", meshtastic_PortNum_STORE_FORWARD_APP, &meshtastic_StoreAndForward_msg)
{
#if defined(ARCH_ESP32) || defined(ARCH_PORTDUINO)
    LOG_INFO("Initializing Store & Forward Module...");
    isPromiscuous = true;

    if (StoreForward_Dev)
        moduleConfig.store_forward.enabled = 1;

    if (moduleConfig.store_forward.enabled) {
        bool isRouter = (config.device.role == meshtastic_Config_DeviceConfig_Role_ROUTER);
        bool isServerConfig = moduleConfig.store_forward.is_server;

        if (isRouter || isServerConfig) {
            if (memGet.getPsramSize() > 0 && memGet.getFreePsram() >= 1024 * 1024) {
                configureModuleSettings();
                populatePSRAM();
                is_server = true;
            } else {
                LOG_WARN("S&F - Not enough free PSRAM");
            }
        } else {
            is_client = true;
        }
    } else {
        disable();
    }
#endif
}

void StoreForwardModule::configureModuleSettings()
{
    if (moduleConfig.store_forward.history_return_max)
        historyReturnMax = moduleConfig.store_forward.history_return_max;
    if (moduleConfig.store_forward.history_return_window)
        historyReturnWindow = moduleConfig.store_forward.history_return_window;
    if (moduleConfig.store_forward.records)
        records = moduleConfig.store_forward.records;
    heartbeat = moduleConfig.store_forward.heartbeat;
    maxRetryCount = 7;
    retryTimeoutMs = 5000;
}

int32_t StoreForwardModule::runOnce()
{
#if defined(ARCH_ESP32) || defined(ARCH_PORTDUINO)
    if (moduleConfig.store_forward.enabled && is_server) {
        logStatusPeriodically();
        handleRetries();
        tryTransmitMessageQueue();
        sendHeartbeatIfNeeded();
        trySendPendingResetConfirmation();
        trySendPendingResetNotification();
        trySendPendingNoMessages();
        return packetTimeMax;
    }
#endif
    return disable();
}

void StoreForwardModule::logStatusPeriodically()
{
    static uint32_t lastStatusLog = 0;
    if (millis() - lastStatusLog > 60000) {
        lastStatusLog = millis();
        LOG_INFO("S&F Status - Server: %d, Client: %d, Busy: %d, WaitingForAck: %d, RetryCount: %d, PacketHistoryCount: %d",
                 is_server, is_client, busy, waitingForAck, messageRetryCount, packetHistoryTotalCount);
    }
}

void StoreForwardModule::handleRetries()
{
    if (!waitingForAck || (millis() - lastSendTime <= retryTimeoutMs))
        return;

    auto *node = nodeDB->getMeshNode(busyTo);
    const char *clientName = (node && node->has_user && node->user.long_name[0])    ? node->user.long_name
                             : (node && node->has_user && node->user.short_name[0]) ? node->user.short_name
                                                                                    : "Unknown";

    if (messageRetryCount < maxRetryCount) {
        LOG_INFO("S&F - Retrying message to %s (0x%x), attempt %d of %d", clientName, busyTo, messageRetryCount + 1,
                 maxRetryCount);

        if (!node) {
            LOG_WARN("S&F - Target node 0x%x not found in NodeDB", busyTo);
        }

        if (sendPayload(busyTo, last_time, true)) {
            messageRetryCount++;
            lastSendTime = millis();
            retryTimeoutMs *= 2;
            LOG_INFO("S&F - Next retry in %d ms", retryTimeoutMs);
        }
    } else {
        LOG_WARN("S&F - Max retries reached for node %s (0x%x). Giving up.", clientName, busyTo);
        waitingForAck = false;
        busy = false;
        messageRetryCount = 0;
        retryTimeoutMs = 5000;
    }
}

void StoreForwardModule::tryTransmitMessageQueue()
{
    if (!busy || waitingForAck)
        return;

    LOG_INFO("S&F - Evaluating message queue: busy=%d, waitingForAck=%d, channelUtil=%.2f%%, requestCount=%d/%d", busy,
             waitingForAck, airTime->channelUtilizationPercent(), requestCount, historyReturnMax);

    if (airTime->isTxAllowedChannelUtil(false) && requestCount < historyReturnMax) {
        LOG_INFO("S&F - Attempting to send payload to 0x%x", busyTo);
        if (!sendPayload(busyTo, last_time, false)) {
            requestCount = 0;
            busy = false;
            LOG_INFO("S&F - Finished transmission to 0x%x", busyTo);
        }
    } else {
        LOG_WARN("S&F - Cannot transmit: channelUtil=%.2f%%, requestCount=%d/%d", airTime->channelUtilizationPercent(),
                 requestCount, historyReturnMax);

        if (!waitingForAck && requestCount >= historyReturnMax) {
            LOG_WARN("S&F - Max requests sent. Resetting transmission state.");
            requestCount = 0;
            busy = false;
        }
    }
}

void StoreForwardModule::sendHeartbeatIfNeeded()
{
    if (!heartbeat)
        return;

    if (!Throttle::isWithinTimespanMs(lastHeartbeat, heartbeatInterval * 1000) && airTime->isTxAllowedChannelUtil(false)) {

        lastHeartbeat = millis();
        LOG_INFO("S&F - Sending heartbeat");

        meshtastic_StoreAndForward sf = meshtastic_StoreAndForward_init_zero;
        sf.rr = meshtastic_StoreAndForward_RequestResponse_ROUTER_HEARTBEAT;
        sf.which_variant = meshtastic_StoreAndForward_heartbeat_tag;
        sf.variant.heartbeat.period = heartbeatInterval;
        sf.variant.heartbeat.secondary = 0;

        sendMessage(NODENUM_BROADCAST, sf);
    }
}

void StoreForwardModule::trySendPendingResetConfirmation()
{
    if (pendingResetConfirmation && !busy && !waitingForAck && airTime->isTxAllowedChannelUtil(false) &&
        millis() - pendingResetTime > 500) {
        const char *msg = "S&F - History reset successful. Use 'SF' to receive all messages.";
        sendTextNotification(pendingResetConfirmation, msg);
        pendingResetConfirmation = 0;
    }
}

void StoreForwardModule::trySendPendingResetNotification()
{
    if (pendingResetNotification && !busy && !waitingForAck && airTime->isTxAllowedChannelUtil(false) &&
        millis() - pendingResetNotifTime > 500) {
        const char *msg = "S&F - No history found to reset. Use 'SF' to begin receiving messages.";
        sendTextNotification(pendingResetNotification, msg);
        pendingResetNotification = 0;
    }
}

void StoreForwardModule::trySendPendingNoMessages()
{
    if (pendingNoMsgNotification && !busy && !waitingForAck && airTime->isTxAllowedChannelUtil(false) &&
        millis() - pendingNoMsgTime > 500) {
        const char *msg = "S&F - No messages available in your history window.";
        sendTextNotification(pendingNoMsgNotification, msg);
        pendingNoMsgNotification = 0;
    }
}

void StoreForwardModule::sendTextNotification(NodeNum target, const char *message)
{
    meshtastic_MeshPacket *pr = allocDataPacket();
    pr->to = target;
    pr->priority = meshtastic_MeshPacket_Priority_RELIABLE;
    pr->want_ack = true;
    pr->decoded.want_response = false;
    pr->decoded.portnum = meshtastic_PortNum_TEXT_MESSAGE_APP;
    pr->channel = 0;

    memcpy(pr->decoded.payload.bytes, message, strlen(message));
    pr->decoded.payload.size = strlen(message);

    busyTo = target;
    lastMessageId = pr->id;
    waitingForAck = true;
    messageRetryCount = 0;
    lastSendTime = millis();

    busy = true;
    service->sendToMesh(pr);
}

const char *StoreForwardModule::getClientName(meshtastic_NodeInfoLite *node)
{
    if (!node || !node->has_user)
        return "Unknown";
    if (node->user.long_name[0])
        return node->user.long_name;
    if (node->user.short_name[0])
        return node->user.short_name;
    return "Unknown";
}

StoreForwardModule::~StoreForwardModule()
{
    StoreForwardPersistence::saveToFlash(this);
}

void StoreForwardModule::populatePSRAM()
{
#if defined(ARCH_ESP32)
    packetHistory = static_cast<PacketHistoryStruct *>(ps_calloc(records, sizeof(PacketHistoryStruct)));
#elif defined(ARCH_PORTDUINO)
    packetHistory = static_cast<PacketHistoryStruct *>(calloc(records, sizeof(PacketHistoryStruct)));
#endif

    if (packetHistory == nullptr) {
        LOG_ERROR("S&F - FAILED to allocate memory for packet history!");
    } else {
        LOG_INFO("S&F - Successfully allocated memory for packet history");
    }

    StoreForwardPersistence::loadFromFlash(this);
}

void StoreForwardModule::sendMessage(NodeNum dest, const meshtastic_StoreAndForward &payload)
{
    meshtastic_MeshPacket *p = allocDataProtobuf(payload);
    p->to = dest;
    p->priority = meshtastic_MeshPacket_Priority_BACKGROUND;
    p->want_ack = false;
    p->decoded.want_response = false;
    p->channel = 0;
    service->sendToMesh(p);
}

void StoreForwardModule::sendMessage(NodeNum dest, meshtastic_StoreAndForward_RequestResponse rr)
{
    meshtastic_StoreAndForward sf = meshtastic_StoreAndForward_init_zero;
    sf.rr = rr;
    sendMessage(dest, sf);
}

bool StoreForwardModule::sendPayload(NodeNum dest, uint32_t last_time, bool isRetry)
{
    auto *node = nodeDB->getMeshNode(dest);
    const char *clientName = (node && node->has_user && node->user.long_name[0])    ? node->user.long_name
                             : (node && node->has_user && node->user.short_name[0]) ? node->user.short_name
                                                                                    : "Unknown";

    LOG_INFO("S&F - Preparing payload for %s (0x%x), last_time=%u, isRetry=%d", clientName, dest, last_time, isRetry);
    meshtastic_MeshPacket *packet = preparePayload(dest, last_time, false, isRetry);

    if (!packet) {
        LOG_INFO("S&F - No payload prepared for %s (0x%x)", clientName, dest);
        return false;
    }

    lastMessageId = packet->id;
    packet->want_ack = true;

    if (!isRetry) {
        waitingForAck = true;
        messageRetryCount = 0;
    }

    lastSendTime = millis();

    service->sendToMesh(packet);

    requestCount++;
    LOG_INFO("S&F - Payload sent to %s (0x%x), id=0x%08x, waitingForAck=%d", clientName, dest, packet->id, waitingForAck);
    return true;
}

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
                            if (busy) {
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

                                // Call historySend whether we have messages or not - let it handle the notification
                                historySend(windowSeconds, clientNodeNum);
                            }
                        } else {
                            LOG_INFO("S&F - This node is not a server, ignoring SF command");
                        }
                        return ProcessMessage::STOP; // We've handled this message
                    }
                }
            }
            // ... rest of existing code ...
        }
    }
#endif
    return ProcessMessage::CONTINUE; // Let others look at this message also if they want
}

void StoreForwardModule::resetClientHistoryPosition(NodeNum clientNodeNum)
{
    meshtastic_NodeInfoLite *node = nodeDB->getMeshNode(clientNodeNum);
    const char *clientName = (node && node->has_user && node->user.long_name[0])    ? node->user.long_name
                             : (node && node->has_user && node->user.short_name[0]) ? node->user.short_name
                                                                                    : "Unknown";

    if (lastRequest.find(clientNodeNum) != lastRequest.end()) {
        lastRequest[clientNodeNum] = 0;
        LOG_INFO("S&F - Reset history position for %s (0x%x)", clientName, clientNodeNum);
        StoreForwardPersistence::saveToFlash(this);
        pendingResetConfirmation = clientNodeNum;
        pendingResetTime = millis();
        setIntervalFromNow(100);
    } else {
        LOG_INFO("S&F - No history position found for %s (0x%x)", clientName, clientNodeNum);
        pendingResetNotification = clientNodeNum;
        pendingResetNotifTime = millis();
        setIntervalFromNow(100);
    }
}

meshtastic_MeshPacket *StoreForwardModule::preparePayload(NodeNum dest, uint32_t last_time, bool local, bool isRetry)
{
    uint32_t startIndex = isRetry && lastRequest[dest] > 0 ? lastRequest[dest] - 1 : lastRequest[dest];

    for (uint32_t i = startIndex; i < packetHistoryTotalCount; ++i) {
        const auto &record = packetHistory[i];

        if (record.time > last_time && (record.to == dest || record.to == NODENUM_BROADCAST)) {
            meshtastic_MeshPacket *packet = allocDataPacket();

            packet->to = local ? record.to : dest;
            packet->from = isRetry ? nodeDB->getNodeNum() : record.from;
            packet->id = isRetry ? (uint32_t)random(0, UINT32_MAX) : record.id;
            packet->rx_time = record.time;
            packet->decoded.reply_id = record.reply_id;
            packet->decoded.emoji = (uint32_t)record.emoji;
            packet->want_ack = !local;

            if (isRetry) {
                packet->priority = meshtastic_MeshPacket_Priority_RELIABLE;
                packet->decoded.request_id = record.id;
            }

            if (local) {
                packet->decoded.portnum = meshtastic_PortNum_TEXT_MESSAGE_APP;
                memcpy(packet->decoded.payload.bytes, record.payload, record.payload_size);
                packet->decoded.payload.size = record.payload_size;
            } else {
                meshtastic_StoreAndForward sf = meshtastic_StoreAndForward_init_zero;
                sf.which_variant = meshtastic_StoreAndForward_text_tag;
                sf.variant.text.size = record.payload_size;
                memcpy(sf.variant.text.bytes, record.payload, record.payload_size);
                sf.rr = (record.to == NODENUM_BROADCAST) ? meshtastic_StoreAndForward_RequestResponse_ROUTER_TEXT_BROADCAST
                                                         : meshtastic_StoreAndForward_RequestResponse_ROUTER_TEXT_DIRECT;

                packet->decoded.want_response = false;
                packet->channel = 0;
                packet->decoded.payload.size = pb_encode_to_bytes(
                    packet->decoded.payload.bytes, sizeof(packet->decoded.payload.bytes), &meshtastic_StoreAndForward_msg, &sf);
            }

            if (!isRetry) {
                lastRequest[dest] = i + 1;
            }

            return packet;
        }
    }

    LOG_INFO("S&F - No message found for node 0x%x from index %u", dest, startIndex);
    return nullptr;
}

void StoreForwardModule::historySend(uint32_t secAgo, uint32_t to)
{
    if (!is_server) {
        LOG_INFO("S&F - Not a server, skipping historySend");
        return;
    }

    if (waitingForAck) {
        LOG_INFO("S&F - Still waiting for ACK, sending ROUTER_BUSY to 0x%x", to);
        sendMessage(to, meshtastic_StoreAndForward_RequestResponse_ROUTER_BUSY);
        return;
    }

    meshtastic_NodeInfoLite *node = nodeDB->getMeshNode(to);
    const char *clientName = (node && node->has_user && node->user.long_name[0])    ? node->user.long_name
                             : (node && node->has_user && node->user.short_name[0]) ? node->user.short_name
                                                                                    : "Unknown";

    if (lastRequest.find(to) == lastRequest.end()) {
        lastRequest[to] = 0;
        LOG_INFO("S&F - New request entry created for 0x%x", to);
    }

    uint32_t timeThreshold = getTime() < secAgo ? 0 : getTime() - secAgo;
    uint32_t available = getNumAvailablePackets(to, timeThreshold);

    if (available == 0) {
        pendingNoMsgNotification = to;
        pendingNoMsgTime = millis();
        LOG_INFO("S&F - No messages for %s (0x%x), queued no-message notification", clientName, to);
        setIntervalFromNow(100);
        return;
    }

    uint32_t sendCount = std::min(available, historyReturnMax);
    LOG_INFO("S&F - Sending history to %s (0x%x): %u messages from last %u sec", clientName, to, sendCount, secAgo);

    meshtastic_StoreAndForward sf = meshtastic_StoreAndForward_init_zero;
    sf.rr = meshtastic_StoreAndForward_RequestResponse_ROUTER_HISTORY;
    sf.which_variant = meshtastic_StoreAndForward_history_tag;
    sf.variant.history.history_messages = sendCount;
    sf.variant.history.window = secAgo * 1000;
    sf.variant.history.last_request = lastRequest[to];

    meshtastic_MeshPacket *p = allocDataProtobuf(sf);
    p->to = to;
    p->priority = meshtastic_MeshPacket_Priority_BACKGROUND;
    p->want_ack = false;
    p->decoded.want_response = false;
    p->channel = 0;

    service->sendToMesh(p);

    busy = true;
    busyTo = to;
    last_time = timeThreshold;
    requestCount = 0;

    setIntervalFromNow(packetTimeMax);
}

uint32_t StoreForwardModule::getNumAvailablePackets(NodeNum dest, uint32_t last_time)
{
    uint32_t count = 0;

    // Ensure entry exists
    if (lastRequest.find(dest) == lastRequest.end()) {
        lastRequest[dest] = 0;
    }

    uint32_t startIndex = (lastRequest[dest] == 0) ? 0 : lastRequest[dest];

    for (uint32_t i = startIndex; i < packetHistoryTotalCount; ++i) {
        const auto &entry = packetHistory[i];
        if (entry.time > last_time && (entry.to == dest || entry.to == NODENUM_BROADCAST)) {
            count++;
        }
    }

    return count;
}

void StoreForwardModule::sendErrorTextMessage(NodeNum dest, bool want_response)
{
    meshtastic_MeshPacket *p = allocDataPacket();
    p->to = dest;
    p->priority = meshtastic_MeshPacket_Priority_BACKGROUND;
    p->want_ack = false;
    p->decoded.want_response = false;
    p->decoded.portnum = meshtastic_PortNum_TEXT_MESSAGE_APP;
    p->channel = 0;

    const char *errorMsg = busy ? "S&F - Busy. Try again shortly." : "S&F not permitted on the public channel.";

    size_t len = strlen(errorMsg);
    memcpy(p->decoded.payload.bytes, errorMsg, len);
    p->decoded.payload.size = len;

    if (want_response) {
        ignoreRequest = true;
    }

    service->sendToMesh(p);
    LOG_WARN("S&F - Sent error message to 0x%x: %s", dest, errorMsg);
}

void StoreForwardModule::checkPendingNotifications()
{
    if (!airTime->isTxAllowedChannelUtil(false) || busy || waitingForAck)
        return;

    auto sendTextNotification = [&](NodeNum target, const char *msg, NodeNum &pendingVar, uint32_t &pendingTime) {
        if (millis() - pendingTime < 500)
            return;

        meshtastic_MeshPacket *pr = allocDataPacket();
        pr->to = target;
        pr->priority = meshtastic_MeshPacket_Priority_RELIABLE;
        pr->want_ack = true;
        pr->decoded.want_response = false;
        pr->decoded.portnum = meshtastic_PortNum_TEXT_MESSAGE_APP;
        pr->channel = 0;

        memcpy(pr->decoded.payload.bytes, msg, strlen(msg));
        pr->decoded.payload.size = strlen(msg);

        busyTo = target;
        lastMessageId = pr->id;
        waitingForAck = true;
        messageRetryCount = 0;
        lastSendTime = millis();
        busy = true;

        LOG_INFO("S&F - Sent notification to 0x%x: %s", target, msg);
        service->sendToMesh(pr);

        pendingVar = 0;
    };

    if (pendingResetConfirmation) {
        sendTextNotification(pendingResetConfirmation, "S&F - History reset successful. Use 'SF' to receive all messages.",
                             pendingResetConfirmation, pendingResetTime);
    } else if (pendingResetNotification) {
        sendTextNotification(pendingResetNotification, "S&F - No history found to reset. Use 'SF' to begin receiving messages.",
                             pendingResetNotification, pendingResetNotifTime);
    } else if (pendingNoMsgNotification) {
        sendTextNotification(pendingNoMsgNotification, "S&F - No messages available in your history window.",
                             pendingNoMsgNotification, pendingNoMsgTime);
    }
}

void StoreForwardModule::statsSend(uint32_t to)
{
    LOG_INFO("S&F - statsSend() called for node 0x%x", to);

    // Just notify that stats are available (without actual data)
    sendMessage(to, meshtastic_StoreAndForward_RequestResponse_ROUTER_STATS);
}

meshtastic_MeshPacket *StoreForwardModule::getForPhone()
{
    // Only return messages if we're running in server mode and have history
    if (!is_server || packetHistoryTotalCount == 0) {
        return nullptr;
    }

    NodeNum localNodeNum = nodeDB->getNodeNum();

    for (uint32_t i = 0; i < packetHistoryTotalCount; ++i) {
        const PacketHistoryStruct &entry = packetHistory[i];

        // Only include packets addressed to this node or broadcast messages
        if (entry.to != localNodeNum && entry.to != NODENUM_BROADCAST) {
            continue;
        }

        // Create a new mesh packet for local delivery
        meshtastic_MeshPacket *packet = allocDataPacket();
        packet->to = localNodeNum;
        packet->from = entry.from;
        packet->id = entry.id;
        packet->rx_time = entry.time;
        packet->channel = entry.channel;

        // Set decoded fields for text message
        packet->decoded.portnum = meshtastic_PortNum_TEXT_MESSAGE_APP;
        packet->decoded.reply_id = entry.reply_id;
        packet->decoded.emoji = (uint32_t)entry.emoji;
        packet->decoded.payload.size = entry.payload_size;
        memcpy(packet->decoded.payload.bytes, entry.payload, entry.payload_size);

        LOG_INFO("S&F - getForPhone returning packet id=0x%08x to=0x%x", packet->id, packet->to);
        return packet;
    }

    LOG_INFO("S&F - getForPhone found no matching packet for phone");
    return nullptr;
}

bool StoreForwardModule::handleReceivedProtobuf(const meshtastic_MeshPacket &mp, meshtastic_StoreAndForward *p)
{
    if (!moduleConfig.store_forward.enabled) {
        // If this module is not enabled in any capacity, don't handle the packet, and allow other modules to consume
        return false;
    }

    LOG_INFO("S&F - Received Protobuf message, rr=%d", p->rr);
    requests++;

    switch (p->rr) {
    case meshtastic_StoreAndForward_RequestResponse_CLIENT_ERROR:
    case meshtastic_StoreAndForward_RequestResponse_CLIENT_ABORT:
        if (is_server) {
            // stop sending stuff, the client wants to abort or has another error
            if ((busy) && (busyTo == getFrom(&mp))) {
                LOG_ERROR("S&F - Client in ERROR or ABORT requested");
                requestCount = 0;
                busy = false;
            }
        }
        break;

    case meshtastic_StoreAndForward_RequestResponse_CLIENT_HISTORY:
        if (is_server) {
            requests_history++;
            LOG_INFO("S&F - Client Request to send HISTORY");
            // Send the last 60 minutes of messages.
            if (busy || channels.isDefaultChannel(mp.channel)) {
                sendErrorTextMessage(getFrom(&mp), mp.decoded.want_response);
            } else {
                if ((p->which_variant == meshtastic_StoreAndForward_history_tag) && (p->variant.history.window > 0)) {
                    // window is in minutes
                    historySend(p->variant.history.window * 60, getFrom(&mp));
                } else {
                    historySend(historyReturnWindow * 60, getFrom(&mp)); // defaults to 4 hours
                }
            }
        }
        break;

    case meshtastic_StoreAndForward_RequestResponse_CLIENT_PING:
        if (is_server) {
            // respond with a ROUTER PONG
            sendMessage(getFrom(&mp), meshtastic_StoreAndForward_RequestResponse_ROUTER_PONG);
        }
        break;

    case meshtastic_StoreAndForward_RequestResponse_CLIENT_PONG:
        if (is_server) {
            // NodeDB is already updated
            LOG_INFO("S&F - Received CLIENT_PONG from 0x%x", getFrom(&mp));
        }
        break;

    case meshtastic_StoreAndForward_RequestResponse_CLIENT_STATS:
        if (is_server) {
            LOG_INFO("S&F - Client Request to send STATS");
            if (busy) {
                sendMessage(getFrom(&mp), meshtastic_StoreAndForward_RequestResponse_ROUTER_BUSY);
                LOG_INFO("S&F - Busy. Try again shortly");
            } else {
                statsSend(getFrom(&mp));
            }
        }
        break;

    case meshtastic_StoreAndForward_RequestResponse_ROUTER_ERROR:
    case meshtastic_StoreAndForward_RequestResponse_ROUTER_BUSY:
        if (is_client) {
            LOG_INFO("S&F - Received ROUTER_BUSY/ERROR from 0x%x", getFrom(&mp));
            // retry in messages_saved * packetTimeMax ms
            retry_delay = millis() + getNumAvailablePackets(busyTo, last_time) * packetTimeMax *
                                         (meshtastic_StoreAndForward_RequestResponse_ROUTER_ERROR ? 2 : 1);
        }
        break;

    case meshtastic_StoreAndForward_RequestResponse_ROUTER_PONG:
    // A router responded, this is equal to receiving a heartbeat
    case meshtastic_StoreAndForward_RequestResponse_ROUTER_HEARTBEAT:
        if (is_client) {
            // register heartbeat and interval
            if (p->which_variant == meshtastic_StoreAndForward_heartbeat_tag) {
                heartbeatInterval = p->variant.heartbeat.period;
            }
            lastHeartbeat = millis();
            LOG_INFO("S&F - Heartbeat received from 0x%x", getFrom(&mp));
        }
        break;

    case meshtastic_StoreAndForward_RequestResponse_ROUTER_PING:
        if (is_client) {
            // respond with a CLIENT PONG
            LOG_INFO("S&F - Responding to PING from 0x%x", getFrom(&mp));
            sendMessage(getFrom(&mp), meshtastic_StoreAndForward_RequestResponse_CLIENT_PONG);
        }
        break;

    case meshtastic_StoreAndForward_RequestResponse_ROUTER_STATS:
        if (is_client) {
            LOG_INFO("S&F - Received ROUTER_STATS from 0x%x", getFrom(&mp));
            // These fields only have informational purpose on a client. Fill them to consume later.
            if (p->which_variant == meshtastic_StoreAndForward_stats_tag) {
                records = p->variant.stats.messages_max;
                requests = p->variant.stats.requests;
                requests_history = p->variant.stats.requests_history;
                heartbeat = p->variant.stats.heartbeat;
                historyReturnMax = p->variant.stats.return_max;
                historyReturnWindow = p->variant.stats.return_window;
            }
        }
        break;

    case meshtastic_StoreAndForward_RequestResponse_ROUTER_HISTORY:
        if (is_client) {
            // These fields only have informational purpose on a client. Fill them to consume later.
            if (p->which_variant == meshtastic_StoreAndForward_history_tag) {
                historyReturnWindow = p->variant.history.window / 60000;
                LOG_INFO("S&F - Router Response HISTORY - Sending %d messages from last %d minutes",
                         p->variant.history.history_messages, historyReturnWindow);
            }
        }
        break;

    default:
        LOG_WARN("S&F - Unhandled Store & Forward message type: %d", p->rr);
        break; // no need to do anything
    }
    return false; // RoutingModule sends it to the phone
}

// Also need to implement historyAdd to store incoming messages
void StoreForwardModule::historyAdd(const meshtastic_MeshPacket &mp)
{
    if (!is_server) {
        LOG_INFO("S&F - Not a server, not storing message");
        return;
    }

    LOG_INFO("S&F - Adding message to history: from=0x%x, to=0x%x, id=0x%08x", mp.from, mp.to, mp.id);

    // Check if we need to wrap around
    if (packetHistoryTotalCount >= records) {
        LOG_INFO("S&F - History buffer full, wrapping around (total=%d, max=%d)", packetHistoryTotalCount, records);
        packetHistoryTotalCount = 0;

        // Reset all client positions since we're wrapping
        for (auto &i : lastRequest) {
            i.second = 0;
            LOG_INFO("S&F - Reset history position for client 0x%x due to buffer wrap", i.first);
        }
    }

    // Store message data
    if (mp.which_payload_variant == meshtastic_MeshPacket_decoded_tag) {
        const auto &p = mp.decoded;

        packetHistory[packetHistoryTotalCount].time = getTime();
        packetHistory[packetHistoryTotalCount].to = mp.to;
        packetHistory[packetHistoryTotalCount].from = getFrom(&mp);
        packetHistory[packetHistoryTotalCount].id = mp.id;
        packetHistory[packetHistoryTotalCount].channel = mp.channel;
        packetHistory[packetHistoryTotalCount].reply_id = p.reply_id;
        packetHistory[packetHistoryTotalCount].emoji = (bool)p.emoji;
        packetHistory[packetHistoryTotalCount].payload_size = p.payload.size;

        if (p.payload.size <= meshtastic_Constants_DATA_PAYLOAD_LEN) {
            memcpy(packetHistory[packetHistoryTotalCount].payload, p.payload.bytes, p.payload.size);
        } else {
            LOG_ERROR("S&F - Payload too large, truncating: %d bytes", p.payload.size);
            memcpy(packetHistory[packetHistoryTotalCount].payload, p.payload.bytes, meshtastic_Constants_DATA_PAYLOAD_LEN);
            packetHistory[packetHistoryTotalCount].payload_size = meshtastic_Constants_DATA_PAYLOAD_LEN;
        }

        LOG_INFO("S&F - Stored decoded message in history at index %d", packetHistoryTotalCount);
    } else if (mp.which_payload_variant == meshtastic_MeshPacket_encrypted_tag) {
        // For encrypted messages, store minimal information
        packetHistory[packetHistoryTotalCount].time = getTime();
        packetHistory[packetHistoryTotalCount].to = mp.to;
        packetHistory[packetHistoryTotalCount].from = getFrom(&mp);
        packetHistory[packetHistoryTotalCount].id = mp.id;
        packetHistory[packetHistoryTotalCount].channel = mp.channel;
        packetHistory[packetHistoryTotalCount].reply_id = 0;
        packetHistory[packetHistoryTotalCount].emoji = false;

        // Store encrypted payload
        size_t size = std::min((size_t)mp.encrypted.size, (size_t)meshtastic_Constants_DATA_PAYLOAD_LEN);
        packetHistory[packetHistoryTotalCount].payload_size = size;
        memcpy(packetHistory[packetHistoryTotalCount].payload, mp.encrypted.bytes, size);

        LOG_INFO("S&F - Stored encrypted message in history at index %d", packetHistoryTotalCount);
    }

    packetHistoryTotalCount++;

    // Save history to flash periodically (every 10th message)
    if (packetHistoryTotalCount % 10 == 0) {
        StoreForwardPersistence::saveToFlash(this);
    }

    LOG_INFO("S&F - History now contains %d messages", packetHistoryTotalCount);
}
