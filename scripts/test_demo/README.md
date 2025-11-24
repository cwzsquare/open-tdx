# LKL VMX Test Program

This directory contains a test program for the LKL VMX functionality.

## Files

- `test.c` - Main test program that exercises LKL VMX ioctls
- `Makefile` - Build system for both static and dynamic compilation
- `README.md` - This documentation

## Building

### Static Compilation (Recommended)
```bash
make static
# or
make all
```

### Dynamic Compilation
```bash
make dynamic
```

### Install Static Binary
```bash
make install
# This copies the static binary to ../lkl_vmx_test
```

## Usage

### Prerequisites
1. The LKL VMX kernel module must be loaded
2. The program must be run as root (for /dev/kvm access)
3. VMX support must be available on the CPU

### Running the Test
```bash
# Build static version
make static

# Run as root
sudo ./lkl_vmx_test_static
```

### Expected Output
The test program will:
1. Open /dev/kvm device
2. Setup initial guest state
3. Call KVM_LKL_VMLAUNCH
4. Handle any vmexit
5. Call KVM_LKL_VMRESUME
6. Report results

## Test Features

The test program exercises:
- **KVM_LKL_VMLAUNCH** - Initial VM entry
- **KVM_LKL_VMEXIT** - Get vmexit information
- **KVM_LKL_VMRESUME** - Resume VM execution

## Troubleshooting

### Common Issues

1. **Permission Denied**
   - Solution: Run as root with `sudo`

2. **Device Not Found**
   - Solution: Ensure KVM module is loaded: `modprobe kvm-intel`

3. **VMX Not Supported**
   - Solution: Check CPU supports VMX: `grep vmx /proc/cpuinfo`

4. **EPT Not Supported**
   - Solution: Check EPT support: `grep ept /proc/cpuinfo`

### Debug Information

The test program provides detailed output including:
- Guest register state
- Exit reasons
- VMCS configuration status

## Implementation Notes

The test program:
- Sets up a basic 64-bit long mode guest state
- Uses identity mapping (GPA == HPA)
- Handles common vmexit scenarios
- Provides comprehensive error reporting

## Future Enhancements

Potential improvements:
- Add more complex guest code execution
- Test VMCALL hypercall functionality
- Performance benchmarking
- Multi-process testing
- Error injection testing
