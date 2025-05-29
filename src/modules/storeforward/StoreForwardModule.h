#pragma once

// Include our interfaces
#include "IStoreForwardRole.h"
#include "ProtobufModule.h"
#include "StoreForwardRoleFactory.h"
#include "concurrency/OSThread.h"
#include "interfaces/ILogger.h"
#include "interfaces/IStoreForwardHistoryManager.h"
#include "interfaces/IStoreForwardMessenger.h"
#include "mesh/generated/meshtastic/storeforward.pb.h"
#include <memory>

// Forward declaration
class StoreForwardModule;
extern StoreForwardModule *storeForwardModule;

/**
 * Main module class for Store & Forward functionality.
 * Acts as a coordinator between history management, messaging, and role implementations.
 */
class StoreForwardModule : public ProtobufModule<meshtastic_StoreAndForward>, public concurrency::OSThread
{
  public:
    /**
     * Constructor with dependency injection
     *
     * @param messenger The messenger component for network communication
     * @param historyManager The history manager for message storage
     * @param roleFactory The factory for creating appropriate roles
     * @param logger The logger for diagnostic output
     */
    StoreForwardModule(std::unique_ptr<IStoreForwardMessenger> messenger,
                       std::unique_ptr<IStoreForwardHistoryManager> historyManager,
                       std::unique_ptr<StoreForwardRoleFactory> roleFactory, ILogger &logger);

    /**
     * Legacy constructor that creates its own dependencies
     * (maintained for backward compatibility)
     */
    StoreForwardModule();

    ~StoreForwardModule();

    /**
     * Static factory method to create a module with default dependencies
     * @return A newly created StoreForwardModule instance
     */
    static StoreForwardModule *createWithDefaultDependencies();

    // Override from OSThread
    int32_t runOnce() override;

    // Override from ProtobufModule
    bool handleReceivedProtobuf(const meshtastic_MeshPacket &mp, meshtastic_StoreAndForward *decoded) override;

    // Our own methods
    void onReceivePacket(const meshtastic_MeshPacket &packet);

    // For PhoneAPI
    meshtastic_MeshPacket *getForPhone();

    /**
     * Reconfigures the module with new settings from moduleConfig
     * This allows runtime switching between client/server or changing parameters
     *
     * @return true if reconfiguration was successful
     */
    bool reconfigureRole();

    /**
     * Resets the module's state (useful for testing)
     */
    void reset();

    // Role configuration
    bool isServerMode() const { return isServer; }
    bool isClientMode() const { return isClient; }

    // Accessors for testing/diagnostics
    IStoreForwardHistoryManager *getHistoryManager() const { return historyManager.get(); }
    IStoreForwardMessenger *getMessenger() const { return messenger.get(); }

  private:
    // Components
    std::unique_ptr<IStoreForwardMessenger> messenger;
    std::unique_ptr<IStoreForwardHistoryManager> historyManager;
    std::unique_ptr<StoreForwardRoleFactory> roleFactory;
    std::unique_ptr<IStoreForwardRole> role;
    ILogger &logger;

    // State tracking
    bool isServer = false;
    bool isClient = false;

    // Time constants
    static constexpr int32_t ACTIVE_POLL_INTERVAL_MS = 5000;    // 5 seconds
    static constexpr int32_t INACTIVE_POLL_INTERVAL_MS = 30000; // 30 seconds

    /**
     * Initialize role based on configuration
     * @return true if initialization was successful
     */
    bool initializeRole();
};
