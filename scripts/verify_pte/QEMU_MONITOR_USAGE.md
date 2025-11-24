# 使用 QEMU Monitor 验证 PTE _PAGE_USER 位

## 概述

通过 QEMU monitor 可以直接查看虚拟机的物理内存和页表，从而验证 PTE 的 `_PAGE_USER` 位是否被成功清除。

## 启动配置

已修改 `launch-opentdx.sh` 以启用 QEMU monitor。Monitor 端口默认为 `debug_port + 100`（例如，如果 debug_port 是 1234，则 monitor_port 是 1334）。

## 方法 1: 使用 Python 脚本（推荐）

### 步骤 1: 确保 QEMU 正在运行

启动虚拟机：
```bash
./launch-opentdx.sh
```

### 步骤 2: 获取虚拟地址

从内核日志中获取 VFIO DMA 映射的虚拟地址：
```bash
# 在虚拟机内或从外部查看日志
dmesg | grep "VFIO: Clearing _PAGE_USER" | tail -1
```

或者从你的 VFIO 应用程序中记录地址。

### 步骤 3: 运行验证脚本

```bash
cd scripts/verify_pte
python3 qemu_verify_pte.py <monitor_port> <virtual_address>
```

示例：
```bash
python3 qemu_verify_pte.py 1334 0x7f1234567000
```

脚本会自动：
1. 连接到 QEMU monitor
2. 获取 CR3（页表基址）
3. 根据虚拟地址遍历页表（PML4 -> PDPT -> PD -> PT）
4. 读取 PTE 值
5. 检查 `_PAGE_USER` 位（bit 2，值 0x4）

### 输出示例

成功的情况：
```
[+] Connected to QEMU monitor at 127.0.0.1:1334
[+] CR3 (Page Table Base): 0x12345000

[+] Resolving PTE for virtual address 0x7f1234567000
    PML4 index: 0
    PDPT index: 63
    PD index: 18
    PT index: 52
    PML4 entry: 0x12346001, base: 0x12346000
    PDPT entry: 0x12347001, base: 0x12347000
    PD entry: 0x12348001, base: 0x12348000
    PTE address: 0x12349000
    PTE value: 0x80000001234563

[+] PTE Analysis:
    Present: True
    _PAGE_USER bit: CLEARED (kernel page)
    _PAGE_USER value: 0x0

[+] SUCCESS: _PAGE_USER bit is cleared (kernel page)
[+] Verification PASSED: _PAGE_USER bit is cleared
```

失败的情况：
```
[+] PTE Analysis:
    Present: True
    _PAGE_USER bit: SET (user page)
    _PAGE_USER value: 0x4

[-] FAIL: _PAGE_USER bit is still set (0x4)
[-] Verification FAILED: _PAGE_USER bit is still set
```

## 方法 2: 手动使用 QEMU Monitor

### 步骤 1: 连接到 QEMU Monitor

```bash
telnet 127.0.0.1 1334
```

或者使用 `nc`：
```bash
nc 127.0.0.1 1334
```

### 步骤 2: 获取 CR3（页表基址）

在 monitor 中运行：
```
(qemu) info registers
```

查找 CR3 的值，例如：`CR3=0000000012345000`

### 步骤 3: 计算页表项地址

对于虚拟地址 `0x7f1234567000`，需要计算：

1. **PML4 索引**：`(0x7f1234567000 >> 39) & 0x1ff`
2. **PDPT 索引**：`(0x7f1234567000 >> 30) & 0x1ff`
3. **PD 索引**：`(0x7f1234567000 >> 21) & 0x1ff`
4. **PT 索引**：`(0x7f1234567000 >> 12) & 0x1ff`

然后遍历页表：
- 读取 PML4 项：`xp /1xg <CR3 + PML4_idx * 8>`
- 从 PML4 项获取 PDPT 基址（低12位清零）
- 读取 PDPT 项：`xp /1xg <PDPT_base + PDPT_idx * 8>`
- 从 PDPT 项获取 PD 基址
- 读取 PD 项：`xp /1xg <PD_base + PD_idx * 8>`
- 从 PD 项获取 PT 基址
- 读取 PTE：`xp /1xg <PT_base + PT_idx * 8>`

### 步骤 4: 检查 _PAGE_USER 位

读取到的 PTE 值是一个 64 位整数。检查 bit 2（值 0x4）：

- 如果 `PTE & 0x4 == 0x4`：`_PAGE_USER` 位已设置（用户页）❌
- 如果 `PTE & 0x4 == 0x0`：`_PAGE_USER` 位已清除（内核页）✅

### 示例手动操作

假设：
- CR3 = `0x12345000`
- 虚拟地址 = `0x7f1234567000`
- PML4 索引 = 0, PDPT 索引 = 63, PD 索引 = 18, PT 索引 = 52

```
(qemu) xp /1xg 0x12345000
0000000012345000: 0x0000000012346001
(qemu) xp /1xg 0x12346000
0000000012346000: 0x0000000012347001
(qemu) xp /1xg 0x12347000
0000000012347000: 0x0000000012348001
(qemu) xp /1xg 0x12348000
0000000012348000: 0x0000000012349001
(qemu) xp /1xg 0x12349000
0000000012349000: 0x80000001234563
```

检查 `0x80000001234563 & 0x4`：
- `0x80000001234563 & 0x4 = 0x0` ✅ 成功清除

## 有用的 QEMU Monitor 命令

- `info registers` - 显示所有 CPU 寄存器
- `xp /<count><format> <addr>` - 读取物理内存
  - 格式：`b` (byte), `h` (halfword), `w` (word), `g` (giant/quadword)
  - 示例：`xp /8xg 0x1000` - 读取 8 个 64 位值
- `x /<count><format> <addr>` - 读取虚拟内存（需要知道当前页表）
- `info mem` - 显示内存映射信息
- `help` - 显示所有可用命令

## 故障排查

### 问题 1: 无法连接到 Monitor

**错误**: `Connection refused` 或 `Connection timed out`

**解决**:
- 确认 QEMU 正在运行
- 检查 monitor 端口是否正确（默认是 debug_port + 100）
- 确认防火墙没有阻止连接

### 问题 2: 无法读取页表项

**错误**: 读取到的值为 0 或页表项不存在

**可能原因**:
- 虚拟地址无效
- 页表项不存在（需要先分配）
- CR3 值不正确（可能是不同进程的页表）

**解决**:
- 确认虚拟地址是正确的 VFIO DMA 映射地址
- 确保在正确的进程上下文中（可能需要切换到正确的进程）
- 检查页表项是否真的存在（Present 位）

### 问题 3: Python 脚本报错

**错误**: `ModuleNotFoundError` 或语法错误

**解决**:
- 确保使用 Python 3: `python3 --version`
- 检查脚本权限: `chmod +x qemu_verify_pte.py`

## 注意事项

1. **进程上下文**: 页表是每个进程私有的。确保查看的是正确的进程的页表（通常是进行 VFIO DMA 映射的进程）。

2. **CR3 切换**: 如果需要在不同进程间切换，可能需要：
   - 在虚拟机内切换到目标进程
   - 或者使用 GDB 连接到 QEMU 来切换进程上下文

3. **大页**: 如果使用了 2MB 或 1GB 大页，页表结构会有所不同（只有 3 级或 2 级）。

4. **权限**: 某些 QEMU 命令可能需要特定权限或配置。

## 相关文件

- `launch-opentdx.sh` - 已修改以启用 QEMU monitor
- `scripts/verify_pte/qemu_verify_pte.py` - Python 验证脚本
- `scripts/verify_pte/qemu_verify_pte.sh` - Shell 脚本（简化版）

