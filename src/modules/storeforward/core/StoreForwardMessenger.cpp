#include "StoreForwardMessenger.h"
#include "MemoryPool.h" // Include for packetPool
#include "mesh/generated/meshtastic/storeforward.pb.h"

StoreForwardMessenger::StoreForwardMessenger(MeshService &service, ILogger &logger, Router *router)
    : service(service), logger(logger), router(router)
{
}

// Helper method to allocate packets with common settings
meshtastic_MeshPacket *StoreForwardMessenger::allocatePacket(NodeNum to, meshtastic_PortNum portNum, bool wantAck)
{
    meshtastic_MeshPacket *p = packetPool.allocZeroed();
    p->to = to;
    p->decoded.portnum = portNum;
    p->want_ack = wantAck;

    return p;
}

meshtastic_MeshPacket *StoreForwardMessenger::allocReply()
{
    return router->allocForSending();
}

bool StoreForwardMessenger::sendReply(meshtastic_MeshPacket *packet)
{
    service.sendToMesh(packet);
    return true;
}

void StoreForwardMessenger::sendTextNotification(NodeNum dest, const char *message)
{
    meshtastic_MeshPacket *p = allocatePacket(dest, meshtastic_PortNum_TEXT_MESSAGE_APP);

    // Copy text into the packet
    size_t len = strlen(message);
    if (len > sizeof(p->decoded.payload.bytes))
        len = sizeof(p->decoded.payload.bytes);

    memcpy(p->decoded.payload.bytes, message, len);
    p->decoded.payload.size = len;

    service.sendToMesh(p);
}

void StoreForwardMessenger::sendHeartbeat(uint32_t heartbeatInterval)
{
    meshtastic_MeshPacket *p = allocatePacket(NODENUM_BROADCAST, meshtastic_PortNum_STORE_FORWARD_APP);

    // Create and populate heartbeat
    meshtastic_StoreAndForward sf = meshtastic_StoreAndForward_init_default;
    sf.rr = meshtastic_StoreAndForward_RequestResponse_UNSET;    // Use UNSET instead of RESPONSE
    sf.which_payload = meshtastic_StoreAndForward_heartbeat_tag; // Use payload instead of variant
    sf.payload.heartbeat.period = heartbeatInterval;

    // Encode to the packet
    pb_ostream_t stream = pb_ostream_from_buffer(p->decoded.payload.bytes, sizeof(p->decoded.payload.bytes));
    if (pb_encode(&stream, meshtastic_StoreAndForward_fields, &sf)) {
        p->decoded.payload.size = stream.bytes_written;
        service.sendToMesh(p);
    } else {
        logger.error("Heartbeat too large for packet");
        packetPool.release(p); // Use global packetPool
    }
}

void StoreForwardMessenger::sendStats(NodeNum to, uint32_t messageTotal, uint32_t messagesSaved, uint32_t messagesMax,
                                      uint32_t uptime, bool hasStore, uint32_t requestsHandled, uint32_t window)
{
    meshtastic_MeshPacket *p = allocatePacket(to, meshtastic_PortNum_STORE_FORWARD_APP);

    // Create and populate stats
    meshtastic_StoreAndForward sf = meshtastic_StoreAndForward_init_default;
    sf.rr = meshtastic_StoreAndForward_RequestResponse_UNSET; // Use UNSET instead of RESPONSE
    sf.which_payload = meshtastic_StoreAndForward_stats_tag;  // Use payload instead of variant

    // Update field names to match the actual protobuf definition
    sf.payload.stats.time_window = window;
    sf.payload.stats.message_count = messagesSaved;
    sf.payload.stats.max_messages = messagesMax;
    sf.payload.stats.message_overwritten = messageTotal - messagesSaved;
    sf.payload.stats.up_time = uptime;
    sf.payload.stats.storage_enabled = hasStore;
    sf.payload.stats.request_count = requestsHandled;

    // Encode to the packet
    pb_ostream_t stream = pb_ostream_from_buffer(p->decoded.payload.bytes, sizeof(p->decoded.payload.bytes));
    if (pb_encode(&stream, meshtastic_StoreAndForward_fields, &sf)) {
        p->decoded.payload.size = stream.bytes_written;
        service.sendToMesh(p);
    } else {
        logger.error("Stats too large for packet");
        packetPool.release(p); // Use global packetPool
    }
}

void StoreForwardMessenger::sendHistoryResponse(NodeNum dest, uint32_t historyMessages, uint32_t window, uint32_t lastIndex)
{
    meshtastic_MeshPacket *p = allocatePacket(dest, meshtastic_PortNum_STORE_FORWARD_APP);

    // Create and populate history response
    meshtastic_StoreAndForward sf = meshtastic_StoreAndForward_init_default;
    sf.rr = meshtastic_StoreAndForward_RequestResponse_UNSET;           // Use UNSET instead of RESPONSE
    sf.which_payload = meshtastic_StoreAndForward_history_response_tag; // Use payload instead of variant
    sf.payload.history_response.time_window = window;
    sf.payload.history_response.message_count = historyMessages;
    sf.payload.history_response.last_index = lastIndex;

    // Encode to the packet
    pb_ostream_t stream = pb_ostream_from_buffer(p->decoded.payload.bytes, sizeof(p->decoded.payload.bytes));
    if (pb_encode(&stream, meshtastic_StoreAndForward_fields, &sf)) {
        p->decoded.payload.size = stream.bytes_written;
        service.sendToMesh(p);
    } else {
        logger.error("History response too large for packet");
        packetPool.release(p); // Use global packetPool
    }
}

meshtastic_MeshPacket *StoreForwardMessenger::prepareHistoryPayload(const meshtastic_MeshPacket &historyMessage, NodeNum dest)
{
    meshtastic_MeshPacket *p = allocatePacket(dest, meshtastic_PortNum_STORE_FORWARD_APP);

    // Create and populate history item
    meshtastic_StoreAndForward sf = meshtastic_StoreAndForward_init_default;
    sf.rr = meshtastic_StoreAndForward_RequestResponse_UNSET;  // Use UNSET instead of RESPONSE
    sf.which_payload = meshtastic_StoreAndForward_history_tag; // Use payload instead of variant

    // Copy the original message into the history item
    memcpy(&sf.payload.history.message, &historyMessage, sizeof(meshtastic_MeshPacket));

    // Encode to the packet
    pb_ostream_t stream = pb_ostream_from_buffer(p->decoded.payload.bytes, sizeof(p->decoded.payload.bytes));
    if (pb_encode(&stream, meshtastic_StoreAndForward_fields, &sf)) {
        p->decoded.payload.size = stream.bytes_written;
        return p;
    } else {
        logger.error("History item too large for packet");
        packetPool.release(p); // Use global packetPool
        return nullptr;
    }
}

void StoreForwardMessenger::requestHistory(NodeNum serverNode, uint32_t minutes)
{
    meshtastic_MeshPacket *p = allocatePacket(serverNode, meshtastic_PortNum_STORE_FORWARD_APP);

    // Create and populate history request
    meshtastic_StoreAndForward sf = meshtastic_StoreAndForward_init_default;
    sf.rr = meshtastic_StoreAndForward_RequestResponse_UNSET;          // Use UNSET instead of REQUEST
    sf.which_payload = meshtastic_StoreAndForward_history_request_tag; // Use payload instead of variant
    sf.payload.history_request.time_window = minutes;

    // Encode to the packet
    pb_ostream_t stream = pb_ostream_from_buffer(p->decoded.payload.bytes, sizeof(p->decoded.payload.bytes));
    if (pb_encode(&stream, meshtastic_StoreAndForward_fields, &sf)) {
        p->decoded.payload.size = stream.bytes_written;
        service.sendToMesh(p);
    } else {
        logger.error("History request too large for packet");
        packetPool.release(p); // Use global packetPool
    }
}

void StoreForwardMessenger::requestStats(NodeNum serverNode)
{
    meshtastic_MeshPacket *p = allocatePacket(serverNode, meshtastic_PortNum_STORE_FORWARD_APP);

    // Create and populate stats request
    meshtastic_StoreAndForward sf = meshtastic_StoreAndForward_init_default;
    sf.rr = meshtastic_StoreAndForward_RequestResponse_UNSET;        // Use UNSET instead of REQUEST
    sf.which_payload = meshtastic_StoreAndForward_stats_request_tag; // Use payload instead of variant

    // Encode to the packet
    pb_ostream_t stream = pb_ostream_from_buffer(p->decoded.payload.bytes, sizeof(p->decoded.payload.bytes));
    if (pb_encode(&stream, meshtastic_StoreAndForward_fields, &sf)) {
        p->decoded.payload.size = stream.bytes_written;
        service.sendToMesh(p);
    } else {
        logger.error("Stats request too large for packet");
        packetPool.release(p); // Use global packetPool
    }
}

void StoreForwardMessenger::sendPing(NodeNum serverNode)
{
    meshtastic_MeshPacket *p = allocatePacket(serverNode, meshtastic_PortNum_STORE_FORWARD_APP);

    // Create and populate ping
    meshtastic_StoreAndForward sf = meshtastic_StoreAndForward_init_default;
    sf.rr = meshtastic_StoreAndForward_RequestResponse_UNSET; // Use UNSET instead of REQUEST
    sf.which_payload = meshtastic_StoreAndForward_ping_tag;   // Use payload instead of variant

    // Encode to the packet
    pb_ostream_t stream = pb_ostream_from_buffer(p->decoded.payload.bytes, sizeof(p->decoded.payload.bytes));
    if (pb_encode(&stream, meshtastic_StoreAndForward_fields, &sf)) {
        p->decoded.payload.size = stream.bytes_written;
        service.sendToMesh(p);
    } else {
        logger.error("Ping too large for packet");
        packetPool.release(p); // Use global packetPool
    }
}

bool StoreForwardMessenger::sendPayload(NodeNum dest, uint8_t portNum, const uint8_t *payload, size_t len)
{
    meshtastic_MeshPacket *p = allocatePacket(dest, (meshtastic_PortNum)portNum);

    if (len > sizeof(p->decoded.payload.bytes)) {
        len = sizeof(p->decoded.payload.bytes);
    }

    memcpy(p->decoded.payload.bytes, payload, len);
    p->decoded.payload.size = len;

    service.sendToMesh(p);
    return true;
}

bool StoreForwardMessenger::sendText(NodeNum dest, uint8_t portNum, const char *text)
{
    size_t len = strlen(text) + 1; // Include null terminator
    return sendPayload(dest, portNum, (const uint8_t *)text, len);
}

bool StoreForwardMessenger::sendToNextHop(const meshtastic_MeshPacket &p)
{
    if (!router) {
        logger.error("Cannot send to next hop without router");
        return false;
    }

    // Create a copy of the packet
    meshtastic_MeshPacket *copy = router->allocForSending();
    memcpy(copy, &p, sizeof(meshtastic_MeshPacket));

    // Send to next hop
    service.sendToMesh(copy);
    return true;
}
