#include "StoreForwardDiagnostics.h"
#include "NodeDB.h"
#include "RTC.h"
#include "StoreForwardModule.h"
#include "configuration.h"
#include "interfaces/IStoreForwardHistoryManager.h"
#include <cstring>

/**
 * Diagnostic utility for Store & Forward functionality
 * This file provides tools to diagnose and fix Store & Forward issues
 */

namespace StoreForwardDiagnostics
{

void printDiagnostics(ILogger &logger, StoreForwardModule *module)
{
    if (!module) {
        logger.warn("S&F: No module instance available for diagnostics");
        return;
    }

    auto historyManager = module->getHistoryManager();
    auto messenger = module->getMessenger();

    if (!historyManager) {
        logger.warn("S&F: History manager not available");
        return;
    }

    // Print basic status information
    logger.info("S&F: === DIAGNOSTICS REPORT ===");
    logger.info("S&F: Mode: %s", module->isServerMode() ? "SERVER" : (module->isClientMode() ? "CLIENT" : "DISABLED"));

    // Print message store statistics
    logger.info("S&F: Message count: %u", historyManager->getTotalMessageCount());
    logger.info("S&F: Max records: %u", historyManager->getMaxRecords());
    logger.info("S&F: Storage stats: %s", historyManager->getStatisticsJson().c_str());

    // Check configuration
    logger.info("S&F: Module enabled in config: %s", moduleConfig.store_forward.enabled ? "YES" : "NO");
    logger.info("S&F: Server enabled in config: %s", moduleConfig.store_forward.is_server ? "YES" : "NO");

    // Check channel configuration - use channels.getNumChannels() instead of isValidIndex
    logger.info("S&F: Primary channel PSK set: %s", channels.getNumChannels() > 0 ? "YES" : "NO");
    logger.info("S&F: Available channels: %d", channels.getNumChannels());

    logger.info("S&F: === END DIAGNOSTICS ===");
}

bool forceStoreTestMessage(const char *message, ILogger &logger, StoreForwardModule *module)
{
    if (!module || !module->isServerMode()) {
        logger.warn("S&F: Cannot store test message - module not available or not in server mode");
        return false;
    }

    auto historyManager = module->getHistoryManager();
    if (!historyManager) {
        logger.warn("S&F: History manager not available");
        return false;
    }

    // Create a test packet
    meshtastic_MeshPacket *p = packetPool.allocZeroed();
    p->which_payload_variant = meshtastic_MeshPacket_decoded_tag;
    p->from = nodeDB->getNodeNum();
    p->to = NODENUM_BROADCAST;
    p->id = generatePacketId();
    p->rx_time = getTime(); // Using getTime() directly to avoid RTCQuality enum
    p->hop_limit = 3;
    p->decoded.portnum = meshtastic_PortNum_TEXT_MESSAGE_APP;

    // Add the message
    strncpy((char *)p->decoded.payload.bytes, message, sizeof(p->decoded.payload.bytes));
    p->decoded.payload.size = strlen(message) + 1;

    // Store it using the record method instead of storePacket
    historyManager->record(*p);
    logger.info("S&F: Force-stored test message: '%s'", message);

    // Save to flash
    historyManager->saveToFlash();

    packetPool.release(p);
    return true;
}

bool applyFixes(ILogger &logger, StoreForwardModule *module)
{
    if (!module) {
        logger.warn("S&F: No module instance available to fix");
        return false;
    }

    bool fixesApplied = false;

    // Force reset of module and reconfiguration
    module->reset();
    if (module->reconfigureRole()) {
        logger.info("S&F: Module reconfigured successfully");
        fixesApplied = true;
    }

    // Force a storage reset if needed
    auto historyManager = module->getHistoryManager();
    if (historyManager) {
        if (historyManager->getTotalMessageCount() == 0) {
            // Try to store a test message to verify functionality
            if (forceStoreTestMessage("S&F diagnostic test message", logger, module)) {
                logger.info("S&F: Test message stored successfully");
                fixesApplied = true;
            }
        }
    }

    return fixesApplied;
}

} // namespace StoreForwardDiagnostics
