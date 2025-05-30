#include "StoreForwardServer.h" // Fix the include path to local file
#include "../utils/StoreForwardConfigUtils.h"
#include "MeshService.h"
#include "NodeDB.h"
#include "RTC.h"
#include "Router.h"
#include "configuration.h"
#include <cstring>

StoreForwardServer::StoreForwardServer(IStoreForwardHistoryManager &historyManager, IStoreForwardMessenger &messenger)
    : historyManager(historyManager), messenger(messenger)
{
    LOG_INFO("S&F: Initializing Server mode");

    // Use the config utils to load configuration values
    historyReturnMax = StoreForwardConfigUtils::getHistoryReturnMax();
    historyReturnWindow = StoreForwardConfigUtils::getHistoryReturnWindow();
}

void StoreForwardServer::onRunOnce()
{
    unsigned long now = millis();

    // Periodically log status information
    if (now - lastStatusLog > STATUS_LOG_INTERVAL) {
        lastStatusLog = now;
        LOG_INFO("S&F Server Status - Active, Messages: %u", historyManager.getTotalMessageCount());
    }

    // Send heartbeat if enabled
    if (StoreForwardConfigUtils::isHeartbeatEnabled() && (now - lastHeartbeatTime > HEARTBEAT_INTERVAL)) {
        lastHeartbeatTime = now;
        sendHeartbeat();
    }

    // Process queued history sending if busy
    if (busy && airTime->isTxAllowedChannelUtil(true) && requestCount < historyReturnMax) {
        if (!sendNextHistoryPacket()) {
            // No more packets to send, we're done
            requestCount = 0;
            busy = false;
        }
    }
}

void StoreForwardServer::onReceivePacket(const meshtastic_MeshPacket &packet)
{
    // Log EVERY packet that reaches this method
    LOG_INFO("S&F Server: onReceivePacket called - from=0x%x, portnum=%d, payloadSize=%d", packet.from,
             packet.which_payload_variant == meshtastic_MeshPacket_decoded_tag ? packet.decoded.portnum : -1,
             packet.which_payload_variant == meshtastic_MeshPacket_decoded_tag ? packet.decoded.payload.size : 0);

    // Process text messages that might contain commands or need to be stored
    if (packet.which_payload_variant == meshtastic_MeshPacket_decoded_tag) {
        if (packet.decoded.portnum == meshtastic_PortNum_TEXT_MESSAGE_APP && packet.decoded.payload.size > 0) {
            // For text messages, always log the content
            char msgBuf[meshtastic_Constants_DATA_PAYLOAD_LEN + 1] = {0};
            memcpy(msgBuf, packet.decoded.payload.bytes,
                   std::min((size_t)packet.decoded.payload.size, (size_t)meshtastic_Constants_DATA_PAYLOAD_LEN));
            LOG_INFO("S&F Server: Text message content: \"%s\"", msgBuf);

            // Check if this is a command for us
            if (strncmp(msgBuf, "SF", 2) == 0 && (packet.decoded.payload.size == 2 || msgBuf[2] == ' ' || msgBuf[2] == '\0')) {
                LOG_INFO("S&F Server: Processing SF command");
                processTextCommand(packet);
                return; // Don't try to store commands
            } else {
                LOG_INFO("S&F Server: Regular text message, will try to store");
            }
        }

        // For any non-command message, check if we should store it
        LOG_INFO("S&F Server: Checking if packet should be stored...");

        bool shouldStoreMsg = historyManager.shouldStore(packet);
        LOG_INFO("S&F Server: shouldStore() returned %d for message from 0x%x to 0x%x", shouldStoreMsg, packet.from, packet.to);

        if (shouldStoreMsg) {
            LOG_INFO("S&F Server: Recording message from 0x%x", packet.from);

            // Store the message
            historyManager.record(packet);

            LOG_INFO("S&F Server: Successfully stored message from 0x%x to 0x%x", packet.from, packet.to);
            LOG_INFO("S&F Server: Total messages in storage: %u", historyManager.getTotalMessageCount());
        } else {
            LOG_INFO("S&F Server: Message NOT stored from 0x%x - failed shouldStore check", packet.from);
        }
    } else {
        LOG_INFO("S&F Server: Ignoring packet with non-decoded payload from 0x%x", packet.from);
    }
}

void StoreForwardServer::processTextCommand(const meshtastic_MeshPacket &packet)
{
    // Validate packet has a payload
    if (packet.decoded.payload.size == 0) {
        LOG_WARN("S&F: Received empty command packet from 0x%x", packet.from);
        return;
    }

    // Make a null-terminated copy of the message
    char message[meshtastic_Constants_DATA_PAYLOAD_LEN + 1];
    memset(message, 0, sizeof(message));
    memcpy(message, packet.decoded.payload.bytes, packet.decoded.payload.size);

    // Add more logging to see what we're processing
    LOG_INFO("S&F: Processing command from 0x%x: \"%s\"", packet.from, message);

    // Validate it's actually a Store & Forward command
    if (strncmp(message, "SF", 2) != 0) {
        LOG_INFO("S&F: Message doesn't start with 'SF', ignoring");
        return; // Not for us
    }

    // Process specific command types with more detailed error handling
    if (strcmp(message, "SF") == 0) {
        LOG_INFO("S&F: Processing 'SF' history request command");

        // Request for messages (send all messages from last X minutes)
        if (busy) {
            LOG_WARN("S&F: Busy with request from 0x%x, rejecting request from 0x%x", busyTo, packet.from);
            messenger.sendTextNotification(packet.from, "S&F - Busy. Try again shortly.");
        } else if (channels.isDefaultChannel(packet.channel)) {
            LOG_WARN("S&F: Request on public channel (ch=%d) from 0x%x rejected", packet.channel, packet.from);
            messenger.sendTextNotification(packet.from, "S&F - Not permitted on public channel");
        } else {
            LOG_INFO("S&F: Starting history send for node 0x%x with window %d minutes", packet.from, historyReturnWindow);
            historySend(packet.from, historyReturnWindow * 60);
        }
    } else if (strncmp(message, "SF reset", 8) == 0) {
        // Reset client history position
        historyManager.updateLastRequest(packet.from, 0);
        messenger.sendTextNotification(packet.from, "S&F - History reset successful. Use 'SF' to receive all messages.");
        LOG_INFO("S&F: Reset history position for 0x%x", packet.from);
    } else if (strncmp(message, "SF stats", 8) == 0) {
        // Send stats
        if (busy) {
            messenger.sendTextNotification(packet.from, "S&F - Busy. Try again shortly.");
        } else {
            sendStats(packet.from);
        }
    }
}

void StoreForwardServer::historySend(NodeNum to, uint32_t secondsAgo)
{
    this->last_time = getTime() < secondsAgo ? 0 : getTime() - secondsAgo;
    LOG_INFO("S&F: Calculating available packets since time %u", last_time); // Changed from DEBUG to INFO

    uint32_t queueSize = historyManager.getNumAvailablePackets(to, last_time);
    LOG_INFO("S&F: Found %u potential messages for node 0x%x", queueSize, to); // Changed from DEBUG to INFO

    if (queueSize > historyReturnMax) {
        LOG_INFO("S&F: Limiting messages to max %u", historyReturnMax); // Changed from DEBUG to INFO
        queueSize = historyReturnMax;
    }

    LOG_INFO("S&F: Found %u message(s) for node 0x%x", queueSize, to);

    // Send notification about how many messages will be sent
    LOG_INFO("S&F: Sending history response notification to 0x%x", to); // Changed from DEBUG to INFO
    messenger.sendHistoryResponse(to, queueSize, secondsAgo, historyManager.getLastRequestIndex(to));

    if (queueSize > 0) {
        // Set up state for sending messages in runOnce()
        LOG_INFO("S&F: Setting busy flag to begin message delivery"); // Changed from DEBUG to INFO
        this->busy = true;
        this->busyTo = to;
        this->requestCount = 0;
    } else {
        LOG_INFO("S&F: No messages to send, not setting busy flag"); // Changed from DEBUG to INFO
    }
}

void StoreForwardServer::sendStats(NodeNum to)
{
    messenger.sendStats(to, historyManager.getMaxRecords(), historyManager.getTotalMessageCount(), historyManager.getMaxRecords(),
                        millis() / 1000, // uptime in seconds
                        moduleConfig.store_forward.heartbeat, historyReturnMax, historyReturnWindow);

    LOG_INFO("S&F: Sent stats to 0x%x", to);
}

void StoreForwardServer::sendHeartbeat()
{
    messenger.sendHeartbeat(heartbeatInterval);
    LOG_INFO("S&F: Sent heartbeat");
}

meshtastic_MeshPacket *StoreForwardServer::prepareHistoryPayload(NodeNum dest, uint32_t index)
{
    // Get all available messages since last_time
    std::vector<meshtastic_MeshPacket> messages = historyManager.getMessagesForNode(dest, last_time);

    // Check if we have any messages to send
    if (index >= messages.size()) {
        return nullptr;
    }

    const auto &msg = messages[index];
    meshtastic_MeshPacket *p = messenger.prepareHistoryPayload(msg, dest);

    // Update the last request index for this client
    if (p != nullptr) {
        historyManager.updateLastRequest(dest, index + 1);
    }

    return p;
}

bool StoreForwardServer::sendNextHistoryPacket()
{
    if (!busy) {
        return false;
    }

    meshtastic_MeshPacket *p = prepareHistoryPayload(busyTo, requestCount);
    if (p) {
        LOG_INFO("S&F: Sending history packet %d to 0x%x", requestCount + 1, busyTo);
        service->sendToMesh(p);
        requestCount++;
        return true;
    }

    return false;
}
