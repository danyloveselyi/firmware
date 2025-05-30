#include "StoreForwardRoleFactory.h"
#include "StoreForwardClient.h"
#include "StoreForwardServer.h"
#include "configuration.h"
#include <memory>

StoreForwardRoleFactory::StoreForwardRoleFactory(ILogger &logger) : logger(logger) {}

std::unique_ptr<IStoreForwardRole> StoreForwardRoleFactory::createRole(IStoreForwardMessenger &messenger,
                                                                       IStoreForwardHistoryManager &historyManager,
                                                                       StoreForwardRoleType requestedType, bool hasEnoughMemory)
{
    bool memoryAvailable = checkMemoryRequirements();

    // First check if the module is disabled
    if (requestedType == StoreForwardRoleType::INACTIVE) {
        logger.info("S&F: Role set to INACTIVE");
        return nullptr;
    }

    // Check if we have enough memory to run as a server
    if (requestedType == StoreForwardRoleType::SERVER && (!hasEnoughMemory || !memoryAvailable)) {
        logger.warn("S&F: Insufficient memory for server role, falling back to client");
        requestedType = StoreForwardRoleType::CLIENT;
    }

    // Create the appropriate role based on the type
    switch (requestedType) {
    case StoreForwardRoleType::SERVER:
        logger.info("S&F: Creating SERVER role");
        return std::unique_ptr<IStoreForwardRole>(new StoreForwardServer(historyManager, messenger));

    case StoreForwardRoleType::CLIENT:
        logger.info("S&F: Creating CLIENT role");
        return std::unique_ptr<IStoreForwardRole>(new StoreForwardClient(messenger));

    case StoreForwardRoleType::RELAY:
        logger.info("S&F: RELAY role not yet implemented, falling back to CLIENT");
        return std::unique_ptr<IStoreForwardRole>(new StoreForwardClient(messenger));

    default:
        logger.error("S&F: Unknown role type, falling back to CLIENT");
        return std::unique_ptr<IStoreForwardRole>(new StoreForwardClient(messenger));
    }
}

StoreForwardRoleType StoreForwardRoleFactory::configToRoleType(bool isServer, bool isEnabled)
{
    if (!isEnabled)
        return StoreForwardRoleType::INACTIVE;

    if (isServer)
        return StoreForwardRoleType::SERVER;

    return StoreForwardRoleType::CLIENT;
}

bool StoreForwardRoleFactory::checkMemoryRequirements() const
{
    // Check for available memory to run as a server
    // This is a simplified version for now
    return true;
}
