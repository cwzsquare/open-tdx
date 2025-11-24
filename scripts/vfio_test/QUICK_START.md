# VFIO 测试快速开始

## 在 L1 VM 中测试 VFIO

### 1. 准备环境

在 L0 中绑定设备到 vfio-pci：
```bash
./init_linux-l0_vfio.sh
```

### 2. 在 L1 VM 中编译

```bash
cd scripts/vfio_test
make
```

### 3. 查找设备

```bash
./find_vfio_device.sh
```

或者手动查找：
```bash
lspci | grep -i amd
```

### 4. 运行测试

**方法 1: 使用自动脚本**
```bash
sudo ./run_test.sh
```

**方法 2: 手动运行**
```bash
sudo ./vfio_test 0000:00:01.0
```
（将 `0000:00:01.0` 替换为实际的设备 BDF）

### 5. 验证 PTE

程序成功运行后会输出虚拟地址，例如：
```
DMA: IOVA=0x0, VADDR=0x7f1234567000
```

在宿主机上使用 QEMU monitor 验证：
```bash
python3 scripts/verify_pte/qemu_verify_pte.py 1334 0x7f1234567000
```

## 完整流程示例

```bash
# 1. L0: 绑定设备
./init_linux-l0_vfio.sh

# 2. L1 VM: 进入 scripts 目录（通过 virtfs 挂载）
cd /path/to/mounted/scripts/vfio_test

# 3. L1 VM: 编译
make

# 4. L1 VM: 查找设备
./find_vfio_device.sh

# 5. L1 VM: 运行测试（记录输出的虚拟地址）
sudo ./vfio_test 0000:00:01.0

# 6. 宿主机: 验证 PTE
python3 scripts/verify_pte/qemu_verify_pte.py 1334 <vaddr_from_step5>
```

## 故障排查

- **找不到设备**: 确保在 L0 中已运行 `init_linux-l0_vfio.sh`
- **Permission denied**: 使用 `sudo` 运行
- **编译错误**: 确保安装了 `gcc` 和 `make`
- **IOMMU group 不可用**: 确保 IOMMU group 中的所有设备都已绑定

