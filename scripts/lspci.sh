#!/bin/bash

PCI_PATH="/sys/bus/pci/devices"
PCI_IDS_PATH="/dev/shm/pci.ids"

# 如果不存在，就下载（像前面的脚本）
if [ ! -f "${PCI_IDS_PATH}" ]; then
    echo "pci.ids 不存在，尝试下载..."
    mkdir -p "$(dirname "$PCI_IDS_PATH")"
    if command -v curl >/dev/null 2>&1; then
        curl -fsSL "https://pci-ids.ucw.cz/pci.ids" -o "${PCI_IDS_PATH}"
    elif command -v wget >/dev/null 2>&1; then
        wget -qO "${PCI_IDS_PATH}" "https://pci-ids.ucw.cz/pci.ids"
    else
        echo "ERROR: 没有 curl 或 wget，无法下载 pci.ids"
        exit 1
    fi
fi

# 函数：通过 vendorID + deviceID 查名字
lookup_name() {
    local vendor_id="$1"
    local device_id="$2"
    local vendor_name
    local device_name

    # vendor 名：找以 vendor_id 开始的行
    vendor_name=$(grep -m1 -i "^${vendor_id} " "${PCI_IDS_PATH}" | cut -f2- -d" ")
    if [ -z "$vendor_name" ]; then
        vendor_name="Unknown vendor ${vendor_id}"
    fi

    # device 名：在 vendor 下面，带一个 tab, 然后 device_id
    device_name=$(grep -A20 -i "^${vendor_id} " "${PCI_IDS_PATH}" | grep -i "^[[:space:]]\\{1\\}${device_id} " | head -n1 | sed -e 's/^[[:space:]]*'"${device_id}"' //I')
    if [ -z "$device_name" ]; then
        device_name="Unknown device ${device_id}"
    fi

    echo "${vendor_name} : ${device_name}"
}

echo "Slot            VendorID  DeviceID   名称"
for dev in "${PCI_PATH}"/*; do
    [ -d "$dev" ] || continue
    slot=$(basename "$dev")
    vendor_hex=$(cat "$dev/vendor" | sed 's/0x//')
    device_hex=$(cat "$dev/device" | sed 's/0x//')

    # 把小写统一成小写或大写，以匹配 pci.ids 格式（通常是小写 hex 或者不区分大小写）
    vendor_id=$(echo "$vendor_hex" | tr '[:upper:]' '[:lower:]')
    device_id=$(echo "$device_hex" | tr '[:upper:]' '[:lower:]')

    name=$(lookup_name "$vendor_id" "$device_id")

    printf "%-15s %-8s %-8s %s\n" "$slot" "$vendor_id" "$device_id" "$name"
done