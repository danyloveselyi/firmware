#pragma once

#include "MeshTypes.h"

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
     * Destructor
     */
    virtual ~IStoreForwardRole() = default;

    /**
     * Process a received packet for this role
     * @param packet The received mesh packet
     */
    virtual void onReceivePacket(const meshtastic_MeshPacket &packet) = 0;

    /**
     * Perform periodic operations for this role
     * Called regularly by the main module's runOnce method
     */
    virtual void onRunOnce() = 0;
};
