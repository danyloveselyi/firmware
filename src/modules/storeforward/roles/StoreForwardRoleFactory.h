#pragma once

#include "../interfaces/ILogger.h"
#include "../interfaces/IStoreForwardHistoryManager.h"
#include "../interfaces/IStoreForwardMessenger.h"
#include "../interfaces/IStoreForwardRole.h"
#include <memory>

// Define the role types
enum class StoreForwardRoleType {
    SERVER,  // Acts as a store & forward server
    CLIENT,  // Acts as a client to store & forward servers
    RELAY,   // Acts as a relay only
    INACTIVE // Role is disabled
};

/**
 * Factory for creating Store & Forward roles
 */
class StoreForwardRoleFactory
{
  public:
    /**
     * Constructor
     * @param logger The logger to use
     */
    explicit StoreForwardRoleFactory(ILogger &logger) : logger(logger) {}

    /**
     * Create a role implementation based on the specified type
     * @param messenger The messenger to use for network communication
     * @param historyManager The history manager to use for message storage
     * @param requestedType The type of role to create
     * @param hasEnoughMemory Whether there is enough memory for the requested role
     * @return A role implementation or nullptr if the role could not be created
     */
    std::unique_ptr<IStoreForwardRole> createRole(IStoreForwardMessenger &messenger, IStoreForwardHistoryManager &historyManager,
                                                  StoreForwardRoleType requestedType, bool hasEnoughMemory);

    /**
     * Convert config boolean values to a role type
     * @param isServer Whether the node is configured as a server
     * @param isEnabled Whether the module is enabled
     * @return The appropriate role type
     */
    static StoreForwardRoleType configToRoleType(bool isServer, bool isEnabled);

    /**
     * Check if there is enough memory available for the requested role
     * @return true if there is enough memory, false otherwise
     */
    bool checkMemoryRequirements() const;

  private:
    ILogger &logger;
};
