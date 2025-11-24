#!/bin/bash
# VFIO Initialization Script for L1 VM
# This script automates the process of binding PCI devices to vfio-pci driver
# Usage: sudo ./init_vfio.sh [PCI_BDF]
# Example: sudo ./init_vfio.sh 0000:01:00.0

set -e

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

# Default device (GPU in L1 VM)
DEFAULT_DEVICE="0000:01:00.0"
DEVICE="${1:-$DEFAULT_DEVICE}"

echo "=== VFIO Initialization Script ==="
echo "Target device: $DEVICE"
echo ""

# Check if running as root
if [ "$EUID" -ne 0 ]; then 
    echo -e "${RED}Error: This script must be run as root${NC}"
    echo "Usage: sudo $0 [PCI_BDF]"
    exit 1
fi

# Step 1: Load VFIO modules
echo -e "${YELLOW}[1/6] Loading VFIO modules...${NC}"
if ! lsmod | grep -q "^vfio "; then
    modprobe vfio 2>/dev/null || true
    echo "  ✓ vfio module loaded"
else
    echo "  ✓ vfio module already loaded"
fi

if ! lsmod | grep -q "^vfio_pci "; then
    modprobe vfio-pci 2>/dev/null || true
    echo "  ✓ vfio-pci module loaded"
else
    echo "  ✓ vfio-pci module already loaded"
fi

if ! lsmod | grep -q "^vfio_iommu_type1 "; then
    modprobe vfio-iommu-type1 2>/dev/null || true
    echo "  ✓ vfio-iommu-type1 module loaded"
else
    echo "  ✓ vfio-iommu-type1 module already loaded"
fi

echo ""

# Step 2: Verify device exists
echo -e "${YELLOW}[2/6] Verifying device exists...${NC}"
if [ ! -e "/sys/bus/pci/devices/$DEVICE" ]; then
    echo -e "${RED}Error: Device $DEVICE not found${NC}"
    echo "Available PCI devices:"
    lspci | head -10
    exit 1
fi

DEVICE_NAME=$(lspci -s "$DEVICE" 2>/dev/null || echo "Unknown")
echo "  ✓ Device found: $DEVICE_NAME"
echo ""

# Step 3: Check current driver
echo -e "${YELLOW}[3/6] Checking current driver binding...${NC}"
CURRENT_DRIVER=$(readlink "/sys/bus/pci/devices/$DEVICE/driver" 2>/dev/null | sed 's/.*drivers\///' || echo "none")

if [ "$CURRENT_DRIVER" = "vfio-pci" ]; then
    echo -e "  ${GREEN}✓ Device already bound to vfio-pci${NC}"
    echo ""
    # Skip to verification
    SKIP_BIND=1
elif [ "$CURRENT_DRIVER" = "none" ]; then
    echo "  Device is not bound to any driver"
else
    echo "  Current driver: $CURRENT_DRIVER"
fi
echo ""

# Step 4: Unbind from current driver (if needed)
if [ -z "$SKIP_BIND" ] && [ "$CURRENT_DRIVER" != "none" ]; then
    echo -e "${YELLOW}[4/6] Unbinding from current driver...${NC}"
    if [ -e "/sys/bus/pci/devices/$DEVICE/driver/unbind" ]; then
        echo "$DEVICE" > "/sys/bus/pci/devices/$DEVICE/driver/unbind" 2>/dev/null || {
            echo -e "${RED}Error: Failed to unbind device${NC}"
            exit 1
        }
        echo -e "  ${GREEN}✓ Unbound from $CURRENT_DRIVER${NC}"
    else
        echo "  Device is not bound to any driver (unbind not needed)"
    fi
    echo ""
fi

# Step 5: Bind to vfio-pci
if [ -z "$SKIP_BIND" ]; then
    echo -e "${YELLOW}[5/6] Binding device to vfio-pci...${NC}"
    
    # Get vendor and device IDs
    VENDOR_ID=$(cat "/sys/bus/pci/devices/$DEVICE/vendor" 2>/dev/null | sed 's/^0x//')
    DEVICE_ID=$(cat "/sys/bus/pci/devices/$DEVICE/device" 2>/dev/null | sed 's/^0x//')
    
    if [ -n "$VENDOR_ID" ] && [ -n "$DEVICE_ID" ]; then
        echo "  Vendor ID: $VENDOR_ID"
        echo "  Device ID: $DEVICE_ID"
    fi
    
    # Use driver_override method (more reliable)
    echo "  Setting driver_override..."
    echo "vfio-pci" > "/sys/bus/pci/devices/$DEVICE/driver_override" 2>/dev/null || {
        echo -e "${YELLOW}Warning: driver_override not available, trying direct bind...${NC}"
    }
    
    # Try to bind
    if [ -e "/sys/bus/pci/drivers/vfio-pci/bind" ]; then
        echo "$DEVICE" > "/sys/bus/pci/drivers/vfio-pci/bind" 2>/dev/null || {
            # Try with new_id if direct bind fails
            if [ -n "$VENDOR_ID" ] && [ -n "$DEVICE_ID" ]; then
                echo -e "${YELLOW}Warning: Direct bind failed, trying with new_id...${NC}"
                echo "$VENDOR_ID $DEVICE_ID" > /sys/bus/pci/drivers/vfio-pci/new_id 2>/dev/null || true
                sleep 1
                echo "$DEVICE" > "/sys/bus/pci/drivers/vfio-pci/bind" 2>/dev/null || {
                    echo -e "${RED}Error: Failed to bind device to vfio-pci${NC}"
                    echo "  This may be normal in nested virtualization if vIOMMU doesn't fully support binding"
                    echo "  You can still try to run vfio_test to see if DMA mapping works"
                    exit 1
                }
            else
                echo -e "${RED}Error: Failed to bind device to vfio-pci${NC}"
                exit 1
            fi
        }
    else
        echo -e "${RED}Error: vfio-pci driver bind interface not available${NC}"
        exit 1
    fi
    
    echo -e "  ${GREEN}✓ Bound to vfio-pci${NC}"
    echo ""
fi

# Step 6: Verify binding and IOMMU group
echo -e "${YELLOW}[6/6] Verifying binding...${NC}"
sleep 1

FINAL_DRIVER=$(readlink "/sys/bus/pci/devices/$DEVICE/driver" 2>/dev/null | sed 's/.*drivers\///' || echo "none")

if [ "$FINAL_DRIVER" != "vfio-pci" ]; then
    echo -e "${RED}Error: Device is not bound to vfio-pci (current: $FINAL_DRIVER)${NC}"
    exit 1
fi

echo -e "  ${GREEN}✓ Device is bound to vfio-pci${NC}"

# Check IOMMU group
IOMMU_GROUP=$(readlink "/sys/bus/pci/devices/$DEVICE/iommu_group" 2>/dev/null | sed 's/.*\///' || echo "")
if [ -n "$IOMMU_GROUP" ]; then
    echo "  IOMMU Group: $IOMMU_GROUP"
    
    # Check if all devices in the group are bound to vfio-pci
    echo "  Checking IOMMU group members:"
    GROUP_MEMBERS=$(ls "/sys/kernel/iommu_groups/$IOMMU_GROUP/devices/" 2>/dev/null || echo "")
    if [ -n "$GROUP_MEMBERS" ]; then
        ALL_BOUND=1
        for member in $GROUP_MEMBERS; do
            member_driver=$(readlink "/sys/bus/pci/devices/$member/driver" 2>/dev/null | sed 's/.*drivers\///' || echo "none")
            member_name=$(lspci -s "$member" 2>/dev/null | cut -d: -f3- || echo "Unknown")
            if [ "$member_driver" = "vfio-pci" ]; then
                echo -e "    ${GREEN}✓ $member: $member_name (vfio-pci)${NC}"
            else
                echo -e "    ${YELLOW}⚠ $member: $member_name ($member_driver)${NC}"
                ALL_BOUND=0
            fi
        done
        
        if [ $ALL_BOUND -eq 0 ]; then
            echo -e "  ${YELLOW}Warning: Not all devices in IOMMU group are bound to vfio-pci${NC}"
            echo "  You may need to bind other devices in the group manually"
        fi
    fi
    
    # Check VFIO device file
    if [ -c "/dev/vfio/$IOMMU_GROUP" ]; then
        echo -e "  ${GREEN}✓ VFIO device file: /dev/vfio/$IOMMU_GROUP${NC}"
    else
        echo -e "  ${YELLOW}⚠ VFIO device file not found: /dev/vfio/$IOMMU_GROUP${NC}"
    fi
else
    echo -e "  ${YELLOW}⚠ No IOMMU group found${NC}"
fi

echo ""
echo -e "${GREEN}=== Initialization Complete ===${NC}"
echo ""
echo "Device is ready for VFIO testing:"
echo "  sudo ./vfio_test $DEVICE"
echo ""

