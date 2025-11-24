#!/usr/bin/env python3
"""
QEMU Monitor PTE Verification Tool

This script connects to QEMU monitor and checks PTE entries to verify
if _PAGE_USER bit is cleared for VFIO DMA mappings.

Usage:
    python3 qemu_verify_pte.py <monitor_port> <virtual_address>
    python3 qemu_verify_pte.py 1334 0x7f1234567000
"""

import sys
import socket
import struct
import re
import time

class QEMUMonitor:
    def __init__(self, host='127.0.0.1', port=1334):
        self.host = host
        self.port = port
        self.sock = None
        
    def connect(self):
        """Connect to QEMU monitor"""
        try:
            self.sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            self.sock.settimeout(5)
            self.sock.connect((self.host, self.port))
            # Read welcome message
            self.sock.recv(1024)
            print(f"[+] Connected to QEMU monitor at {self.host}:{self.port}")
            return True
        except Exception as e:
            print(f"[-] Failed to connect to QEMU monitor: {e}")
            return False
    
    def send_command(self, cmd):
        """Send command to QEMU monitor and return response"""
        if not self.sock:
            return None
        
        try:
            # Send command with newline
            self.sock.send((cmd + '\n').encode())
            time.sleep(0.1)  # Small delay for response
            
            # Read response
            response = b''
            self.sock.settimeout(1)
            while True:
                try:
                    data = self.sock.recv(4096)
                    if not data:
                        break
                    response += data
                except socket.timeout:
                    break
            
            return response.decode('utf-8', errors='ignore')
        except Exception as e:
            print(f"[-] Error sending command: {e}")
            return None
    
    def get_cr3(self, cpu_index=0):
        """Get CR3 register value (page table base address)"""
        # Try to get CR3 from info registers for specific CPU
        # First try current CPU
        response = self.send_command('info registers')
        if not response:
            return None
        
        # Look for CR3 in the response
        # Format: CR3=0000000000000000
        match = re.search(r'CR3=([0-9a-fA-F]+)', response)
        if match:
            cr3 = int(match.group(1), 16)
            print(f"[+] CR3 (Page Table Base): 0x{cr3:x}")
            return cr3
        
        # Try info registers for specific CPU
        for cpu in range(8):  # Try first 8 CPUs
            response = self.send_command(f'info registers -c {cpu}')
            if response:
                match = re.search(r'CR3=([0-9a-fA-F]+)', response)
                if match:
                    cr3 = int(match.group(1), 16)
                    print(f"[+] CR3 (Page Table Base) from CPU {cpu}: 0x{cr3:x}")
                    return cr3
        
        print("[-] Could not find CR3 in registers")
        print("[!] Note: CR3 may be from QEMU main thread, not the test process")
        print("[!] Try using GDB to attach to QEMU and get process CR3")
        return None
    
    def read_physical_memory(self, phys_addr, size=8):
        """Read physical memory from QEMU"""
        # Use xp command to read physical memory
        # Format: xp /<count><format> <addr>
        # xp /1xg 0x1000 - read 1 quadword (8 bytes) in hex starting at 0x1000
        count = max(1, size // 8)
        cmd = f'xp /{count}gx {phys_addr:#x}'
        response = self.send_command(cmd)
        if not response:
            return None
        
        # Parse response: 0000000000001000: 0x0000000000000000
        # Or: 0000000000001000: 0x0000000000000000 0x0000000000000000 ...
        lines = response.strip().split('\n')
        values = []
        for line in lines:
            if ':' in line:
                # Extract hex values after colon
                parts = line.split(':')[1].strip()
                hex_values = re.findall(r'0x([0-9a-fA-F]+)', parts)
                for hex_val in hex_values:
                    try:
                        values.append(int(hex_val, 16))
                    except ValueError:
                        continue
        
        return values[0] if values else None
    
    def get_pte_for_vaddr(self, vaddr, cr3=None):
        """Get PTE for a virtual address"""
        if cr3 is None:
            cr3 = self.get_cr3()
            if cr3 is None:
                return None
        
        # x86_64 page table structure:
        # Bits 47:39 -> PML4 index
        # Bits 38:30 -> PDPT index  
        # Bits 29:21 -> PD index
        # Bits 20:12 -> PT index
        # Bits 11:0  -> Page offset
        
        print(f"\n[+] Resolving PTE for virtual address 0x{vaddr:x}")
        
        # Extract indices
        pml4_idx = (vaddr >> 39) & 0x1ff
        pdpt_idx = (vaddr >> 30) & 0x1ff
        pd_idx = (vaddr >> 21) & 0x1ff
        pt_idx = (vaddr >> 12) & 0x1ff
        
        print(f"    PML4 index: {pml4_idx}")
        print(f"    PDPT index: {pdpt_idx}")
        print(f"    PD index: {pd_idx}")
        print(f"    PT index: {pt_idx}")
        
        # Read PML4 entry
        pml4_addr = cr3 + (pml4_idx * 8)
        pml4_entry = self.read_physical_memory(pml4_addr)
        if pml4_entry is None:
            print(f"[-] Could not read PML4 entry at 0x{pml4_addr:x}")
            return None
        if not (pml4_entry & 1):
            print(f"[-] PML4 entry not present at 0x{pml4_addr:x} (value: 0x{pml4_entry:x})")
            return None
        pml4_base = pml4_entry & 0xfffffffffffff000
        print(f"    PML4 entry at 0x{pml4_addr:x}: 0x{pml4_entry:x}, base: 0x{pml4_base:x}")
        
        # Read PDPT entry
        pdpt_addr = pml4_base + (pdpt_idx * 8)
        pdpt_entry = self.read_physical_memory(pdpt_addr)
        if pdpt_entry is None:
            print(f"[-] Could not read PDPT entry at 0x{pdpt_addr:x}")
            return None
        if not (pdpt_entry & 1):
            print(f"[-] PDPT entry not present at 0x{pdpt_addr:x} (value: 0x{pdpt_entry:x})")
            return None
        pdpt_base = pdpt_entry & 0xfffffffffffff000
        print(f"    PDPT entry at 0x{pdpt_addr:x}: 0x{pdpt_entry:x}, base: 0x{pdpt_base:x}")
        
        # Read PD entry
        pd_addr = pdpt_base + (pd_idx * 8)
        pd_entry = self.read_physical_memory(pd_addr)
        if pd_entry is None:
            print(f"[-] Could not read PD entry at 0x{pd_addr:x}")
            return None
        if not (pd_entry & 1):
            print(f"[-] PD entry not present at 0x{pd_addr:x} (value: 0x{pd_entry:x})")
            return None
        pd_base = pd_entry & 0xfffffffffffff000
        print(f"    PD entry at 0x{pd_addr:x}: 0x{pd_entry:x}, base: 0x{pd_base:x}")
        
        # Read PT entry (this is the PTE we want)
        pte_addr = pd_base + (pt_idx * 8)
        pte_value = self.read_physical_memory(pte_addr)
        if pte_value is None:
            print(f"[-] Could not read PTE at 0x{pte_addr:x}")
            return None
        
        print(f"    PTE at 0x{pte_addr:x}: 0x{pte_value:x}")
        
        return pte_value
    
    def check_page_user_bit(self, pte_value):
        """Check if _PAGE_USER bit is set in PTE"""
        # _PAGE_USER is bit 2 (value 0x4)
        PAGE_USER = 0x4
        PAGE_PRESENT = 0x1
        
        is_present = bool(pte_value & PAGE_PRESENT)
        has_user_bit = bool(pte_value & PAGE_USER)
        
        print(f"\n[+] PTE Analysis:")
        print(f"    Present: {is_present}")
        print(f"    _PAGE_USER bit: {'SET (user page)' if has_user_bit else 'CLEARED (kernel page)'}")
        print(f"    _PAGE_USER value: 0x{pte_value & PAGE_USER:x}")
        
        if is_present:
            if has_user_bit:
                print(f"\n[-] FAIL: _PAGE_USER bit is still set (0x{pte_value & PAGE_USER:x})")
                return False
            else:
                print(f"\n[+] SUCCESS: _PAGE_USER bit is cleared (kernel page)")
                return True
        else:
            print(f"\n[-] WARNING: Page is not present in memory")
            return None
    
    def close(self):
        """Close connection"""
        if self.sock:
            self.sock.close()
            self.sock = None

def main():
    if len(sys.argv) < 3:
        print(__doc__)
        print(f"\nUsage: {sys.argv[0]} <monitor_port> <virtual_address>")
        print(f"Example: {sys.argv[0]} 1334 0x7f1234567000")
        sys.exit(1)
    
    try:
        monitor_port = int(sys.argv[1])
        vaddr = int(sys.argv[2], 0)  # 0 allows hex (0x) or decimal
    except ValueError as e:
        print(f"[-] Invalid arguments: {e}")
        sys.exit(1)
    
    monitor = QEMUMonitor(port=monitor_port)
    
    if not monitor.connect():
        sys.exit(1)
    
    try:
        # Get PTE for the virtual address
        pte_value = monitor.get_pte_for_vaddr(vaddr)
        
        if pte_value is not None:
            # Check _PAGE_USER bit
            result = monitor.check_page_user_bit(pte_value)
            
            if result is True:
                print("\n[+] Verification PASSED: _PAGE_USER bit is cleared")
                sys.exit(0)
            elif result is False:
                print("\n[-] Verification FAILED: _PAGE_USER bit is still set")
                sys.exit(1)
            else:
                print("\n[?] Verification INCONCLUSIVE: Page not present")
                sys.exit(2)
        else:
            print("\n[-] Could not read PTE")
            sys.exit(1)
    finally:
        monitor.close()

if __name__ == '__main__':
    main()

