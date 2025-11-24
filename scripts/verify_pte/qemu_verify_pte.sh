#!/bin/bash
# QEMU Monitor PTE Verification Script (Simple version)
# 
# This script uses telnet/nc to connect to QEMU monitor and check PTE
# Note: This is a simplified version. For full functionality, use qemu_verify_pte.py

MONITOR_PORT=${1:-1334}
VADDR=${2}

if [ -z "$VADDR" ]; then
    echo "Usage: $0 <monitor_port> <virtual_address>"
    echo "Example: $0 1334 0x7f1234567000"
    exit 1
fi

echo "Connecting to QEMU monitor on port $MONITOR_PORT..."
echo "Note: This script requires manual interaction with QEMU monitor"
echo ""
echo "Useful QEMU monitor commands:"
echo "  info registers          - Show CPU registers (including CR3)"
echo "  xp /8xg <phys_addr>      - Read 8 quadwords from physical address"
echo ""
echo "To manually check PTE:"
echo "1. Connect: telnet 127.0.0.1 $MONITOR_PORT"
echo "2. Run: info registers"
echo "3. Find CR3 value"
echo "4. Calculate PTE address based on virtual address $VADDR"
echo "5. Read PTE: xp /1xg <pte_phys_addr>"
echo "6. Check bit 2 (0x4) - if set, it's a user page; if cleared, it's a kernel page"
echo ""

# Try to connect and get CR3
{
    sleep 1
    echo "info registers"
    sleep 1
} | telnet 127.0.0.1 $MONITOR_PORT 2>/dev/null | grep -i "CR3" || {
    echo "Could not connect to QEMU monitor or get CR3"
    echo "Make sure QEMU is running and monitor is enabled"
    exit 1
}

