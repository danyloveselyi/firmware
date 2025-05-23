#pragma once

#include "StoreForwardModule.h"

/**
 * Class for handling persistence of Store & Forward data
 */
class StoreForwardPersistence
{
  public:
    /**
     * Save the Store & Forward message history to flash
     * @param module Pointer to the module whose history should be saved
     * @return true if successful, false otherwise
     */
    static bool saveToFlash(StoreForwardModule *module);

    /**
     * Load the Store & Forward message history from flash
     * @param module Pointer to the module whose history should be loaded
     * @return true if successful, false otherwise
     */
    static bool loadFromFlash(StoreForwardModule *module);

    /**
     * Remove the Store & Forward message history file
     * @return true if successful or if file doesn't exist, false otherwise
     */
    static bool clearFlash();
};