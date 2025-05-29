#pragma once

#include "../interfaces/ILogger.h"
#include "../interfaces/IStoreForwardHistoryManager.h"
#include "../interfaces/IStoreForwardMessenger.h"
#include <memory>

// Forward declaration
class IStoreForwardRole;

/**
 * Factory for creating the appropriate Store & Forward role based on configuration
 */
class StoreForwardRoleFactory
{
  public:
    /**
     * Constructor
     * @param logger The logger to use
     */
    explicit StoreForwardRoleFactory(ILogger &logger);

    /**
     * Create a role based on the provided configuration
     * @param messenger The messenger to use for communication
     * @param historyManager The history manager to use for storage
     * @param configIsServer Whether the role is configured as a server in settings
     * @param hasEnoughMemory Whether the device has enough memory for server operation
     * @return A unique_ptr to the created role, or nullptr if creation failed
     */
    std::unique_ptr<IStoreForwardRole> createRole(IStoreForwardMessenger &messenger, IStoreForwardHistoryManager &historyManager,
                                                  bool configIsServer, bool hasEnoughMemory);

    /**
     * Check if the device has enough memory for server operation
     * @return true if there is enough memory available
     */
    bool checkMemoryRequirements() const;

  private:
    ILogger &logger;
};
