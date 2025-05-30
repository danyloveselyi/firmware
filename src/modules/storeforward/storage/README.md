# Storage

This directory contains code related to persistent storage for the Store & Forward module.

## Key Components

- **StoreForwardPersistence**: Utilities for saving and loading messages to/from persistent storage
- **FileSystemStorageBackend**: Implementation of the storage backend using the filesystem

## Persistence Strategy

Messages are persisted to flash storage to survive device reboots. The persistence mechanism:

1. Serializes messages to binary format
2. Stores them in the filesystem
3. Maintains metadata for efficient retrieval
4. Loads messages back into memory on startup

The persistence layer is designed to be resilient against power failures and to minimize flash wear.
