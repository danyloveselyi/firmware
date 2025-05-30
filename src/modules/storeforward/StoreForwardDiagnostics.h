#pragma once

#include "interfaces/ILogger.h"

// Forward declaration of StoreForwardModule class
class StoreForwardModule;

/**
 * Diagnostic utility for Store & Forward functionality
 * This namespace provides tools to diagnose and fix Store & Forward issues
 */
namespace StoreForwardDiagnostics
{

/**
 * Prints detailed diagnostic information about the Store & Forward module
 * @param logger The logger to use for output
 * @param module The S&F module instance to diagnose
 */
void printDiagnostics(ILogger &logger, StoreForwardModule *module);

/**
 * Force a message to be stored in the S&F history
 * @param message Text message content
 * @param logger The logger to use for output
 * @param module The S&F module
 * @return true if successful
 */
bool forceStoreTestMessage(const char *message, ILogger &logger, StoreForwardModule *module);

/**
 * Fix common Store & Forward issues
 * @param logger The logger to use for output
 * @param module The S&F module
 * @return true if any fixes were applied
 */
bool applyFixes(ILogger &logger, StoreForwardModule *module);

} // namespace StoreForwardDiagnostics
