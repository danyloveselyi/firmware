#include "StoreForwardModule.h"
#include "NodeDB.h"
#include "StoreForwardClient.h"
#include "StoreForwardHistoryManager.h" // Include concrete implementation
#include "StoreForwardMessenger.h"      // Include concrete implementation
#include "StoreForwardServer.h"
#include "configuration.h"
#include "mesh/generated/meshtastic/storeforward.pb.h"
#include "utils/DefaultLogger.h"

// External references to global service and router pointers
extern Router *router;
extern MeshService *service;

// Global instance
StoreForwardModule *storeForwardModule = nullptr;

StoreForwardModule::StoreForwardModule(std::unique_ptr<IStoreForwardMessenger> messenger,
                                       std::unique_ptr<IStoreForwardHistoryManager> historyManager,
                                       std::unique_ptr<StoreForwardRoleFactory> roleFactory, ILogger &logger)
    : ProtobufModule("storeforward", meshtastic_PortNum_STORE_FORWARD_APP, &meshtastic_StoreAndForward_msg),
      OSThread("StoreForwardModule"), messenger(std::move(messenger)), historyManager(std::move(historyManager)),
      roleFactory(std::move(roleFactory)), logger(logger)
{
    // Initialize role based on configuration
    initializeRole();
}

// Legacy constructor that creates its own dependencies
StoreForwardModule::StoreForwardModule()
    : ProtobufModule("storeforward", meshtastic_PortNum_STORE_FORWARD_APP, &meshtastic_StoreAndForward_msg),
      OSThread("StoreForwardModule"), logger(defaultLogger)
{
    // Create a messenger that uses the global router and service
    messenger = std::make_unique<StoreForwardMessenger>(*router, *service, logger);

    // Create a history manager that uses our logger
    historyManager = std::make_unique<StoreForwardHistoryManager>(logger);

    // Create the role factory
    roleFactory = std::make_unique<StoreForwardRoleFactory>(logger);

    // Initialize role based on configuration
    initializeRole();
}

StoreForwardModule::~StoreForwardModule()
{
    // Clean up resources if needed
}

// Changed to return bool as per header declaration
bool StoreForwardModule::initializeRole()
{
    const bool enabled = moduleConfig.store_forward.enabled;
    const bool configIsServer = moduleConfig.store_forward.is_server;

    // Reset flags
    isServer = false;
    isClient = false;

    if (!enabled) {
        logger.info("S&F: Module is disabled");
        role.reset(); // Clear any existing role
        return false;
    }

    // Check if we have sufficient memory for server mode
    bool hasEnoughMemory = true;
#ifdef ARCH_ESP32
    if (memGet.getPsramSize() > 0) {
        hasEnoughMemory = memGet.getFreePsram() >= 1024 * 1024; // At least 1MB free
    } else {
        hasEnoughMemory = false; // No PSRAM
    }
#endif

    // Use the factory to create the appropriate role
    role = roleFactory->createRole(*messenger, *historyManager, configIsServer, hasEnoughMemory);

    if (role) {
        isServer = configIsServer && hasEnoughMemory;
        isClient = !isServer && enabled;
        return true;
    }
    return false;
}

// Changed to return bool as per header declaration
bool StoreForwardModule::reconfigureRole()
{
    logger.info("S&F: Reconfiguring role based on new settings");
    return initializeRole();
}

int32_t StoreForwardModule::runOnce()
{
    if (moduleConfig.store_forward.enabled && role) {
        role->onRunOnce();
        return ACTIVE_POLL_INTERVAL_MS;
    }

    return INACTIVE_POLL_INTERVAL_MS;
}

void StoreForwardModule::onReceivePacket(const meshtastic_MeshPacket &packet)
{
    if (role) {
        role->onReceivePacket(packet);
    }
}

bool StoreForwardModule::handleReceivedProtobuf(const meshtastic_MeshPacket &mp, meshtastic_StoreAndForward *decoded)
{
    // Only handle Store & Forward protocol messages
    if (mp.decoded.portnum == meshtastic_PortNum_STORE_FORWARD_APP) {
        // Pass the packet to our implementation role through onReceivePacket
        onReceivePacket(mp);

        // We've handled this message
        return true;
    }

    // Not for us, let other modules handle it
    return false;
}

meshtastic_MeshPacket *StoreForwardModule::getForPhone()
{
    if (!isServer || !historyManager) {
        return nullptr;
    }

    // If we're a server, forward stored messages to the phone
    StoreForwardServer *server = dynamic_cast<StoreForwardServer *>(role.get());
    if (!server) {
        return nullptr;
    }

    // First, check if we're already busy sending to someone else
    if (server->isBusy() && server->getBusyRecipient() != nodeDB->getNodeNum()) {
        return nullptr;
    }

    // Get messages relevant for this node
    NodeNum ourNodeNum = nodeDB->getNodeNum();

    // If we're not already busy sending to our node, check if we have any packets
    if (!server->isBusy()) {
        uint32_t msgCount = historyManager->getNumAvailablePackets(ourNodeNum, 0);
        if (msgCount == 0) {
            return nullptr; // No messages for us
        }

        // Start sending history to ourself
        server->historySend(ourNodeNum, 0); // 0 means no time limit
    }

    // Generate a payload packet
    return server->prepareHistoryPayload(ourNodeNum, server->getRequestCount());
}

void StoreForwardModule::reset()
{
    // Re-initialize everything
    if (historyManager) {
        historyManager->clearStorage();
    }
    initializeRole();
}

// Add a static factory method to create a module with default dependencies
StoreForwardModule *StoreForwardModule::createWithDefaultDependencies()
{
    // Create a logger
    auto logger = &defaultLogger;

    // Create a messenger that uses the global router and service directly
    auto messenger = std::make_unique<StoreForwardMessenger>(*router, *service, *logger);

    // Create a history manager that uses our logger
    auto historyManager = std::make_unique<StoreForwardHistoryManager>(*logger);

    // Create the role factory
    auto roleFactory = std::make_unique<StoreForwardRoleFactory>(*logger);

    // Create and return the module
    return new StoreForwardModule(std::move(messenger), std::move(historyManager), std::move(roleFactory), *logger);
}
