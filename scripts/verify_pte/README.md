# VFIO PTE 验证工具

本目录提供了两种方法来验证 VFIO DMA 映射中 PTE 的 `_PAGE_USER` 位是否被成功清除：

1. **内核模块方法** - 在虚拟机内加载内核模块进行验证
2. **QEMU Monitor 方法** - 从宿主机通过 QEMU monitor 查看页表（推荐，无需修改虚拟机内核）

## 方法选择

- **推荐使用 QEMU Monitor 方法**：无需在虚拟机内安装内核模块，可以从外部直接查看
- **内核模块方法**：适合需要在虚拟机内进行自动化测试的场景

详细使用方法请参考：
- QEMU Monitor 方法：`QEMU_MONITOR_USAGE.md`
- 内核模块方法：`USAGE_CN.md`

---

## 方法 1: QEMU Monitor 验证（推荐）

### 快速开始

1. 启动虚拟机（已自动启用 monitor）：
   ```bash
   ./launch-opentdx.sh
   ```

2. 获取 VFIO DMA 映射的虚拟地址（从内核日志或应用程序）

3. 运行验证脚本：
   ```bash
   cd scripts/verify_pte
   python3 qemu_verify_pte.py 1334 0x7f1234567000
   ```

详细说明请参考 `QEMU_MONITOR_USAGE.md`。

---

## 方法 2: 内核模块验证

这个内核模块用于验证 VFIO DMA 映射中 PTE 的 `_PAGE_USER` 位是否被成功清除。

## 编译

在虚拟机内核源码目录中编译：

```bash
cd scripts/verify_pte
make KDIR=/path/to/linux-l1
```

或者如果已经在虚拟机内，可以直接使用：

```bash
make
```

**注意**: 如果编译时出现 `proc_ops` 相关的错误，说明内核版本较旧（< 5.6），需要将代码中的 `proc_ops` 改为 `proc_fops`，并将 `.proc_read` 和 `.proc_write` 改为 `.read` 和 `.write`。

## 使用方法

1. **加载模块**：
   ```bash
   sudo insmod verify_pte.ko
   ```

2. **设置要检查的虚拟地址**：
   ```bash
   # 将 VFIO DMA 映射的虚拟地址写入（十六进制格式）
   echo 0x7f1234567000 > /proc/verify_pte
   ```

3. **查看 PTE 状态**：
   ```bash
   cat /proc/verify_pte
   ```

   输出示例：
   ```
   Virtual Address: 0x7f1234567000
   PTE Value: 0x80000001234567
   _PAGE_USER bit: CLEARED (kernel page) (0x0)
   PTE Present: Yes
   Physical Address: 0x1234567000
   ```

4. **卸载模块**：
   ```bash
   sudo rmmod verify_pte
   ```

## 如何获取 VFIO DMA 映射的虚拟地址

可以通过以下方式获取：

1. **查看内核日志**：修改后的代码会在清除 `_PAGE_USER` 位时输出日志，包含虚拟地址信息。

2. **使用 VFIO 工具**：如果 VFIO 设备已经映射，可以通过 `/proc/<pid>/maps` 查看进程的内存映射。

3. **在用户空间程序中记录地址**：在调用 VFIO IOCTL 进行 DMA 映射时，记录返回的虚拟地址。

## 验证成功的标志

- `_PAGE_USER bit: CLEARED (kernel page) (0x0)` - 表示 `_PAGE_USER` 位已被清除
- `_PAGE_USER bit: SET (user page) (0x4)` - 表示 `_PAGE_USER` 位仍然设置（修改失败）

## 注意事项

- 需要 root 权限
- 虚拟地址必须是当前进程地址空间内的有效地址
- 如果地址无效或无法访问，会显示错误信息

