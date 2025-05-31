#!/usr/bin/env bash

# Ensure script runs with elevated privileges
if [ "$EUID" -ne 0 ]; then
    echo "Requesting elevated privileges to deploy meshtasticd..."
    exec sudo "$0" "$@"
    exit $?
fi

echo "Starting deployment with root privileges..."

# Check if we're on Raspberry Pi
IS_RASPBERRY_PI=false
if [ -f /proc/device-tree/model ] && grep -q "Raspberry Pi" /proc/device-tree/model; then
    IS_RASPBERRY_PI=true
    echo "Raspberry Pi environment detected..."
fi

# Check if we're using systemd
USE_SYSTEMD=false
if ps -p 1 -o comm= | grep -q "systemd" && command -v systemctl &> /dev/null && systemctl 2>&1 | grep -q "running"; then
    USE_SYSTEMD=true
    echo "Using systemd for service management..."
else
    echo "Alternative service management will be used..."
fi

# Function to list services
list_services() {
    echo "Services via systemctl:"
    systemctl list-units --type=service --all 2>/dev/null | grep -v "loaded units listed" || echo "  No services found"

    if [ -d /etc/init.d ]; then
        echo "Services in /etc/init.d:"
        ls /etc/init.d || echo "  No init.d scripts found"
    fi
}

# Function to stop the service
stop_service() {
    echo ">>> Stopping meshtasticd service..."
    if [ "$USE_SYSTEMD" = true ]; then
        echo "Stopping via systemctl..."
        systemctl stop meshtasticd || true
    elif [ -f /etc/init.d/meshtasticd ]; then
        echo "Stopping via init.d script..."
        /etc/init.d/meshtasticd stop || true
    elif [ "$IS_RASPBERRY_PI" = true ]; then
        echo "Stopping via service (Raspberry Pi)..."
        service meshtasticd stop || true
    fi

    if pgrep meshtasticd > /dev/null; then
        echo "Forcefully terminating meshtasticd processes..."
        pkill meshtasticd || true
        sleep 1
    fi

    if pgrep meshtasticd > /dev/null; then
        echo "Using SIGKILL on remaining processes..."
        pkill -9 meshtasticd || true
        sleep 1
    fi

    if ! pgrep meshtasticd > /dev/null; then
        echo "✓ meshtasticd successfully stopped"
    else
        echo "! Warning: Failed to stop all meshtasticd processes"
    fi
}

# Function to start the service
start_service() {
    echo ">>> Starting meshtasticd service..."
    if [ "$USE_SYSTEMD" = true ]; then
        echo "Starting via systemctl..."
        systemctl start meshtasticd || true
        if systemctl is-active --quiet meshtasticd; then
            echo "✓ Service started successfully via systemd"
            return 0
        fi
    elif [ "$IS_RASPBERRY_PI" = true ]; then
        echo "Starting via service (Raspberry Pi)..."
        service meshtasticd start || true
        sleep 2
        if pgrep meshtasticd > /dev/null; then
            echo "✓ Service started successfully via service command"
            return 0
        fi
    elif [ -f /etc/init.d/meshtasticd ]; then
        echo "Starting via init.d script..."
        /etc/init.d/meshtasticd start
        sleep 2
        if pgrep meshtasticd > /dev/null; then
            echo "✓ Service started successfully via init.d script"
            return 0
        fi
    fi

    echo "Starting manually..."
    nohup /usr/sbin/meshtasticd > /tmp/meshtasticd.log 2>&1 &
    sleep 2
    if pgrep meshtasticd > /dev/null; then
        echo "✓ Service started successfully (manual start)"
        echo "  Log file: /tmp/meshtasticd.log"
        return 0
    else
        echo "! Failed to start meshtasticd service"
        return 1
    fi
}

# Stop the service first
stop_service

# Get the binary path
BINARY_PATH="../release/meshtasticd_linux_$(uname -m)"

# Verify the binary exists
if [ ! -f "$BINARY_PATH" ]; then
    echo "Error: Binary file not found at $BINARY_PATH"
    echo "Current directory: $(pwd)"
    echo "Expected binary: $BINARY_PATH"
    exit 1
fi

# Create destination directory if it doesn't exist
mkdir -p /usr/sbin

# Copy the new binary
echo ">>> Deploying new binary to /usr/sbin/meshtasticd..."
cp "$BINARY_PATH" /usr/sbin/meshtasticd
chmod +x /usr/sbin/meshtasticd

echo "✓ Binary deployed successfully"

# Start the service
start_service

echo "Deployment completed."
