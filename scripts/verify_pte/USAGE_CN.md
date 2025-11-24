# VFIO PTE _PAGE_USER 位清除验证指南

## 概述

本文档说明如何验证在内核代码中清除 VFIO DMA 映射页的 `_PAGE_USER` 位是否成功。

## 验证方法

### 方法 1: 查看内核日志（最简单）

修改后的代码会在清除 `_PAGE_USER` 位时输出日志。查看内核日志：

```bash
# 实时查看日志
sudo dmesg -w

# 或者查看最近的日志
sudo dmesg | grep "VFIO:"
```

日志输出示例：
```
[  123.456789] VFIO: Clearing _PAGE_USER at vaddr 0x7f1234567000, old PTE: 0x80000001234567
[  123.456790] VFIO: Successfully cleared _PAGE_USER at vaddr 0x7f1234567000, new PTE: 0x80000001234563
```

**成功标志**：
- 看到 "Successfully cleared _PAGE_USER" 消息
- 新 PTE 值中 `_PAGE_USER` 位（bit 2，值为 0x4）被清除

### 方法 2: 使用验证内核模块（推荐）

#### 步骤 1: 编译模块

在虚拟机内编译：

```bash
cd /path/to/open-tdx/scripts/verify_pte
make
```

如果编译失败，可能需要指定内核源码路径：

```bash
make KDIR=/path/to/linux-l1
```

#### 步骤 2: 加载模块

```bash
sudo insmod verify_pte.ko
```

#### 步骤 3: 获取 VFIO DMA 映射的虚拟地址

有几种方式可以获取地址：

**方式 A: 从内核日志中提取**
```bash
sudo dmesg | grep "VFIO: Clearing _PAGE_USER" | tail -1
# 从输出中提取地址，例如: 0x7f1234567000
```

**方式 B: 使用测试脚本自动提取**
```bash
./test_verify.sh --dmesg
```

**方式 C: 从用户空间程序记录**
在调用 VFIO IOCTL 进行 DMA 映射时，记录返回的虚拟地址。

#### 步骤 4: 验证 PTE 状态

**使用测试脚本（推荐）**：
```bash
sudo ./test_verify.sh 0x7f1234567000
```

**手动操作**：
```bash
# 设置要检查的地址
echo 0x7f1234567000 | sudo tee /proc/verify_pte

# 查看 PTE 状态
cat /proc/verify_pte
```

输出示例：
```
Virtual Address: 0x7f1234567000
PTE Value: 0x80000001234563
_PAGE_USER bit: CLEARED (kernel page) (0x0)
PTE Present: Yes
Physical Address: 0x1234567000
```

**成功标志**：
- `_PAGE_USER bit: CLEARED (kernel page) (0x0)` - 表示成功
- `_PAGE_USER bit: SET (user page) (0x4)` - 表示失败

#### 步骤 5: 卸载模块

```bash
sudo rmmod verify_pte
```

## 验证流程示例

完整的验证流程：

```bash
# 1. 编译并加载验证模块
cd scripts/verify_pte
make
sudo insmod verify_pte.ko

# 2. 触发 VFIO DMA 映射（通过你的 VFIO 应用程序）
# 这会触发内核代码执行，清除 _PAGE_USER 位

# 3. 从日志中获取地址
ADDR=$(sudo dmesg | grep "VFIO: Clearing _PAGE_USER" | tail -1 | grep -oP '0x[0-9a-f]+' | head -1)

# 4. 验证 PTE 状态
sudo ./test_verify.sh $ADDR

# 5. 清理
sudo rmmod verify_pte
```

## 如何理解 PTE 值

PTE (Page Table Entry) 是一个 64 位的值，在 x86_64 上：

- Bit 0: Present (P) - 页是否在内存中
- Bit 1: Write (W) - 是否可写
- Bit 2: User (U) - **用户/内核页标志**（这就是我们要清除的位）
- Bit 3: Page Write Through (PWT)
- Bit 4: Page Cache Disable (PCD)
- Bit 5: Accessed (A)
- Bit 6: Dirty (D)
- Bit 7: Page Size (PS)
- Bit 8: Global (G)
- Bits 12-51: 物理页帧号 (PFN)
- Bits 52-62: 保留
- Bit 63: Execute Disable (XD)

**关键点**：
- `_PAGE_USER` = 0x4 (bit 2)
- 如果 PTE 值包含 0x4，说明是用户页
- 如果 PTE 值不包含 0x4（在 bit 2 位置），说明是内核页

**示例**：
- `0x80000001234567` - 包含 `_PAGE_USER` (0x4)，是用户页
- `0x80000001234563` - 不包含 `_PAGE_USER`，是内核页（0x67 - 0x4 = 0x63）

## 故障排查

### 问题 1: 模块加载失败

**错误**: `insmod: ERROR: could not insert module verify_pte.ko: Invalid module format`

**解决**: 确保模块是针对当前运行的内核编译的。

### 问题 2: 无法读取 PTE

**错误**: `Error: Could not get PTE for address 0x...`

**可能原因**:
- 地址无效
- 地址不在当前进程的地址空间中
- 页表项不存在

**解决**: 确保使用正确的虚拟地址，并且该地址属于当前进程。

### 问题 3: _PAGE_USER 位仍然设置

**可能原因**:
- 代码修改未生效（需要重新编译内核）
- 地址不正确
- 页被其他代码重新设置了标志位

**解决**: 
- 检查内核是否重新编译并加载
- 确认使用的是正确的虚拟地址
- 检查是否有其他代码修改了这些页

## 注意事项

1. **需要 root 权限**: 加载内核模块和访问 `/proc/verify_pte` 需要 root 权限
2. **地址空间**: 虚拟地址必须是当前进程地址空间内的有效地址
3. **内核版本**: 验证模块需要与运行的内核版本匹配
4. **并发访问**: 如果多个进程同时访问，可能需要额外的同步机制

## 相关文件

- `linux-l1/drivers/vfio/vfio_iommu_type1.c` - 修改的内核代码
- `scripts/verify_pte/verify_pte.c` - 验证内核模块
- `scripts/verify_pte/test_verify.sh` - 测试脚本

