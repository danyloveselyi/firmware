# Store & Forward Module

The Store & Forward module enables devices to store messages when recipients are offline and forward them when they reconnect to the network.

## Structure

- **core/**: Core implementations of storage, messaging, and history tracking
- **interfaces/**: Interface definitions that define contracts for the module's components
- **roles/**: Client and server role implementations
- **storage/**: Persistence and storage management
- **utils/**: Utility classes and helpers

## Architecture

The module uses a role-based architecture where a device can be either a:

- **Server**: Stores messages and delivers them on request
- **Client**: Requests stored messages from servers

The architecture is designed to be memory-efficient and robust to support devices with limited resources.

## Configuration

Configuration is done through the `store_forward` section in the Meshtastic config.
