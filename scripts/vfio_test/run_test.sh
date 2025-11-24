#!/bin/bash
# Quick test script for VFIO

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

# Check if compiled
if [ ! -f "./vfio_test" ]; then
    echo "Compiling vfio_test..."
    make
fi

# Find first VFIO device or initialize
echo "Finding VFIO devices..."
BDF=$(./find_vfio_device.sh 2>/dev/null | grep "^  BDF:" | head -1 | awk '{print $2}')

if [ -z "$BDF" ]; then
    echo "No VFIO device found, attempting initialization..."
    echo ""
    
    # Try to find a device to initialize (default GPU device in L1 VM)
    DEFAULT_DEVICE="0000:01:00.0"
    if [ -e "/sys/bus/pci/devices/$DEFAULT_DEVICE" ]; then
        echo "Initializing device $DEFAULT_DEVICE..."
        if [ -f "./init_vfio.sh" ]; then
            sudo ./init_vfio.sh "$DEFAULT_DEVICE" || {
                echo "ERROR: Initialization failed"
                exit 1
            }
            BDF="$DEFAULT_DEVICE"
        else
            echo "ERROR: init_vfio.sh not found"
            exit 1
        fi
    else
        echo "ERROR: No VFIO device found and no device to initialize"
        echo ""
        echo "Please run initialization manually:"
        echo "  sudo ./init_vfio.sh <PCI_BDF>"
        exit 1
    fi
fi

echo "Found device: $BDF"
echo ""

# Run test
echo "Running VFIO test..."
sudo ./vfio_test "$BDF"

