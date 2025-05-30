#pragma once

#include "../interfaces/ILogger.h"
#include "../interfaces/IStoreForwardHistoryManager.h"
#include "../interfaces/IStoreForwardMessenger.h"
#include "../interfaces/IStoreForwardRole.h"
#include "mesh/generated/meshtastic/storeforward.pb.h"

/**
 * Base class for Store & Forward roles that implements common functionality
 * shared between client and server roles.
 */
class StoreForwardBaseRole : public IStoreForwardRole
{
  public:
    /**
     * Constructor
     * @param historyManager The history manager to use
     * @param messenger The messenger to use
     * @param logger The logger to use
     */
    StoreForwardBaseRole(IStoreForwardHistoryManager &historyManager, IStoreForwardMessenger &messenger, ILogger &logger);

    // IStoreForwardRole interface implementation
    virtual void onRunOnce() override;
    virtual void onReceivePacket(const meshtastic_MeshPacket &packet) override;

  protected:
    IStoreForwardHistoryManager &historyManager;
    IStoreForwardMessenger &messenger;
    ILogger &logger;

    // Common state tracking
    unsigned long lastStatusLog = 0;
    const unsigned long STATUS_LOG_INTERVAL = 60000; // 1 minute

    // Common methods shared by derived classes
    virtual void processTextCommand(const meshtastic_MeshPacket &packet);
    virtual void processProtocolMessage(const meshtastic_MeshPacket &packet, const meshtastic_StoreAndForward &data);
    virtual bool isBusy() const { return false; }
};
