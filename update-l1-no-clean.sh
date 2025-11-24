#!/bin/bash -e

echo "============================================================"
echo "= Updating linux-l1 with incremental build (no clean)     ="
echo "============================================================"

# 检查是否在正确的目录
if [ ! -f "common.sh" ]; then
    echo "Error: common.sh not found. Please run this script from the project root directory."
    exit 1
fi

# 检查 linux-l1 目录是否存在
if [ ! -d "linux-l1" ]; then
    echo "Error: linux-l1 directory not found. Please ensure the project is properly set up."
    exit 1
fi

echo "[+] Cleaning up tmp directory..."
# 安全地清理 tmp 目录，如果已挂载则先卸载
if [ -d "tmp" ]; then
    if mountpoint -q tmp 2>/dev/null; then
        echo "  - Unmounting existing tmp directory..."
        sudo umount tmp 2>/dev/null || true
    fi
    rm -rf tmp
fi

echo "[+] Rebuilding linux-l1 kernel (incremental build)..."
./common.sh -t linux_no_clean -l l1

echo "[+] Installing kernel to l1 image..."
./common.sh -t kernel -l l1

echo "[+] Building initrd for l1..."
./common.sh -t initrd -l l1

echo "[+] Extracting and building KVM modules for l1..."
./common.sh -t kvm -l l1

echo "[+] Building KVM modules..."
pushd kvm-l1 >/dev/null
./build.sh
popd >/dev/null

echo "============================================================"
echo "= linux-l1 update completed successfully!                 ="
echo "============================================================"

