#include "StoreForwardMessenger.h"
#include "NodeDB.h"
#include "Router.h"
#include "configuration.h"
#include "mesh/generated/meshtastic/storeforward.pb.h"
#include <cstring>

StoreForwardMessenger::StoreForwardMessenger(Router &router, MeshService &service, ILogger &logger)
    : router(router), service(service), logger(logger)
{
}

meshtastic_MeshPacket *StoreForwardMessenger::allocatePacket(NodeNum to, meshtastic_PortNum portNum, bool wantAck)
{
    meshtastic_MeshPacket *p = router.allocForSending();
    p->to = to;
    p->decoded.portnum = portNum;
    p->want_ack = wantAck;
    p->priority = meshtastic_MeshPacket_Priority_BACKGROUND;
    return p;
}

void StoreForwardMessenger::sendTextNotification(NodeNum dest, const char *message)
{
    if (!message || !*message)
        return;

    // Allocate a packet for sending
    meshtastic_MeshPacket *p = allocatePacket(dest, meshtastic_PortNum_TEXT_MESSAGE_APP);

    // Set message content
    size_t messageLen = strlen(message);
    memcpy(p->decoded.payload.bytes, message, messageLen);
    p->decoded.payload.size = messageLen;

    // Send the packet
    service.sendToMesh(p);
    logger.info("S&F: Sent notification to 0x%x: %s", dest, message);
}

void StoreForwardMessenger::sendHeartbeat(uint32_t heartbeatInterval)
{
    // Allocate a packet for the heartbeat
    meshtastic_MeshPacket *p = allocatePacket(NODENUM_BROADCAST, meshtastic_PortNum_STORE_FORWARD_APP);

    // Build the store and forward message
    meshtastic_StoreAndForward sf = meshtastic_StoreAndForward_init_zero;
    sf.rr = meshtastic_StoreAndForward_RequestResponse_ROUTER_HEARTBEAT;
    sf.which_variant = meshtastic_StoreAndForward_heartbeat_tag;
    sf.variant.heartbeat.period = heartbeatInterval;
    sf.variant.heartbeat.secondary = 0; // We always have one primary router for now

    // Pack the protobuf
    p->decoded.payload.size =
        pb_encode_to_bytes(p->decoded.payload.bytes, sizeof(p->decoded.payload.bytes), &meshtastic_StoreAndForward_msg, &sf);

    // Send the packet
    service.sendToMesh(p);
    logger.info("S&F: Sent heartbeat with interval %d seconds", heartbeatInterval);
}

void StoreForwardMessenger::sendStats(NodeNum to, uint32_t messageTotal, uint32_t messagesSaved, uint32_t messagesMax,
                                      uint32_t upTime, bool heartbeatEnabled, uint32_t returnMax, uint32_t returnWindow)
{
    meshtastic_StoreAndForward sf = meshtastic_StoreAndForward_init_zero;

    sf.rr = meshtastic_StoreAndForward_RequestResponse_ROUTER_STATS;
    sf.which_variant = meshtastic_StoreAndForward_stats_tag;
    sf.variant.stats.messages_total = messageTotal;
    sf.variant.stats.messages_saved = messagesSaved;
    sf.variant.stats.messages_max = messagesMax;
    sf.variant.stats.up_time = upTime;
    sf.variant.stats.heartbeat = heartbeatEnabled;
    sf.variant.stats.return_max = returnMax;
    sf.variant.stats.return_window = returnWindow;

    // Prepare and send the message
    meshtastic_MeshPacket *p = allocatePacket(to, meshtastic_PortNum_STORE_FORWARD_APP);
    p->decoded.payload.size =
        pb_encode_to_bytes(p->decoded.payload.bytes, sizeof(p->decoded.payload.bytes), &meshtastic_StoreAndForward_msg, &sf);
    service.sendToMesh(p);

    logger.info("S&F: Sent stats to 0x%x", to);
}

void StoreForwardMessenger::sendHistoryResponse(NodeNum to, uint32_t messageCount, uint32_t windowTime, uint32_t lastRequestIndex)
{
    meshtastic_StoreAndForward sf = meshtastic_StoreAndForward_init_zero;
    sf.rr = meshtastic_StoreAndForward_RequestResponse_ROUTER_HISTORY;
    sf.which_variant = meshtastic_StoreAndForward_history_tag;
    sf.variant.history.history_messages = messageCount;
    sf.variant.history.window = windowTime * 1000; // Convert to ms for the protocol
    sf.variant.history.last_request = lastRequestIndex;

    // Prepare and send the response
    meshtastic_MeshPacket *p = allocatePacket(to, meshtastic_PortNum_STORE_FORWARD_APP);
    p->decoded.payload.size =
        pb_encode_to_bytes(p->decoded.payload.bytes, sizeof(p->decoded.payload.bytes), &meshtastic_StoreAndForward_msg, &sf);
    service.sendToMesh(p);

    logger.info("S&F: Sent history response to 0x%x: %d messages available", to, messageCount);
}

meshtastic_MeshPacket *StoreForwardMessenger::prepareHistoryPayload(const meshtastic_MeshPacket &historyMessage, NodeNum dest)
{
    // Create a S&F packet with the stored message
    meshtastic_MeshPacket *p = allocatePacket(dest, meshtastic_PortNum_STORE_FORWARD_APP);

    // Prepare the S&F payload
    meshtastic_StoreAndForward sf = meshtastic_StoreAndForward_init_zero;

    // Determine if this is a broadcast or direct message
    if (historyMessage.to == NODENUM_BROADCAST) {
        sf.rr = meshtastic_StoreAndForward_RequestResponse_ROUTER_TEXT_BROADCAST;
    } else {
        sf.rr = meshtastic_StoreAndForward_RequestResponse_ROUTER_TEXT_DIRECT;
    }

    // Set the message data
    sf.which_variant = meshtastic_StoreAndForward_text_tag;

    // Copy the actual payload - this is the only field we should set in the text variant
    if (historyMessage.which_payload_variant == meshtastic_MeshPacket_decoded_tag) {
        sf.variant.text.size = historyMessage.decoded.payload.size;
        memcpy(sf.variant.text.bytes, historyMessage.decoded.payload.bytes, historyMessage.decoded.payload.size);
    } else {
        // Cannot forward an encrypted message
        logger.warn("S&F: Cannot prepare payload from encrypted message");
        packetPool.release(p);
        return nullptr;
    }

    // Message metadata will be handled at the MeshPacket level instead of in the payload
    p->from = historyMessage.from;
    p->id = historyMessage.id;
    p->rx_time = historyMessage.rx_time;

    // Encode the protobuf
    p->decoded.payload.size =
        pb_encode_to_bytes(p->decoded.payload.bytes, sizeof(p->decoded.payload.bytes), &meshtastic_StoreAndForward_msg, &sf);

    return p;
}

void StoreForwardMessenger::requestHistory(NodeNum serverNode, uint32_t minutes)
{
    if (serverNode == 0) {
        logger.warn("S&F: No server specified for history request");
        return;
    }

    // Create history request
    meshtastic_StoreAndForward sf = meshtastic_StoreAndForward_init_zero;
    sf.rr = meshtastic_StoreAndForward_RequestResponse_CLIENT_HISTORY;

    // If minutes were specified, include them
    if (minutes > 0) {
        sf.which_variant = meshtastic_StoreAndForward_history_tag;
        sf.variant.history.window = minutes * 60; // Convert to seconds
    }

    // Prepare and send the request
    meshtastic_MeshPacket *p = allocatePacket(serverNode, meshtastic_PortNum_STORE_FORWARD_APP);
    p->decoded.payload.size =
        pb_encode_to_bytes(p->decoded.payload.bytes, sizeof(p->decoded.payload.bytes), &meshtastic_StoreAndForward_msg, &sf);
    service.sendToMesh(p);

    logger.info("S&F: Requested history from server 0x%x with window %d minutes", serverNode, minutes);
}

void StoreForwardMessenger::requestStats(NodeNum serverNode)
{
    if (serverNode == 0) {
        logger.warn("S&F: No server specified for stats request");
        return;
    }

    // Create stats request
    meshtastic_StoreAndForward sf = meshtastic_StoreAndForward_init_zero;
    sf.rr = meshtastic_StoreAndForward_RequestResponse_CLIENT_STATS;

    // Prepare and send the request
    meshtastic_MeshPacket *p = allocatePacket(serverNode, meshtastic_PortNum_STORE_FORWARD_APP);
    p->decoded.payload.size =
        pb_encode_to_bytes(p->decoded.payload.bytes, sizeof(p->decoded.payload.bytes), &meshtastic_StoreAndForward_msg, &sf);
    service.sendToMesh(p);

    logger.info("S&F: Requested stats from server 0x%x", serverNode);
}

void StoreForwardMessenger::sendPing(NodeNum serverNode)
{
    if (serverNode == 0) {
        logger.warn("S&F: No server specified for ping");
        return;
    }

    // Create ping request
    meshtastic_StoreAndForward sf = meshtastic_StoreAndForward_init_zero;
    sf.rr = meshtastic_StoreAndForward_RequestResponse_CLIENT_PING;

    // Prepare and send the request
    meshtastic_MeshPacket *p = allocatePacket(serverNode, meshtastic_PortNum_STORE_FORWARD_APP);
    p->decoded.payload.size =
        pb_encode_to_bytes(p->decoded.payload.bytes, sizeof(p->decoded.payload.bytes), &meshtastic_StoreAndForward_msg, &sf);
    service.sendToMesh(p);

    logger.info("S&F: Sent ping to server 0x%x", serverNode);
}
