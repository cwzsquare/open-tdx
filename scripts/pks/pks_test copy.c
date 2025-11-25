#include <linux/init.h>      // 包含 __init 和 __exit 宏
#include <linux/module.h>    // 包含所有模块都需要的核心头文件
#include <linux/kernel.h>    // 包含 KERN_INFO 等宏
#include <linux/smp.h>       // 包含 on_each_cpu 等多核处理函数
#include <linux/percpu.h>    // 包含 DEFINE_PER_CPU 等per-cpu变量宏
#include <asm/processor.h>   // 包含 X86_CR4_PKS 宏
#include <asm/msr.h>         // 包含 MSR 相关的宏

// 模块许可证声明，避免内核污染警告
MODULE_LICENSE("GPL");
MODULE_AUTHOR("anonymous");
MODULE_DESCRIPTION("A simple kernel module to test PKS");
MODULE_VERSION("0.1");

#define PKRS_MSR 0x6e0

// 定义一个 per-cpu 变量，用于保存每个CPU上原始的PKRS MSR值
// 这样我们可以在模块卸载时恢复它
static DEFINE_PER_CPU(u32, original_pkrs_val);

// 32 bit of PKRS_MSR
// PKRS 包含16对权限位 (AD, WD)，分别对应保护密钥 0-15。
// Bit 2*k    : Access-Disable (AD) for key k
// Bit 2*k+1  : Write-Disable (WD) for key k
// key 0 的权限总是 00 (允许访问/写入)，不可修改。
// 我们将在本测试中修改 key 1 的权限。
typedef union {
    uint32_t raw;
    struct {
        uint32_t pkey0_ad : 1;
        uint32_t pkey0_wd : 1;
        uint32_t pkey1_ad : 1;
        uint32_t pkey1_wd : 1;
        // ... and so on for 14 more keys
        uint32_t rest : 28;
    } perms;
} pkrs_layout;


static inline unsigned long read_cr4_reg(void)
{
    unsigned long cr4_val;
    asm volatile("mov %%cr4, %0" : "=r"(cr4_val));
    return cr4_val;
}

static inline u32 read_pkrs_msr(void)
{
    u32 low, high;
    // 使用内核提供的 rdmsr 宏更安全
    rdmsr(PKRS_MSR, low, high);
    return low;
}

static inline void write_pkrs_msr(u32 val)
{
    // 使用内核提供的 wrmsr 宏更安全
    wrmsr(PKRS_MSR, val, 0);
}

/*
 * 回调函数：读取并保存原始的 PKRS MSR 值。
 * 这个函数将由 on_each_cpu() 在每个CPU上执行。
 */
static void read_and_save_pkrs(void *info)
{
    int cpu = smp_processor_id();
    u32 pkrs_val = read_pkrs_msr();

    // 保存到 per-cpu 变量中
    per_cpu(original_pkrs_val, cpu) = pkrs_val;

    printk(KERN_INFO "[CPU %d] Initial PKRS MSR value = 0x%08x (saved)\n", cpu, pkrs_val);
}

/*
 * 回调函数：向 PKRS MSR 写入一个新值。
 */
static void write_new_pkrs(void *info)
{
    int cpu = smp_processor_id();
    u32 new_val;

    // 我们来禁用 key 1 的权限 (AD=1, WD=1)。
    // key 1 的权限位在 bit 2 和 bit 3。
    // 设置 AD=1, WD=1 意味着设置值为 '11'b，即十进制的 3。
    const int key_to_modify = 1;
    const u32 permissions_disable = 3; // AD=1, WD=1
    u32 mask = permissions_disable << (2 * key_to_modify);

    // 读取当前值并应用掩码
    new_val = per_cpu(original_pkrs_val, cpu) | mask;

    write_pkrs_msr(new_val);
    printk(KERN_INFO "[CPU %d] Wrote new PKRS MSR value: 0x%08x\n", cpu, new_val);
}

/*
 * 回调函数：读取并打印当前的 PKRS MSR 值，用于验证。
 */
static void verify_pkrs(void *info)
{
    int cpu = smp_processor_id();
    u32 pkrs_val = read_pkrs_msr();
    printk(KERN_INFO "[CPU %d] Verified PKRS MSR value = 0x%08x\n", cpu, pkrs_val);
}

/*
 * 回调函数：恢复原始的 PKRS MSR 值。
 */
static void restore_pkrs(void *info)
{
    int cpu = smp_processor_id();
    u32 original_val = per_cpu(original_pkrs_val, cpu);
    write_pkrs_msr(original_val);
    printk(KERN_INFO "[CPU %d] Restored original PKRS MSR value: 0x%08x\n", cpu, original_val);
}


static int __init pks_test_init(void)
{
    printk(KERN_INFO "PKS test module loaded\n");

    u32 cr4_val = read_cr4_reg();

    // 步骤 1: 检查 PKS 是否被支持和启用
    if (!(cr4_val & X86_CR4_PKS)) {
        printk(KERN_ERR "PKS is not supported or not enabled in CR4: 0x%08x\n", cr4_val);
        return -EOPNOTSUPP; // Operation not supported
    }
    printk(KERN_INFO "PKS is enabled in CR4\n");


    // 步骤 2: 在每个CPU上读取并保存原始的 PKRS MSR 值
    printk(KERN_INFO "--- Reading and saving original PKRS MSR values ---\n");
    on_each_cpu(read_and_save_pkrs, NULL, 1); // 第三个参数为1，表示等待所有CPU完成

    // 步骤 3: 在每个CPU上写入新的 PKRS MSR 值
    printk(KERN_INFO "--- Writing new PKRS MSR values (disabling key 1) ---\n");
    on_each_cpu(write_new_pkrs, NULL, 1);

    // 步骤 4: 在每个CPU上验证写入是否成功
    printk(KERN_INFO "--- Verifying new PKRS MSR values ---\n");
    on_each_cpu(verify_pkrs, NULL, 1);

    printk(KERN_INFO "PKS test sequence finished.\n");
    return 0;
}

static void __exit pks_test_exit(void)
{
    // 步骤 5: 模块卸载时，在每个CPU上恢复原始的 PKRS MSR 值
    printk(KERN_INFO "--- Restoring original PKRS MSR values ---\n");
    on_each_cpu(restore_pkrs, NULL, 1);

    printk(KERN_INFO "PKS test module unloaded\n");
}

module_init(pks_test_init);
module_exit(pks_test_exit);