# 快速开始指南

## 使用 QEMU Monitor 验证 PTE（推荐）

### 1. 启动虚拟机

```bash
./launch-opentdx.sh
```

Monitor 会自动启用，端口为 `debug_port + 100`（默认 1334）。

### 2. 获取虚拟地址

从内核日志获取：
```bash
# 在虚拟机内
dmesg | grep "VFIO" | tail -5
```

或从你的 VFIO 应用程序中记录地址。

### 3. 运行验证

```bash
cd scripts/verify_pte
python3 qemu_verify_pte.py 1334 0x7f1234567000
```

### 4. 查看结果

- ✅ **成功**: `_PAGE_USER bit: CLEARED (kernel page)`
- ❌ **失败**: `_PAGE_USER bit: SET (user page)`

## 手动验证（使用 telnet）

```bash
# 1. 连接到 monitor
telnet 127.0.0.1 1334

# 2. 获取 CR3
(qemu) info registers

# 3. 读取页表项（需要根据虚拟地址计算）
(qemu) xp /1xg <pte_physical_address>

# 4. 检查 bit 2 (0x4)
# 如果 PTE & 0x4 == 0: 成功清除
# 如果 PTE & 0x4 == 4: 仍然设置
```

## 使用内核模块验证

```bash
# 1. 在虚拟机内编译
cd scripts/verify_pte
make

# 2. 加载模块
sudo insmod verify_pte.ko

# 3. 设置地址并查看
echo 0x7f1234567000 | sudo tee /proc/verify_pte
cat /proc/verify_pte

# 4. 卸载
sudo rmmod verify_pte
```

## 故障排查

- **无法连接 monitor**: 检查 QEMU 是否运行，端口是否正确
- **无法读取页表**: 确认虚拟地址正确，页表项存在
- **Python 脚本错误**: 确保使用 Python 3，检查网络连接

详细说明请参考：
- `QEMU_MONITOR_USAGE.md` - QEMU monitor 详细使用
- `USAGE_CN.md` - 内核模块详细使用

