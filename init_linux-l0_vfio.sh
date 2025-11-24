#!/bin/bash -e

# Check if VFIO modules are loaded
check_vfio_modules() {
    local missing_modules=()
    if ! lsmod | grep -q "^vfio "; then
        missing_modules+=("vfio")
    fi
    if ! lsmod | grep -q "^vfio_iommu_type1 "; then
        missing_modules+=("vfio_iommu_type1")
    fi
    if ! lsmod | grep -q "^vfio_pci "; then
        missing_modules+=("vfio_pci")
    fi
    
    if [ ${#missing_modules[@]} -gt 0 ]; then
        echo "ERROR: Missing VFIO modules: ${missing_modules[*]}"
        return 1
    fi
    return 0
}

# Load VFIO modules if not already loaded
load_vfio_modules() {
    echo "Loading VFIO modules..."
    if ! lsmod | grep -q "^vfio "; then
        echo "  Loading vfio module..."
        sudo modprobe vfio || { echo "ERROR: Failed to load vfio module"; return 1; }
    else
        echo "  vfio module already loaded"
    fi
    if ! lsmod | grep -q "^vfio_iommu_type1 "; then
        echo "  Loading vfio_iommu_type1 module..."
        sudo modprobe vfio_iommu_type1 || { echo "ERROR: Failed to load vfio_iommu_type1 module"; return 1; }
    else
        echo "  vfio_iommu_type1 module already loaded"
    fi
    if ! lsmod | grep -q "^vfio_pci "; then
        echo "  Loading vfio-pci module..."
        sudo modprobe vfio-pci || { echo "ERROR: Failed to load vfio-pci module"; return 1; }
    else
        echo "  vfio-pci module already loaded"
    fi
    
    # Verify modules are loaded
    if ! check_vfio_modules; then
        echo "ERROR: Failed to verify VFIO modules after loading"
        return 1
    fi
    echo "VFIO modules loaded and verified"
    return 0
}

# Check if device is bound to vfio-pci
check_device_bound() {
    local full_bdf=$1
    local driver=$(readlink /sys/bus/pci/devices/$full_bdf/driver 2>/dev/null | sed 's/.*drivers\///' || echo "")
    if [ "$driver" = "vfio-pci" ]; then
        return 0
    else
        return 1
    fi
}

# Get IOMMU group for a device
get_iommu_group() {
    local full_bdf=$1
    local iommu_group_path=$(readlink /sys/bus/pci/devices/$full_bdf/iommu_group 2>/dev/null || echo "")
    if [ -z "$iommu_group_path" ]; then
        echo ""
        return
    fi
    # Extract just the group number (e.g., "12" from "../../../../kernel/iommu_groups/12")
    local iommu_group=$(basename "$iommu_group_path")
    echo "$iommu_group"
}

# Check if /dev/vfio/<group> exists
check_vfio_device() {
    local iommu_group=$1
    if [ -z "$iommu_group" ]; then
        return 1
    fi
    if [ -c "/dev/vfio/$iommu_group" ]; then
        return 0
    else
        return 1
    fi
}

vfio_bind() {
    local bdf=$1
    # Convert short BDF format (e.g., 94:00.0) to full format (0000:94:00.0)
    local full_bdf="0000:$bdf"
    echo "Binding $bdf to vfio-pci"
    
    # Check if already bound to vfio-pci
    if check_device_bound "$full_bdf"; then
        echo "  Device $bdf is already bound to vfio-pci"
        return 0
    fi
    
    # Unbind from current driver if bound
    if [ -e "/sys/bus/pci/devices/$full_bdf/driver/unbind" ]; then
        echo "  Unbinding from current driver..."
        echo "$full_bdf" | sudo tee /sys/bus/pci/devices/$full_bdf/driver/unbind > /dev/null
        sleep 0.5  # Give kernel time to process unbind
    else
        echo "  Device $bdf is not bound to any driver"
    fi
    
    # Set driver override
    echo "  Setting driver_override to vfio-pci..."
    echo "vfio-pci" | sudo tee /sys/bus/pci/devices/$full_bdf/driver_override > /dev/null
    
    # Trigger binding to vfio-pci driver
    if [ -e "/sys/bus/pci/drivers/vfio-pci/bind" ]; then
        echo "  Attempting to bind to vfio-pci..."
        echo "$full_bdf" | sudo tee /sys/bus/pci/drivers/vfio-pci/bind > /dev/null 2>&1
        sleep 0.5  # Give kernel time to process bind
        
        # Verify binding
        if check_device_bound "$full_bdf"; then
            echo "  ✓ Successfully bound $bdf to vfio-pci"
            return 0
        else
            echo "  ✗ Warning: Binding command succeeded but device not verified as bound"
            return 1
        fi
    else
        echo "  ✗ ERROR: vfio-pci driver bind interface not available"
        return 1
    fi
}

# Load VFIO modules first
if ! load_vfio_modules; then
    echo "ERROR: Failed to load VFIO modules. Exiting."
    exit 1
fi

# Find AMD devices
bdfs=($(lspci | grep -i amd | cut -d' ' -f1))
vdids=$(lspci -nn | grep -i amd | sed -n 's/.*\[\(....:....\)\].*/\1/p' | paste -sd,)

if [ ${#bdfs[@]} -eq 0 ]; then
    echo "WARNING: No AMD devices found"
    exit 0
fi

echo "Found ${#bdfs[@]} AMD device(s): ${bdfs[*]}"
echo ""

# Bind devices
failed_devices=()
for bdf in ${bdfs[@]}
do
    if ! vfio_bind $bdf; then
        failed_devices+=("$bdf")
    fi
    echo ""
done

# Final verification
echo "=== Final Verification ==="
all_success=true

# Check VFIO modules
echo "Checking VFIO modules..."
if ! check_vfio_modules; then
    echo "✗ VFIO modules check failed"
    all_success=false
else
    echo "✓ All VFIO modules loaded"
fi

# Check device binding and /dev/vfio
echo ""
echo "Checking device binding and /dev/vfio devices..."
iommu_groups_checked=()
for bdf in ${bdfs[@]}
do
    full_bdf="0000:$bdf"
    echo "  Device $bdf:"
    
    # Check if bound to vfio-pci
    if check_device_bound "$full_bdf"; then
        echo "    ✓ Bound to vfio-pci"
        
        # Get IOMMU group
        iommu_group=$(get_iommu_group "$full_bdf")
        if [ -n "$iommu_group" ]; then
            echo "    ✓ IOMMU group: $iommu_group"
            
            # Check /dev/vfio/<group> if not already checked
            if [[ ! " ${iommu_groups_checked[@]} " =~ " ${iommu_group} " ]]; then
                if check_vfio_device "$iommu_group"; then
                    echo "    ✓ /dev/vfio/$iommu_group exists"
                    ls -lh /dev/vfio/$iommu_group | awk '{print "      " $0}'
                else
                    echo "    ✗ /dev/vfio/$iommu_group does not exist"
                    all_success=false
                fi
                iommu_groups_checked+=("$iommu_group")
            fi
        else
            echo "    ✗ No IOMMU group found"
            all_success=false
        fi
    else
        echo "    ✗ Not bound to vfio-pci"
        all_success=false
    fi
done

# Summary
echo ""
echo "=== Summary ==="
if [ ${#failed_devices[@]} -gt 0 ]; then
    echo "✗ Failed to bind devices: ${failed_devices[*]}"
    all_success=false
fi

if [ "$all_success" = true ]; then
    echo "✓ All devices successfully bound to vfio-pci"
    echo "✓ All /dev/vfio device files created"
    echo ""
    echo "Available VFIO devices:"
    ls -lh /dev/vfio/ | grep -E "^[^d]" | awk '{print "  " $0}'
    exit 0
else
    echo "✗ Some checks failed. Please review the output above."
    exit 1
fi