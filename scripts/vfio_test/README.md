# VFIO 测试程序

这个程序用于在 L1 VM 中测试 VFIO 功能，验证 VFIO 框架是否正常工作。

## 编译

在 L1 VM 中编译：

```bash
cd scripts/vfio_test
make
```

## 使用方法

### 1. 确保设备已绑定到 vfio-pci

在 L0 中运行 `init_linux-l0_vfio.sh` 将设备绑定到 vfio-pci。

### 2. 在 L1 VM 中查找设备

```bash
# 查找 AMD GPU 设备
lspci | grep -i amd

# 获取完整的 BDF (Bus:Device.Function)
# 例如: 0000:00:01.0
```

### 3. 运行测试程序

```bash
sudo ./vfio_test 0000:00:01.0
```

将 `0000:00:01.0` 替换为实际的设备 BDF。

## 程序功能

程序会执行以下操作：

1. **打开 VFIO 容器** (`/dev/vfio/vfio`)
   - 检查 VFIO API 版本
   - 验证 Type1 IOMMU 支持

2. **打开 VFIO 组** (`/dev/vfio/<group>`)
   - 获取设备的 IOMMU group
   - 检查组是否可用

3. **设置 IOMMU**
   - 将组添加到容器
   - 设置 IOMMU 类型为 Type1
   - 获取 IOMMU 信息
   - 启用 IOMMU

4. **测试 DMA 映射**
   - 分配 1MB 内存
   - 进行 DMA 映射（IOVA 映射）
   - 输出虚拟地址（可用于验证 PTE）

5. **获取设备文件描述符**
   - 获取设备 FD
   - 读取设备信息
   - 读取区域信息

## 输出示例

```
=== VFIO Test Program ===
Testing device: 0000:00:01.0

✓ IOMMU group: 1

VFIO API version: 0
✓ VFIO Type1 IOMMU supported

✓ Group 1 is viable

✓ Group added to container
✓ IOMMU type set to Type1
IOMMU Info:
  IOVA range: 0x0 - 0xffffffffffffffff
✓ IOMMU enabled

=== Testing DMA Mapping ===
✓ Allocated 1048576 bytes at virtual address: 0x7f1234567000
✓ Filled memory with test pattern (0xAA)
✓ DMA mapped: IOVA=0x0, VADDR=0x7f1234567000, SIZE=1048576

=== Getting Device File Descriptor ===
✓ Got device file descriptor for 0000:00:01.0
Device Info:
  Flags: 0x3
  Regions: 5
  IRQs: 1
  Region 0: size=0x1000, offset=0x0, flags=0x3
  Region 1: size=0x100000, offset=0x1000, flags=0x3
  ...

=== Test Summary ===
✓ All VFIO operations completed successfully
  - Container: fd=3
  - Group: fd=4 (group 1)
  - Device: fd=5 (0000:00:01.0)
  - DMA: IOVA=0x0, VADDR=0x7f1234567000

You can now use QEMU monitor to verify PTE _PAGE_USER bit:
  python3 scripts/verify_pte/qemu_verify_pte.py 1334 0x7f1234567000
```

## 验证 PTE

程序成功运行后，会输出 DMA 映射的虚拟地址。可以使用 QEMU monitor 工具验证 PTE 的 `_PAGE_USER` 位：

```bash
# 在宿主机上运行
python3 scripts/verify_pte/qemu_verify_pte.py 1334 0x7f1234567000
```

## 故障排查

### 错误: Cannot read IOMMU group

**原因**: 设备未绑定到 vfio-pci 或设备不存在

**解决**: 
- 检查设备 BDF 是否正确
- 在 L0 中运行 `init_linux-l0_vfio.sh` 绑定设备

### 错误: Group is not viable

**原因**: IOMMU group 中的设备未全部绑定到 vfio-pci

**解决**: 确保 IOMMU group 中的所有设备都已绑定

### 错误: VFIO Type1 IOMMU not supported

**原因**: 内核未加载 vfio_iommu_type1 模块

**解决**: 
```bash
sudo modprobe vfio_iommu_type1
```

### 错误: Permission denied

**原因**: 未以 root 权限运行

**解决**: 使用 `sudo` 运行程序

## 参考文档

- [VFIO Kernel Documentation](https://docs.kernel.org/driver-api/vfio.html)
- VFIO 头文件通常在 `/usr/include/linux/vfio.h`（如果安装了 kernel headers）

## 注意事项

1. **需要 root 权限**: 访问 `/dev/vfio/*` 需要 root 权限
2. **设备绑定**: 设备必须先绑定到 vfio-pci
3. **IOMMU 支持**: 需要硬件和内核支持 IOMMU
4. **内存限制**: 默认的 `RLIMIT_MEMLOCK` 可能限制 DMA 映射大小

