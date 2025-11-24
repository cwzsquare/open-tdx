# VFIO 初始化脚本使用说明

## 概述

`init_vfio.sh` 脚本用于在 L1 VM 中自动初始化 VFIO 环境，包括：
- 加载 VFIO 内核模块
- 绑定 PCI 设备到 vfio-pci 驱动
- 验证 IOMMU 组和 VFIO 设备文件

## 使用方法

### 基本用法

```bash
sudo ./init_vfio.sh [PCI_BDF]
```

如果不指定 PCI_BDF，默认使用 `0000:01:00.0`（L1 VM 中的 GPU 设备）。

### 示例

```bash
# 使用默认设备 (0000:01:00.0)
sudo ./init_vfio.sh

# 指定设备
sudo ./init_vfio.sh 0000:01:00.0
```

## 脚本功能

1. **加载 VFIO 模块**
   - `vfio`
   - `vfio-pci`
   - `vfio-iommu-type1`

2. **验证设备存在**
   - 检查设备是否在系统中

3. **检查当前驱动绑定**
   - 如果已绑定到 vfio-pci，跳过绑定步骤

4. **解绑当前驱动**（如需要）
   - 从当前驱动（如 `snd_hda_intel`）解绑设备

5. **绑定到 vfio-pci**
   - 将设备绑定到 vfio-pci 驱动

6. **验证绑定**
   - 检查设备是否成功绑定
   - 检查 IOMMU 组
   - 检查 IOMMU 组中的所有设备是否都已绑定
   - 验证 `/dev/vfio/<group>` 设备文件

## 与 run_test.sh 集成

`run_test.sh` 已更新，如果检测到设备未绑定到 vfio-pci，会自动调用 `init_vfio.sh` 进行初始化。

```bash
# 直接运行测试，会自动初始化（如需要）
sudo ./run_test.sh
```

## 注意事项

1. **需要 root 权限**：脚本必须使用 `sudo` 运行

2. **IOMMU 组**：如果 IOMMU 组中有多个设备（如 GPU 和 Audio），所有设备都需要绑定到 vfio-pci 才能使用 VFIO

3. **模块依赖**：确保内核已启用 IOMMU（`intel_iommu=on iommu=on`）

4. **设备状态**：如果设备正在被其他进程使用，解绑可能会失败

## 故障排除

### 设备无法绑定

- 检查设备是否正在被使用：`lspci -v -s <BDF>`
- 检查是否有其他进程占用设备
- 检查内核日志：`dmesg | tail -50`

### IOMMU 组不完整

- 查看 IOMMU 组中的所有设备：`ls -l /sys/kernel/iommu_groups/<group>/devices/`
- 手动绑定组中的其他设备

### VFIO 设备文件不存在

- 检查 VFIO 模块是否加载：`lsmod | grep vfio`
- 检查 IOMMU 是否启用：`dmesg | grep -i iommu`

