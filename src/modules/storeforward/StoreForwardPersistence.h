#pragma once

#include "mesh/generated/meshtastic/mesh.pb.h" // Include the mesh packet definition

// Forward declaration of classes
class StoreForwardModule;
class StoreForwardProcessor;
class StoreForwardHistoryManager;
class IStoreForwardHistoryManager;

/**
 * Namespace containing persistence operations for Store & Forward
 */
namespace StoreForwardPersistence
{
/**
 * Save the message history to persistent storage
 * @param module Pointer to the module containing messages to save
 */
void saveToFlash(StoreForwardModule *module);

/**
 * Load the message history from persistent storage
 * @param module Pointer to the module where messages will be loaded
 */
void loadFromFlash(StoreForwardModule *module);

/**
 * Save the message history to persistent storage
 * @param processor Pointer to the processor containing messages to save
 */
void saveToFlash(StoreForwardProcessor *processor);

/**
 * Load the message history from persistent storage
 * @param processor Pointer to the processor where messages will be loaded
 */
void loadFromFlash(StoreForwardProcessor *processor);

/**
 * Save the message history to persistent storage
 * @param manager Pointer to the history manager containing messages to save
 */
void saveToFlash(StoreForwardHistoryManager *manager);

/**
 * Save the message history to persistent storage using the interface
 * @param manager Pointer to the history manager interface containing messages to save
 */
void saveToFlash(IStoreForwardHistoryManager *manager);

/**
 * Load the message history from persistent storage
 * @param manager Pointer to the history manager where messages will be loaded
 */
void loadFromFlash(StoreForwardHistoryManager *manager);

/**
 * Load the message history from persistent storage using the interface
 * @param manager Pointer to the history manager interface where messages will be loaded
 */
void loadFromFlash(IStoreForwardHistoryManager *manager);

/**
 * Debug helper to log a message content
 * @param msg The message to log
 * @param index The index of the message (for debugging)
 */
void logMessageContent(const meshtastic_MeshPacket *msg, int index);
} // namespace StoreForwardPersistence
