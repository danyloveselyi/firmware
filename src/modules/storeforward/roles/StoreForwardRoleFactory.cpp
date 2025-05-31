#include "StoreForwardRoleFactory.h"
#include "StoreForwardClient.h"
#include "StoreForwardServer.h"
#include <memory>

// Constructor is already defined in the header file, no need to redefine it here

std::unique_ptr<IStoreForwardRole> StoreForwardRoleFactory::createRole(IStoreForwardMessenger &messenger,
                                                                       IStoreForwardHistoryManager &historyManager,
                                                                       StoreForwardRoleType requestedType, bool hasEnoughMemory)
{
    // If we're creating a server, we need to make sure we have enough memory
    bool memoryAvailable = hasEnoughMemory;
    if (!memoryAvailable && requestedType == StoreForwardRoleType::SERVER) {
        logger.warn("S&F: Not enough memory for server role, falling back to client");
    }

    if (requestedType == StoreForwardRoleType::INACTIVE) {
        logger.info("S&F: Creating inactive role");
        return nullptr; // No role implementation for inactive
    }

    switch (requestedType) {
    case StoreForwardRoleType::SERVER:
        if (memoryAvailable) {
            logger.info("S&F: Creating server role");
            return std::make_unique<StoreForwardServer>(messenger, historyManager, logger);
        } else {
            logger.warn("S&F: Not enough memory for server role, falling back to client");
            // Fall through to client case
        }
        [[fallthrough]];

    case StoreForwardRoleType::CLIENT:
        logger.info("S&F: Creating client role");
        return std::make_unique<StoreForwardClient>(messenger, historyManager, logger);

    case StoreForwardRoleType::RELAY:
        logger.info("S&F: Creating relay role");
        // No specific relay role yet, use client for now
        return std::make_unique<StoreForwardClient>(messenger, historyManager, logger);

    default:
        logger.error("S&F: Unknown role type requested");
        return nullptr;
    }
}

StoreForwardRoleType StoreForwardRoleFactory::configToRoleType(bool isServer, bool isEnabled)
{
    if (!isEnabled) {
        return StoreForwardRoleType::INACTIVE;
    }

    if (isServer) {
        return StoreForwardRoleType::SERVER;
    }

    return StoreForwardRoleType::CLIENT;
}

bool StoreForwardRoleFactory::checkMemoryRequirements() const
{
    // Check if we have enough memory for the server role
    // This is a placeholder implementation
    return true;
}
