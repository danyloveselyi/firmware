#pragma once

#include "NodeDB.h" // Include for NodeNum type
#include "mesh/generated/meshtastic/mesh.pb.h"

/**
 * Interface for Store & Forward role implementations (client or server)
 *
 * This interface defines the common operations that any Store & Forward
 * role must implement, regardless of whether it's a client or server.
 */
class IStoreForwardRole
{
  public:
    /**
     * Perform periodic operations for this role
     * Called regularly by the main module's runOnce method
     */
    virtual void onRunOnce() = 0;

    /**
     * Process a received packet for this role
     * @param packet The received mesh packet
     */
    virtual void onReceivePacket(const meshtastic_MeshPacket &packet) = 0;

    /**
     * Destructor
     */
    virtual ~IStoreForwardRole() = default;
};
