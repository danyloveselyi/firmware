#include "StoreForwardBaseRole.h" // Fix: use local path instead of relative path
#include <cstring>

StoreForwardBaseRole::StoreForwardBaseRole(IStoreForwardHistoryManager &historyManager, IStoreForwardMessenger &messenger,
                                           StoreForwardLogger &logger)
    : historyManager(historyManager), messenger(messenger), logger(logger)
{
}

void StoreForwardBaseRole::onRunOnce()
{
    unsigned long now = millis();

    // Periodically log status information
    if (now - lastStatusLog > STATUS_LOG_INTERVAL) {
        lastStatusLog = now;
        logger.info("Status - Messages: %u, Busy: %s", historyManager.getTotalMessageCount(), isBusy() ? "true" : "false");
    }

    // Derived classes should override to implement specific behavior
}

void StoreForwardBaseRole::onReceivePacket(const meshtastic_MeshPacket &packet)
{
    // First check if this is a packet we should process
    if (!shouldProcessPacket(packet)) {
        return;
    }

    // Process based on packet type
    if (packet.which_payload_variant == meshtastic_MeshPacket_decoded_tag) {
        // Handle text messages that might contain commands
        if (packet.decoded.portnum == meshtastic_PortNum_TEXT_MESSAGE_APP) {
            processTextCommand(packet);
        }
        // Handle Store & Forward protocol messages
        else if (packet.decoded.portnum == meshtastic_PortNum_STORE_FORWARD_APP) {
            // Decode the S&F protobuf
            meshtastic_StoreAndForward data = meshtastic_StoreAndForward_init_zero;
            if (pb_decode_from_bytes(packet.decoded.payload.bytes, packet.decoded.payload.size, &meshtastic_StoreAndForward_msg,
                                     &data)) {
                processProtocolMessage(packet, data);
            }
        }

        // Store the message if needed (could be done by client or server)
        if (shouldStorePacket(packet)) {
            historyManager.record(packet);
            logger.debug("Stored message from 0x%x to 0x%x", packet.from, packet.to);
        }
    }
}

bool StoreForwardBaseRole::shouldProcessPacket(const meshtastic_MeshPacket &packet) const
{
    // By default process all packets - derived classes can override
    return true;
}

bool StoreForwardBaseRole::shouldStorePacket(const meshtastic_MeshPacket &packet) const
{
    // By default defer to history manager's logic - derived classes can override
    return historyManager.shouldStore(packet);
}

void StoreForwardBaseRole::processTextCommand(const meshtastic_MeshPacket &packet)
{
    // Basic validation
    if (packet.decoded.payload.size == 0) {
        logger.warn("Received empty command packet from 0x%x", packet.from);
        return;
    }

    // Make a null-terminated copy of the message
    char message[meshtastic_Constants_DATA_PAYLOAD_LEN + 1];
    memset(message, 0, sizeof(message));
    memcpy(message, packet.decoded.payload.bytes, packet.decoded.payload.size);

    // Default implementation just logs - derived classes will override
    if (logger.shouldLog(StoreForwardLogger::LogLevel::DEBUG) && strncmp(message, "SF", 2) == 0) {
        logger.debug("Received command from 0x%x: %s", packet.from, message);
    }
}

void StoreForwardBaseRole::processProtocolMessage(const meshtastic_MeshPacket &packet, const meshtastic_StoreAndForward &data)
{
    // Default implementation does nothing - derived classes will override
    if (logger.shouldLog(StoreForwardLogger::LogLevel::DEBUG)) {
        logger.debug("Received protocol message type %d from 0x%x", data.rr, packet.from);
    }
}
