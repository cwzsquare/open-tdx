/*
 * VFIO PTE Verification Module
 * 
 * This module provides a way to verify that PTE entries have the _PAGE_USER
 * bit cleared for VFIO DMA mappings.
 * 
 * Usage:
 *   insmod verify_pte.ko
 *   echo <virtual_address> > /proc/verify_pte
 *   cat /proc/verify_pte
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/proc_fs.h>
#include <linux/uaccess.h>
#include <linux/mm.h>
#include <linux/sched/mm.h>
#include <linux/sched/task.h>
#include <linux/pid.h>
#include <linux/slab.h>
#include <asm/pgtable.h>

#define PROC_NAME "verify_pte"
#define MAX_ADDR_LEN 64

static struct proc_dir_entry *proc_entry;
static unsigned long target_vaddr = 0;
static pid_t target_pid = 0;  /* 0 means use current process */

/* Read handler for /proc/verify_pte */
static ssize_t verify_pte_read(struct file *file, char __user *buf,
			       size_t count, loff_t *ppos)
{
	char output[512];
	ssize_t len = 0;
	struct mm_struct *mm = NULL;
	struct task_struct *task = NULL;
	pte_t *pte;
	spinlock_t *ptl;
	unsigned long pte_val;
	bool has_user_bit;
	bool mm_acquired = false;

	if (*ppos > 0)
		return 0;

	if (!target_vaddr) {
		len = snprintf(output, sizeof(output),
			       "No address set. Write a virtual address first.\n");
		goto out;
	}

	/* Get mm_struct from target PID or current process */
	if (target_pid > 0) {
		rcu_read_lock();
		task = pid_task(find_vpid(target_pid), PIDTYPE_PID);
		if (task) {
			get_task_struct(task);
			mm = get_task_mm(task);
			if (mm)
				mm_acquired = true;
		}
		rcu_read_unlock();
		
		if (!mm) {
			if (task)
				put_task_struct(task);
			len = snprintf(output, sizeof(output),
				       "Error: Could not find process with PID %d\n"
				       "Or process has no mm_struct (kernel thread?)\n",
				       target_pid);
			goto out;
		}
	} else {
		mm = current->mm;
		if (!mm) {
			len = snprintf(output, sizeof(output),
				       "Error: No mm_struct available (current process)\n");
			goto out;
		}
		mmget(mm);
		mm_acquired = true;
	}

	mmap_read_lock(mm);
	
	/* Check if address is in a valid VMA first */
	struct vm_area_struct *vma = vma_lookup(mm, target_vaddr);
	if (!vma) {
		mmap_read_unlock(mm);
		len = snprintf(output, sizeof(output),
			       "Error: Address 0x%lx is not in any VMA\n"
			       "The address may not be mapped yet or may have been unmapped.\n",
			       target_vaddr);
		goto out;
	}
	
	pte = get_locked_pte(mm, target_vaddr, &ptl);
	if (!pte) {
		pte_unmap_unlock(pte, ptl);
		mmap_read_unlock(mm);
		len = snprintf(output, sizeof(output),
			       "Error: Could not get PTE for address 0x%lx\n"
			       "VMA exists (0x%lx-0x%lx) but PTE is NULL\n",
			       target_vaddr, vma->vm_start, vma->vm_end);
		goto out;
	}

	pte_val = pte_val(*pte);
	has_user_bit = !!(pte_val & _PAGE_USER);

	len = snprintf(output, sizeof(output),
		       "Virtual Address: 0x%lx\n"
		       "Target PID: %d\n"
		       "PTE Value: 0x%lx\n"
		       "_PAGE_USER bit: %s (0x%lx)\n"
		       "PTE Present: %s\n"
		       "Physical Address: 0x%lx\n",
		       target_vaddr,
		       target_pid > 0 ? target_pid : current->pid,
		       pte_val,
		       has_user_bit ? "SET (user page)" : "CLEARED (kernel page)",
		       pte_val & _PAGE_USER,
		       pte_present(*pte) ? "Yes" : "No",
		       pte_present(*pte) ? (pte_val & PAGE_MASK) : 0);

	pte_unmap_unlock(pte, ptl);
	mmap_read_unlock(mm);
	
	if (mm_acquired) {
		mmput(mm);
		mm_acquired = false;
	}

out:
	if (mm_acquired && mm) {
		mmput(mm);
	}
	if (task) {
		put_task_struct(task);
	}
	
	if (len > count)
		len = count;

	if (copy_to_user(buf, output, len))
		return -EFAULT;

	*ppos = len;
	return len;
}

/* Write handler for /proc/verify_pte */
static ssize_t verify_pte_write(struct file *file, const char __user *buf,
				size_t count, loff_t *ppos)
{
	char input[MAX_ADDR_LEN];
	unsigned long vaddr;
	pid_t pid = 0;
	int ret;
	char *pid_str, *addr_str;

	if (count >= MAX_ADDR_LEN)
		return -EINVAL;

	if (copy_from_user(input, buf, count))
		return -EFAULT;

	input[count] = '\0';

	/* Support format: "PID:ADDR" or just "ADDR" */
	pid_str = strstr(input, ":");
	if (pid_str) {
		*pid_str = '\0';
		pid_str++;
		addr_str = pid_str;
		
		ret = kstrtoint(input, 0, &pid);
		if (ret || pid <= 0) {
			pr_err("verify_pte: Invalid PID format\n");
			return ret ? ret : -EINVAL;
		}
	} else {
		addr_str = input;
	}

	ret = kstrtoul(addr_str, 0, &vaddr);
	if (ret) {
		pr_err("verify_pte: Invalid address format\n");
		return ret;
	}

	target_vaddr = vaddr;
	target_pid = pid;
	pr_info("verify_pte: Target address set to 0x%lx (PID: %d)\n", 
		target_vaddr, target_pid > 0 ? target_pid : current->pid);

	return count;
}

static const struct proc_ops verify_pte_proc_ops = {
	.proc_read = verify_pte_read,
	.proc_write = verify_pte_write,
};

static int __init verify_pte_init(void)
{
	proc_entry = proc_create(PROC_NAME, 0644, NULL, &verify_pte_proc_ops);
	if (!proc_entry) {
		pr_err("verify_pte: Failed to create /proc/%s\n", PROC_NAME);
		return -ENOMEM;
	}

	pr_info("verify_pte: Module loaded. Use /proc/%s to verify PTE status\n",
		PROC_NAME);
	pr_info("verify_pte: Usage: echo <hex_address> > /proc/%s\n", PROC_NAME);
	pr_info("verify_pte:        cat /proc/%s\n", PROC_NAME);

	return 0;
}

static void __exit verify_pte_exit(void)
{
	if (proc_entry)
		proc_remove(proc_entry);

	pr_info("verify_pte: Module unloaded\n");
}

module_init(verify_pte_init);
module_exit(verify_pte_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("VFIO PTE Verification");
MODULE_DESCRIPTION("Verify PTE _PAGE_USER bit status for VFIO DMA mappings");

