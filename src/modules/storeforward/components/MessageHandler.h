#pragma once

#include "interfaces/ILogger.h"
#include "interfaces/IStoreForwardHistoryManager.h"
#include "interfaces/IStoreForwardMessenger.h"
#include "interfaces/ITimeProvider.h"
#include "mesh/generated/meshtastic/storeforward.pb.h"

/**
 * Handles message processing for Store & Forward
 * Encapsulates the logic for processing incoming messages
 */
class MessageHandler
{
  public:
    /**
     * Constructor
     * @param historyManager The history manager to use
     * @param messenger The messenger to use
     * @param timeProvider The time provider to use
     * @param logger The logger to use
     */
    MessageHandler(IStoreForwardHistoryManager &historyManager, IStoreForwardMessenger &messenger, ITimeProvider &timeProvider,
                   ILogger &logger);

    /**
     * Process a received text command (e.g., "SF", "SF reset")
     * @param packet The mesh packet containing the command
     * @param isBusy Whether the server is currently busy
     * @param historyReturnWindow The history return window in minutes
     * @param historyReturnMax The maximum number of messages to return
     * @return True if the server should become busy sending history
     */
    bool processTextCommand(const meshtastic_MeshPacket &packet, bool isBusy, uint32_t historyReturnWindow,
                            uint32_t historyReturnMax);

    /**
     * Process a protocol message (meshtastic_StoreAndForward)
     * @param packet The mesh packet
     * @param data The decoded protocol data
     * @param primaryServer The node ID of the primary server (for client mode)
     * @return The updated primary server node ID
     */
    NodeNum processProtocolMessage(const meshtastic_MeshPacket &packet, const meshtastic_StoreAndForward &data,
                                   NodeNum primaryServer);

    /**
     * Prepare to send history to a node
     * @param to Destination node ID
     * @param secondsAgo How far back in time to send messages from
     * @param outQueueSize Will be set to the number of messages that will be sent
     * @return True if there are messages to send
     */
    bool prepareHistorySend(NodeNum to, uint32_t secondsAgo, uint32_t &outQueueSize);

  private:
    IStoreForwardHistoryManager &historyManager;
    IStoreForwardMessenger &messenger;
    ITimeProvider &timeProvider;
    ILogger &logger;

    // For history sending
    uint32_t last_time = 0;
};
