# Interfaces

This directory contains interface definitions that establish contracts between different components of the Store & Forward module.

## Key Interfaces

- **ILogger**: Logging interface for consistent logging across the module
- **IStoreForwardHistoryManager**: Interface for tracking message history and preventing duplicates
- **IStoreForwardMessenger**: Interface for sending messages through the mesh network
- **IStoreForwardProcessor**: Interface for the core processing logic
- **IStoreForwardRole**: Interface that defines the contract for client and server roles
- **IStorageBackend**: Interface for persistent storage operations
- **ITimeProvider**: Interface for time-related operations

## Design Philosophy

These interfaces follow the Interface Segregation Principle, ensuring that clients only depend on methods they actually use.
