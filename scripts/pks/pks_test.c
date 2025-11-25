#include <linux/init.h>      // 包含 __init 和 __exit 宏
#include <linux/module.h>    // 包含所有模块都需要的核心头文件
#include <linux/kernel.h>    // 包含 KERN_INFO 等宏
#include <linux/smp.h>       // 包含 on_each_cpu 等多核处理函数
#include <linux/mm.h>        // 包含页管理相关的函数和结构，如 pte_t
#include <linux/mm_types.h>
#include <linux/gfp.h>       // 包含 GFP_KERNEL 等内存分配标志
#include <asm/pgtable.h>     // 包含PTE操作相关的宏和函数
#include <asm/tlbflush.h>    // 包含刷新TLB的函数
#include <asm/processor.h>   // 包含 X86_CR4_PKS 宏
#include <asm/msr.h>         // 包含 MSR 相关的宏
#include <asm/uaccess.h>     // 用于异常表
#include <linux/kprobes.h>   // 高版本linux内核模块编程找kallsyms_lookup_name用
#include <linux/smp.h>       // 包含 on_each_cpu 等多核处理函数


// 模块许可证声明
MODULE_LICENSE("GPL");
MODULE_AUTHOR("anonymous");
MODULE_DESCRIPTION("A kernel module to test PKS by modifying a PTE");
MODULE_VERSION("0.2");

#define PKRS_MSR 0x6e1
#define TEST_PKEY 2 // 我们选择一个非0的key进行测试，例如 key 2

#ifndef _PAGE_PKEY_SHIFT
#define _PAGE_PKEY_SHIFT 59
#endif

#ifndef _PAGE_PKEY_BITS
#define _PAGE_PKEY_BITS 4
#endif

#ifndef _PAGE_PKEY_MASK
// 创建一个覆盖 PKEY 位的掩码。例如 ( (1<<4) - 1 ) << 59
#define _PAGE_PKEY_MASK (((1ULL << _PAGE_PKEY_BITS) - 1) << _PAGE_PKEY_SHIFT)
#endif

#ifndef _PAGE_PKEY
// 将一个整数 pkey 值转换为可以在 PTE 中使用的值（将其移动到正确的位置）
#define _PAGE_PKEY(pkey) ((pteval_t)(pkey) << _PAGE_PKEY_SHIFT)
#endif

static pgd_t *myfound_init_top_pgt;
void (*myfound_native_write_cr4)(unsigned long);

static struct kprobe kp = {
    .symbol_name = "kallsyms_lookup_name"
};

typedef unsigned long (*kallsyms_lookup_name_t)(const char *name);
kallsyms_lookup_name_t my_kallsyms_lookup_name;

static inline unsigned long read_cr4_reg(void)
{
    return __read_cr4();
}

static inline void write_cr4_reg(unsigned long val)
{
    myfound_native_write_cr4(val);
}

static inline u32 read_pkrs_msr(void)
{
    u32 low, high;
    rdmsr(PKRS_MSR, low, high);
    return low;
}

static void write_pkrs_on_single_cpu(void *info)
{
    // 将 void* 类型的 info 指针转换回我们需要的 u32 类型的值
    u32 value_to_write = *(u32 *)info;

    // 在当前CPU上执行wrmsr指令
    wrmsr(PKRS_MSR, value_to_write, 0);
}

static inline void write_pkrs_msr(u32 val)
{
    on_each_cpu(write_pkrs_on_single_cpu, &val, 1); // 在所有CPU上执行wrmsr
}

// 辅助函数：根据虚拟地址查找PTE
// 注意：这在内核中直接操作页表，需要非常小心
static pte_t *lookup_pte_by_kernel_addr(unsigned long addr)
{
    pgd_t *pgd;
    p4d_t *p4d;
    pud_t *pud;
    pmd_t *pmd;

    // 使用导出的 init_top_pgt 作为页表遍历的起点
    pgd = myfound_init_top_pgt + pgd_index(addr);
    if (pgd_none(*pgd) || pgd_bad(*pgd)) return NULL;

    p4d = p4d_offset(pgd, addr);
    if (p4d_none(*p4d) || p4d_bad(*p4d)) return NULL;

    pud = pud_offset(p4d, addr);
    if (pud_none(*pud) || pud_bad(*pud)) return NULL;

    pmd = pmd_offset(pud, addr);
    if (pmd_none(*pmd) || pmd_bad(*pmd)) return NULL;
    // if (pmd_large(*pmd)) return NULL; // 如果页表是大型页表，则返回NULL

    // 使用导出的 pte_offset_kernel
    return pte_offset_kernel(pmd, addr);
}

/**
 * pte_mkhpkey - 在PTE中设置保护密钥 (Protection Key)
 * @pte: 原始的 pte_t
 * @pkey: 要设置的保护密钥 (0-15)
 *
 * PKEYs 存储在PTE的59-62位。
 * 这个函数手动清除旧的PKEY位并设置新的。
 * _PAGE_PKEY_MASK 覆盖了所有PKEY位。
 * _PAGE_PKEY(pkey) 会将pkey值左移到正确的位置。
 */

static inline pte_t pte_mkhpkey(pte_t pte, int pkey)
{
    pteval_t pteval = pte_val(pte);
    // 1. 清除当前PTE中的所有PKEY位
    pteval &= ~_PAGE_PKEY_MASK;
    // 2. 设置新的PKEY位
    pteval |= _PAGE_PKEY(pkey);
    return __pte(pteval);
}

static int __init pks_test_init(void)
{
    struct page *test_page = NULL;
    void *ptr = NULL;
    pte_t *ptep = NULL;
    pte_t original_pte;
    u32 original_pkrs, new_pkrs;
    int ret = 0;
    int faulted = 0;
    char dummy_char;

    printk(KERN_INFO "PKS PTE test module loaded\n");

    ret = register_kprobe(&kp);
    if (ret < 0) {
        printk(KERN_ERR "Failed to register kprobe: %d\n", ret);
        return ret;
    }

    my_kallsyms_lookup_name = (kallsyms_lookup_name_t)kp.addr;
    unregister_kprobe(&kp);

    myfound_init_top_pgt = (pgd_t *)my_kallsyms_lookup_name("init_top_pgt");
    if (!myfound_init_top_pgt) {
        printk(KERN_ERR "Failed to lookup init_top_pgt\n");
        return -EFAULT;
    }

    myfound_native_write_cr4 = (void *)my_kallsyms_lookup_name("native_write_cr4"); // find func native_write_cr4
    if (!myfound_native_write_cr4) {
        printk(KERN_ERR "Failed to lookup native_write_cr4\n");
        return -EFAULT;
    }

    // 步骤 1: 检查 PKS 是否被支持和启用
    if (!(read_cr4_reg() & X86_CR4_PKS)) {
        printk(KERN_ERR "PKS is not supported or not enabled in CR4: 0x%lx\n", read_cr4_reg());

        // try set CR4_PKS
        write_cr4_reg(read_cr4_reg() | X86_CR4_PKS);
        if (!(read_cr4_reg() & X86_CR4_PKS)) {
            printk(KERN_ERR "Failed to set CR4_PKS\n");
            return -EOPNOTSUPP;
        }
    }
    printk(KERN_INFO "PKS is enabled in CR4\n");

    // 步骤 2: 分配一页内存
    test_page = alloc_page(GFP_KERNEL);
    if (!test_page) {
        printk(KERN_ERR "Failed to allocate a page\n");
        return -ENOMEM;
    }
    ptr = page_address(test_page);
    strcpy(ptr, "Hello PKS!"); // 写入一些数据
    printk(KERN_INFO "Allocated a page at virtual address %px\n", ptr);

    // 步骤 3: 查找该页的PTE
    ptep = lookup_pte_by_kernel_addr((unsigned long)ptr);
    if (!ptep) {
        printk(KERN_ERR "Failed to lookup PTE for address %px\n", ptr);
        ret = -EFAULT;
        goto cleanup_page;
    }
    original_pte = *ptep;
    printk(KERN_INFO "Found PTE for address %px. Original PTE value: 0x%llx\n", ptr, (unsigned long long)pte_val(original_pte));

    // 步骤 4: 修改PTE，添加保护密钥
    pte_t new_pte = pte_mkhpkey(original_pte, TEST_PKEY);
    set_pte_atomic(ptep, new_pte);
    pte_unmap(ptep); // 解除PTE映射
    printk(KERN_INFO "Set PKEY %d on PTE. New PTE value: 0x%llx\n", TEST_PKEY, (unsigned long long)pte_val(new_pte));

    // 步骤 5: 刷新TLB，使PTE更改生效
    // 必须刷新TLB，否则CPU可能使用旧的缓存条目
    // flush_tlb_all();
    // printk(KERN_INFO "TLB flushed.\n");

    // 步骤 6: 修改PKRS MSR，禁止对key的访问
    original_pkrs = read_pkrs_msr();
    // 设置WD(Write-Disable)位。WD位是第 2*PKEY+1 个bit。
    new_pkrs = original_pkrs | (1 << (TEST_PKEY * 2 + 1));
    write_pkrs_msr(new_pkrs);
    printk(KERN_INFO "Original PKRS: 0x%x, Set new PKRS to 0x%x to disable write for PKEY %d\n",
           original_pkrs, new_pkrs, TEST_PKEY);

    // 步骤 7: 尝试再次写受保护的内存
    printk(KERN_INFO "Attempting to read from protected memory address %px...\n", ptr);

    strcpy(ptr, "can i write to this page?"); // 写入一些数据

    printk(KERN_INFO "Attempting to write to protected memory address %px...\n", ptr);
    
    // 使用内核异常表来安全地处理预期的错误
    // 如果 1f 处的指令发生错误，执行流会跳转到 2f 处
    asm volatile(
        "1:\n\t"
        "movb (%[addr]), %[val]\n\t" // 这条指令应该会触发页错误
        "jmp 3f\n\t"               // 如果没有错误，跳过错误处理代码
        "2:\n\t"
        "movl $1, %[faulted]\n\t"    // 如果发生错误，将faulted设置为1
        "3:\n\t"
        ".pushsection \"__ex_table\",\"a\"\n\t"
        ".align 8\n\t"
        ".quad 1b, 2b\n\t"         // 异常表条目: from 1b, to 2b
        ".popsection\n\t"
        : [val] "=r"(dummy_char), [faulted] "+m"(faulted)
        : [addr] "r"(ptr)
        : "memory"
    );

    if (faulted) {
        printk(KERN_INFO "SUCCESS: Caught expected page fault when reading protected memory!\n");
    } else {
        printk(KERN_ERR "FAILURE: Did not catch a page fault. PKS protection might not be working. Value read: '%c'\n", dummy_char);
        ret = -EIO;
    }

    // 步骤 8: 清理和恢复
// cleanup_pkrs:
    write_pkrs_msr(original_pkrs);
    printk(KERN_INFO "Restored original PKRS MSR value: 0x%x\n", original_pkrs);

// cleanup_pte:
    ptep = lookup_pte_by_kernel_addr((unsigned long)ptr);
    if (ptep) {
        set_pte_atomic(ptep, original_pte);
        pte_unmap(ptep);
        // flush_tlb_all();
        printk(KERN_INFO "Restored original PTE value.\n");
    }

cleanup_page:
    __free_page(test_page);
    printk(KERN_INFO "Freed the test page.\n");

    printk(KERN_INFO "PKS PTE test finished.\n");
    return ret;
}

static void __exit pks_test_exit(void)
{
    printk(KERN_INFO "PKS PTE test module unloaded\n");
}

module_init(pks_test_init);
module_exit(pks_test_exit);