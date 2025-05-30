# Core

Core logic of Store & Forward:

- **StoreForwardHistoryManager**: Tracks message history and prevents duplicates
- **StoreForwardMessenger**: Abstracts message sending through the mesh network
- **StoreForwardProcessor**: Core logic for storing, retrieving, and managing messages

These components work together to provide the foundational capabilities needed for store-and-forward messaging.

## Key Patterns

- **Dependency Injection**: Classes accept interfaces rather than concrete implementations
- **Single Responsibility**: Each class has a clear, focused purpose
- **Interface-Based Design**: Implementation details are hidden behind interfaces
