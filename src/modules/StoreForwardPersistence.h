#pragma once

#include "StoreForwardModule.h"

// Namespace for Store & Forward persistence functions
namespace StoreForwardPersistence {
    // Function declarations
    void saveToFlash(StoreForwardModule *module);
    void loadFromFlash(StoreForwardModule *module);
}

// This file needs to be checked to ensure there are no extern declarations for lastSaveTime and messageCounter