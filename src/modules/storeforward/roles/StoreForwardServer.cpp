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
    // Process text messages that might contain commands
    if (packet.which_payload_variant == meshtastic_MeshPacket_decoded_tag) {
        if (packet.decoded.portnum == meshtastic_PortNum_TEXT_MESSAGE_APP) {
            // Check if this is a command for us
            processTextCommand(packet);
        }

        // Store any messages that should be stored
        if (historyManager.shouldStore(packet)) {
            historyManager.record(packet);
            LOG_INFO("S&F: Stored message from 0x%x to 0x%x", packet.from, packet.to);
        }
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

    // Validate it's actually a Store & Forward command
    if (strncmp(message, "SF", 2) != 0) {
        return; // Not for us
    }

    LOG_INFO("S&F: Received command from 0x%x: %s", packet.from, message);

    // Process specific command types with more detailed error handling
    if (strcmp(message, "SF") == 0) {
        // Request for messages (send all messages from last X minutes)
        if (busy) {
            LOG_WARN("S&F: Busy with request from 0x%x, rejecting request from 0x%x", busyTo, packet.from);
            messenger.sendTextNotification(packet.from, "S&F - Busy. Try again shortly.");
        } else if (channels.isDefaultChannel(packet.channel)) {
            LOG_WARN("S&F: Request on public channel from 0x%x rejected", packet.from);
            messenger.sendTextNotification(packet.from, "S&F - Not permitted on public channel");
        } else {
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
    uint32_t queueSize = historyManager.getNumAvailablePackets(to, last_time);

    if (queueSize > historyReturnMax) {
        queueSize = historyReturnMax;
    }

    LOG_INFO("S&F - Found %u message(s) for node 0x%x", queueSize, to);

    // Send notification about how many messages will be sent
    messenger.sendHistoryResponse(to, queueSize, secondsAgo, historyManager.getLastRequestIndex(to));

    if (queueSize > 0) {
        // Set up state for sending messages in runOnce()
        this->busy = true;
        this->busyTo = to;
        this->requestCount = 0;
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
