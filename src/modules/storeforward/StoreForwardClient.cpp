#include "StoreForwardClient.h"
#include "MeshService.h"
#include "NodeDB.h"
#include "ProtobufModule.h"
#include "Router.h"
#include "configuration.h"
#include "mesh/generated/meshtastic/storeforward.pb.h"

// Updated constructor to use interface type
StoreForwardClient::StoreForwardClient(IStoreForwardMessenger &messenger) : messenger(messenger)
{
    LOG_INFO("S&F: Initializing Client mode");
}

void StoreForwardClient::onRunOnce()
{
    unsigned long now = millis();

    // Check for server availability
    if (lastHeartbeat > 0 && now - lastHeartbeat > heartbeatInterval * 2000) {
        // Lost connection to server, mark as unavailable
        if (serverAvailable) {
            LOG_INFO("S&F: Lost connection to server 0x%x", primaryServer);
            serverAvailable = false;
        }
    }

    // Handle retry logic
    if (retry_delay > 0 && now > retry_delay) {
        // Time to retry last request
        LOG_INFO("S&F: Retrying request to server");
        if (primaryServer != 0) {
            requestHistory(primaryServer);
        }
        retry_delay = 0;
    }
}

void StoreForwardClient::onReceivePacket(const meshtastic_MeshPacket &packet)
{
    // Check if this is a S&F specific message
    if (packet.which_payload_variant == meshtastic_MeshPacket_decoded_tag &&
        packet.decoded.portnum == meshtastic_PortNum_STORE_FORWARD_APP) {

        // Decode the S&F protobuf
        meshtastic_StoreAndForward data = meshtastic_StoreAndForward_init_zero;
        if (pb_decode_from_bytes(packet.decoded.payload.bytes, packet.decoded.payload.size, &meshtastic_StoreAndForward_msg,
                                 &data)) {
            processStoreForwardMessage(packet, data);
        }
    }
}

void StoreForwardClient::processStoreForwardMessage(const meshtastic_MeshPacket &packet, const meshtastic_StoreAndForward &data)
{
    switch (data.rr) {
    case meshtastic_StoreAndForward_RequestResponse_ROUTER_HEARTBEAT: {
        // Register the server and update heartbeat info
        primaryServer = packet.from;
        serverAvailable = true;
        lastHeartbeat = millis();

        // Update heartbeat interval if provided
        if (data.which_variant == meshtastic_StoreAndForward_heartbeat_tag) {
            heartbeatInterval = data.variant.heartbeat.period;
        }
        LOG_INFO("S&F: Received heartbeat from server 0x%x, interval %d sec", primaryServer, heartbeatInterval);
        break;
    }

    case meshtastic_StoreAndForward_RequestResponse_ROUTER_PING: {
        // Server is pinging us, respond with a pong
        LOG_INFO("S&F: Received ping from server 0x%x", packet.from);
        primaryServer = packet.from; // Update primary server
        serverAvailable = true;

        // Send pong response using messenger
        meshtastic_StoreAndForward response = meshtastic_StoreAndForward_init_zero;
        response.rr = meshtastic_StoreAndForward_RequestResponse_CLIENT_PONG;

        meshtastic_MeshPacket *p = router->allocForSending();
        p->to = packet.from;
        p->priority = meshtastic_MeshPacket_Priority_BACKGROUND;
        p->want_ack = false;
        p->decoded.portnum = meshtastic_PortNum_STORE_FORWARD_APP;
        p->decoded.payload.size = pb_encode_to_bytes(p->decoded.payload.bytes, sizeof(p->decoded.payload.bytes),
                                                     &meshtastic_StoreAndForward_msg, &response);
        service->sendToMesh(p);
        break;
    }

    case meshtastic_StoreAndForward_RequestResponse_ROUTER_HISTORY: {
        // Server is sending history info
        if (data.which_variant == meshtastic_StoreAndForward_history_tag) {
            LOG_INFO("S&F: Server 0x%x has %d messages for us from last %d minutes", packet.from,
                     data.variant.history.history_messages, data.variant.history.window / 60000);
        }
        break;
    }

    case meshtastic_StoreAndForward_RequestResponse_ROUTER_STATS: {
        // Server is sending stats
        if (data.which_variant == meshtastic_StoreAndForward_stats_tag) {
            LOG_INFO("S&F: Server 0x%x stats - Messages: %d/%d, Uptime: %d sec", packet.from, data.variant.stats.messages_saved,
                     data.variant.stats.messages_max, data.variant.stats.up_time);
        }
        break;
    }

    case meshtastic_StoreAndForward_RequestResponse_ROUTER_TEXT_DIRECT:
    case meshtastic_StoreAndForward_RequestResponse_ROUTER_TEXT_BROADCAST: {
        // Server is forwarding a message to us
        if (data.which_variant == meshtastic_StoreAndForward_text_tag) {
            LOG_INFO("S&F: Received forwarded message via server 0x%x", packet.from);

            // Create a regular text message packet
            meshtastic_MeshPacket *p = router->allocForSending();

            // Get sender info from the server packet
            p->from = packet.from;
            p->to = (data.rr == meshtastic_StoreAndForward_RequestResponse_ROUTER_TEXT_BROADCAST) ? NODENUM_BROADCAST
                                                                                                  : nodeDB->getNodeNum();

            p->decoded.portnum = meshtastic_PortNum_TEXT_MESSAGE_APP;
            p->decoded.payload.size = data.variant.text.size;
            memcpy(p->decoded.payload.bytes, data.variant.text.bytes, data.variant.text.size);

            // Send to our own message handling system (will get forwarded to phone, etc)
            service->sendToMesh(p);
        }
        break;
    }

    case meshtastic_StoreAndForward_RequestResponse_ROUTER_ERROR:
    case meshtastic_StoreAndForward_RequestResponse_ROUTER_BUSY: {
        // Server is busy or encountered an error
        LOG_WARN("S&F: Server 0x%x is busy or encountered an error, retrying later", packet.from);
        // Schedule a retry
        retry_delay = millis() + 30000; // Try again in 30 seconds
        break;
    }

    default:
        break;
    }
}

void StoreForwardClient::requestHistory(NodeNum serverNode, uint32_t minutes)
{
    if (serverNode == 0 && primaryServer != 0) {
        serverNode = primaryServer;
    }

    if (serverNode == 0) {
        LOG_WARN("S&F: No server specified and no primary server known");
        return;
    }

    lastRequestTime = millis();
    messenger.requestHistory(serverNode, minutes);
}

void StoreForwardClient::requestStats(NodeNum serverNode)
{
    if (serverNode == 0 && primaryServer != 0) {
        serverNode = primaryServer;
    }

    messenger.requestStats(serverNode);
}

void StoreForwardClient::sendPing(NodeNum serverNode)
{
    if (serverNode == 0 && primaryServer != 0) {
        serverNode = primaryServer;
    }

    messenger.sendPing(serverNode);
}
