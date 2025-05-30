# Roles

This directory contains the implementation of different roles that a device can assume in the Store & Forward network.

## Key Classes

- **StoreForwardClient**: Implementation of the client role that requests messages from servers
- **StoreForwardServer**: Implementation of the server role that stores messages and delivers them on request
- **StoreForwardRoleFactory**: Factory class that creates the appropriate role based on configuration
- **StoreForwardBaseRole**: Common functionality shared between client and server roles

## Role Selection

The role is determined based on:

1. User configuration (is_server setting)
2. Available resources (memory)
3. Device capabilities

A device without sufficient resources will fall back to client mode even if server mode is requested.
