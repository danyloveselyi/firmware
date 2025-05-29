#pragma once

#include "mesh/generated/meshtastic/mesh.pb.h"

/**
 * Interface for network routing operations
 * This abstracts the low-level packet routing functionality
 */
class INetworkRouter
{
  public:
    virtual ~INetworkRouter() = default;

    /**
     * Allocate a packet for sending
     * @return A newly allocated packet
     */
    virtual meshtastic_MeshPacket *allocForSending() = 0;

    /**
     * Send a packet to the mesh network
     * @param packet The packet to send
     */
    virtual void sendToMesh(meshtastic_MeshPacket *packet) = 0;

    /**
     * Cancel a pending packet
     * @param from Source node ID
     * @param id Packet ID
     * @return true if a packet was found and canceled
     */
    virtual bool cancelSending(NodeNum from, PacketId id) = 0;
};
