#pragma once

#include "../interfaces/ILogger.h"
#include "../interfaces/IStoreForwardHistoryManager.h"
#include "../interfaces/IStoreForwardMessenger.h"
#include "../interfaces/IStoreForwardRole.h"
#include <memory>

/**
 * Enumeration of different role types
 */
enum class StoreForwardRoleType {
    CLIENT,  // Standard client role
    SERVER,  // Full server role
    RELAY,   // Relay role (future expansion)
    INACTIVE // Role is disabled
};

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
     * Create a role based on the provided configuration and system capabilities
     * @param messenger The messenger to use for communication
     * @param historyManager The history manager to use for storage
     * @param requestedType The requested role type based on configuration
     * @param hasEnoughMemory Whether the device has enough memory for server operation
     * @return A unique_ptr to the created role, or nullptr if creation failed
     */
    std::unique_ptr<IStoreForwardRole> createRole(IStoreForwardMessenger &messenger, IStoreForwardHistoryManager &historyManager,
                                                  StoreForwardRoleType requestedType, bool hasEnoughMemory);

    /**
     * Convert boolean configuration to role type
     * @param isServer Whether server mode is requested in configuration
     * @param isEnabled Whether the module is enabled
     * @return The appropriate role type
     */
    static StoreForwardRoleType configToRoleType(bool isServer, bool isEnabled);

    /**
     * Check if the device has enough memory for server operation
     * @return true if there is enough memory available
     */
    bool checkMemoryRequirements() const;

  private:
    ILogger &logger;
};
