#pragma once

#include "IStoreForwardRole.h"
#include "interfaces/ILogger.h"
#include "interfaces/IStoreForwardHistoryManager.h"
#include "interfaces/IStoreForwardMessenger.h"
#include <memory>

/**
 * Factory class for creating Store & Forward roles based on configuration.
 * Isolates the role creation logic from the main module.
 */
class StoreForwardRoleFactory
{
  public:
    StoreForwardRoleFactory(ILogger &logger);

    /**
     * Create an appropriate role (client or server) based on configuration
     *
     * @param messenger The messenger implementation to use
     * @param historyManager The history manager implementation to use
     * @param isServerConfigured Whether server mode is configured
     * @param hasEnoughMemory Whether enough memory is available for server mode
     * @return A unique_ptr to the created role implementation
     */
    std::unique_ptr<IStoreForwardRole> createRole(IStoreForwardMessenger &messenger, IStoreForwardHistoryManager &historyManager,
                                                  bool isServerConfigured, bool hasEnoughMemory);

    /**
     * Check if the system has enough memory for server mode
     *
     * @return true if enough memory is available
     */
    bool checkMemoryRequirements() const;

  private:
    ILogger &logger;
};
