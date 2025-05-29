#pragma once

#include "../interfaces/IStoreForwardHistoryManager.h"
#include "../interfaces/IStoreForwardMessenger.h"
#include "../interfaces/IStoreForwardRole.h"
#include "../utils/StoreForwardLogger.h"

/**
 * Base class that implements common functionality for Store & Forward roles.
 * Both client and server roles inherit from this class.
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
    StoreForwardBaseRole(IStoreForwardHistoryManager &historyManager, IStoreForwardMessenger &messenger,
                         StoreForwardLogger &logger);

    // Common functionality that can be shared between client/server
    virtual void onRunOnce() override;
    virtual void onReceivePacket(const meshtastic_MeshPacket &packet) override;

  protected:
    IStoreForwardHistoryManager &historyManager;
    IStoreForwardMessenger &messenger;
    StoreForwardLogger &logger;

    // Last time status was logged
    unsigned long lastStatusLog = 0;
    static constexpr unsigned long STATUS_LOG_INTERVAL = 60000; // 1 minute

    // Common methods that can be used by derived classes
    virtual bool shouldProcessPacket(const meshtastic_MeshPacket &packet) const;
    virtual bool shouldStorePacket(const meshtastic_MeshPacket &packet) const;

    // Process a text message that might contain a command
    virtual void processTextCommand(const meshtastic_MeshPacket &packet);

    // Process a Store & Forward protocol message
    virtual void processProtocolMessage(const meshtastic_MeshPacket &packet, const meshtastic_StoreAndForward &data);

    // Default implementation returns false - derived classes must override if needed
    virtual bool isBusy() const { return false; }
};
