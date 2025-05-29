#include "StoreForwardRoleFactory.h"     // Update to local path (same directory)
#include "../utils/StoreForwardLogger.h" // Path relative to current directory
#include "StoreForwardClient.h"          // Include from same directory
#include "StoreForwardServer.h"          // Include from same directory
#include "configuration.h"
#include <memory>

#ifdef ARCH_ESP32
#include "memory/MemoryPool.h"
extern MemoryDynamic *memGet; // from main.cpp
#endif

StoreForwardRoleFactory::StoreForwardRoleFactory(ILogger &logger) : logger(logger) {}

std::unique_ptr<IStoreForwardRole> StoreForwardRoleFactory::createRole(IStoreForwardMessenger &messenger,
                                                                       IStoreForwardHistoryManager &historyManager,
                                                                       bool isServerConfigured, bool hasEnoughMemory)
{
    const bool enabled = moduleConfig.store_forward.enabled;

    if (!enabled) {
        logger.info("S&F: Module is disabled");
        return nullptr;
    }

    // Check if we have sufficient memory for server mode
    bool memoryAvailable = checkMemoryRequirements();
    hasEnoughMemory &= memoryAvailable;

    if (isServerConfigured && hasEnoughMemory) {
        logger.info("S&F: Creating SERVER role");
        return std::make_unique<StoreForwardServer>(historyManager, messenger);
    } else {
        logger.info("S&F: Creating CLIENT role%s",
                    (isServerConfigured && !hasEnoughMemory) ? " (insufficient memory for server)" : "");
        return std::make_unique<StoreForwardClient>(messenger);
    }
}

bool StoreForwardRoleFactory::checkMemoryRequirements() const
{
#ifdef ARCH_ESP32
    if (memGet && memGet->getPsramSize() > 0) {
        return memGet->getFreePsram() >= 1024 * 1024; // At least 1MB free
    } else {
        return false; // No PSRAM
    }
#else
    return true; // On other platforms, assume we have enough memory
#endif
}
