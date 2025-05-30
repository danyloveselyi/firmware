#pragma once

#include "NodeDB.h"
#include <cstdint>

// Forward declaration of the processor class
class StoreForwardProcessor;

namespace StoreForwardPersistence
{

/**
 * Save the stored messages to flash
 * @param processor The StoreForwardProcessor instance
 */
void saveToFlash(StoreForwardProcessor *processor);

/**
 * Load the stored messages from flash
 * @param processor The StoreForwardProcessor instance to populate
 */
void loadFromFlash(StoreForwardProcessor *processor);

} // namespace StoreForwardPersistence
