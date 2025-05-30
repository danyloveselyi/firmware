#include "StoreForwardBaseRole.h"
#include <cstring>

StoreForwardBaseRole::StoreForwardBaseRole(IStoreForwardHistoryManager &historyManager, IStoreForwardMessenger &messenger,
                                           ILogger &logger)
    : historyManager(historyManager), messenger(messenger), logger(logger)
{
    // Base class initialization
}

void StoreForwardBaseRole::onRunOnce()
{
    unsigned long now = millis();

    // Periodically log status information
    if (now - lastStatusLog > STATUS_LOG_INTERVAL) {
        lastStatusLog = now;
        logger.info("Status - Messages: %u, Busy: %s", historyManager.getTotalMessageCount(), isBusy() ? "true" : "false");
    }
}

void StoreForwardBaseRole::onReceivePacket(const meshtastic_MeshPacket &packet)
{
    // Common packet handling logic for all roles

    // Check if this is a text message that might contain commands
    if (packet.which_payload_variant == meshtastic_MeshPacket_decoded_tag) {
        if (packet.decoded.portnum == meshtastic_PortNum_TEXT_MESSAGE_APP) {
            // Check if this is a command for us
            processTextCommand(packet);
        } else if (packet.decoded.portnum == meshtastic_PortNum_STORE_FORWARD_APP) {
            // This is a Store & Forward protocol message
            meshtastic_StoreAndForward data = meshtastic_StoreAndForward_init_zero;
            if (pb_decode_from_bytes(packet.decoded.payload.bytes, packet.decoded.payload.size, &meshtastic_StoreAndForward_msg,
                                     &data)) {
                processProtocolMessage(packet, data);
            }
        }

        // Store any messages that should be stored
        if (historyManager.shouldStore(packet)) {
            historyManager.record(packet);
            logger.debug("Stored message from 0x%x to 0x%x", packet.from, packet.to);
        }
    }
}

void StoreForwardBaseRole::processTextCommand(const meshtastic_MeshPacket &packet)
{
    // Common text command processing for all roles

    // Validate packet has a payload
    if (packet.decoded.payload.size == 0) {
        logger.warn("Received empty command packet from 0x%x", packet.from);
        return;
    }

    // Make a null-terminated copy of the message
    char message[meshtastic_Constants_DATA_PAYLOAD_LEN + 1];
    memset(message, 0, sizeof(message));
    memcpy(message, packet.decoded.payload.bytes, packet.decoded.payload.size);

    // Debug log the command if it starts with "SF"
    if (strncmp(message, "SF", 2) == 0) {
        logger.debug("Received command: %s from 0x%x", message, packet.from);
    }
}

void StoreForwardBaseRole::processProtocolMessage(const meshtastic_MeshPacket &packet, const meshtastic_StoreAndForward &data)
{
    // Common protocol message processing for all roles
    logger.debug("Received S&F protocol message type %d from 0x%x", data.rr, packet.from);

    // Each derived class will override this to handle specific message types
}
