#!/bin/bash
# Helper script to find VFIO devices and their IOMMU groups

echo "=== Finding VFIO Devices ==="
echo ""

# Check if VFIO modules are loaded
if ! lsmod | grep -q "^vfio "; then
    echo "WARNING: VFIO module not loaded"
    echo ""
fi

# Find devices bound to vfio-pci
echo "Devices bound to vfio-pci:"
echo "---------------------------"

found=0
for dev in /sys/bus/pci/devices/*; do
    driver=$(readlink "$dev/driver" 2>/dev/null | sed 's/.*drivers\///' || echo "")
    if [ "$driver" = "vfio-pci" ]; then
        bdf=$(basename "$dev")
        iommu_group=$(readlink "$dev/iommu_group" 2>/dev/null | sed 's/.*\///' || echo "")
        device_name=$(lspci -s "$bdf" 2>/dev/null || echo "Unknown")
        
        echo "  BDF: $bdf"
        echo "    Device: $device_name"
        echo "    IOMMU Group: $iommu_group"
        if [ -c "/dev/vfio/$iommu_group" ]; then
            echo "    VFIO Device: /dev/vfio/$iommu_group ✓"
        else
            echo "    VFIO Device: /dev/vfio/$iommu_group ✗ (not found)"
        fi
        echo ""
        found=1
    fi
done

if [ $found -eq 0 ]; then
    echo "  No devices found bound to vfio-pci"
    echo ""
    echo "To bind devices, run in L0:"
    echo "  ./init_linux-l0_vfio.sh"
    echo ""
fi

# List available VFIO groups
echo "Available VFIO groups:"
echo "----------------------"
if [ -d "/dev/vfio" ]; then
    for group in /dev/vfio/*; do
        if [ -c "$group" ]; then
            group_num=$(basename "$group")
            echo "  /dev/vfio/$group_num"
        fi
    done
    if [ -z "$(ls -A /dev/vfio 2>/dev/null | grep -v '^vfio$')" ]; then
        echo "  (none)"
    fi
else
    echo "  /dev/vfio directory not found"
fi

echo ""
echo "Usage example:"
echo "  sudo ./vfio_test 0000:00:01.0"

