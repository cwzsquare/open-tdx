#include <linux/init.h>
#include <linux/module.h>
#include <linux/const.h>
#include <linux/errno.h>
#include <linux/cdev.h>
#include <linux/interrupt.h>
#include <linux/fs.h>   /* Needed for KERN_INFO */
#include <linux/types.h>
#include <linux/errno.h>
#include <linux/fcntl.h>
#include <linux/init.h>
#include <linux/poll.h>
#include <linux/smp.h>
#include <linux/major.h>
#include <linux/fs.h>
#include <linux/device.h>
#include <linux/cpu.h>
#include <linux/notifier.h>
#include <linux/uaccess.h>
#include <linux/gfp.h>
#include <linux/slab.h>
#include <asm/errno.h>
#include <linux/mutex.h>
#include <linux/sched.h>
#include <linux/cpumask.h>

#include "macro.h"
#include "protovirt.h"
#include "exit_reason.h"

char* notes[16] = {0};

static dev_t dev;
static struct cdev *my_cdev;
static struct class *my_class;
static struct device *my_device;

static DEFINE_MUTEX(vmx_mutex);

static int set_cpu_affinity(struct task_struct *task, int cpu) {
    int ret;
    struct cpumask *mask = kzalloc(sizeof(struct cpumask), GFP_KERNEL);
    if (!mask)
        return -ENOMEM;

    cpumask_clear(mask);
    cpumask_set_cpu(cpu, mask);
    ret = set_cpus_allowed_ptr(task, mask);
    kfree(mask);
    return ret;
}

// CH 23.6, Vol 3
// Checking the support of VMX
bool vmxSupport(void)
{

    int getVmxSupport, vmxBit;
    __asm__("mov $1, %rax");
    __asm__("cpuid");
    __asm__("mov %%ecx , %0\n\t":"=r" (getVmxSupport));
    vmxBit = (getVmxSupport >> 5) & 1;
    if (vmxBit == 1){
        return true;
    }
    else {
        return false;
    }
    return false;

}

// CH 23.7, Vol 3
// Enter in VMX mode
bool getVmxOperation(void) {
	unsigned long cr4;
	unsigned long cr0;
  uint64_t feature_control;
	uint64_t required;
	long int vmxon_phy_region = 0;
	u32 low1 = 0;
    // setting CR4.VMXE[bit 13] = 1
    __asm__ __volatile__("mov %%cr4, %0" : "=r"(cr4) : : "memory");
    cr4 |= X86_CR4_VMXE;
    __asm__ __volatile__("mov %0, %%cr4" : : "r"(cr4) : "memory");

    /*
	 * Configure IA32_FEATURE_CONTROL MSR to allow VMXON:
	 *  Bit 0: Lock bit. If clear, VMXON causes a #GP.
	 *  Bit 2: Enables VMXON outside of SMX operation. If clear, VMXON
	 *    outside of SMX causes a #GP.
	 */
	required = FEATURE_CONTROL_VMXON_ENABLED_OUTSIDE_SMX;
	required |= FEATURE_CONTROL_LOCKED;
	feature_control = __rdmsr1(MSR_IA32_FEATURE_CONTROL);

	if ((feature_control & required) != required) {
		wrmsr(MSR_IA32_FEATURE_CONTROL, feature_control | required, low1);
	}

	/*
	 * Ensure bits in CR0 and CR4 are valid in VMX operation:
	 * - Bit X is 1 in _FIXED0: bit X is fixed to 1 in CRx.
	 * - Bit X is 0 in _FIXED1: bit X is fixed to 0 in CRx.
	 */
	__asm__ __volatile__("mov %%cr0, %0" : "=r"(cr0) : : "memory");
	cr0 &= __rdmsr1(MSR_IA32_VMX_CR0_FIXED1);
	cr0 |= __rdmsr1(MSR_IA32_VMX_CR0_FIXED0);
	__asm__ __volatile__("mov %0, %%cr0" : : "r"(cr0) : "memory");

	__asm__ __volatile__("mov %%cr4, %0" : "=r"(cr4) : : "memory");
	cr4 &= __rdmsr1(MSR_IA32_VMX_CR4_FIXED1);
	cr4 |= __rdmsr1(MSR_IA32_VMX_CR4_FIXED0);
	__asm__ __volatile__("mov %0, %%cr4" : : "r"(cr4) : "memory");

	// allocating 4kib((4096 bytes) of memory for vmxon region
	cpu.vmxonRegion = kzalloc(MYPAGE_SIZE,GFP_KERNEL);
   	if(cpu.vmxonRegion==NULL){
		printk(KERN_INFO "Error allocating vmxon region\n");
      	return false;
   	}
	vmxon_phy_region = __pa(cpu.vmxonRegion);
	*(uint32_t *)cpu.vmxonRegion = vmcs_revision_id();
	if (_vmxon(vmxon_phy_region))
		return false;
	return true;
}

// CH 24.2, Vol 3
// allocating VMCS region
bool vmcsOperations(void) {
	long int vmcsPhyRegion = 0;
	if (allocVmcsRegion()){
		vmcsPhyRegion = __pa(cpu.vmcsRegion);
		*(uint32_t *)cpu.vmcsRegion = vmcs_revision_id();
	}
	else {
		return false;
	}

	//making the vmcs active and current
	if (_vmptrld(vmcsPhyRegion))
		return false;
	return true;
}

bool vmxoffOperation(void)
{
  set_cpu_affinity(current, 0);
	if (deallocate_vmxon_region()) {
		printk(KERN_INFO "Successfully freed allocated vmxon region!\n");
	}
	else {
		printk(KERN_INFO "Error freeing allocated vmxon region!\n");
	}
	if (deallocate_vmcs_region()) {
		printk(KERN_INFO "Successfully freed allocated vmcs region!\n");
	}
	else {
		printk(KERN_INFO "Error freeing allocated vmcs region!\n");
	}
	if (deallocate_guest_memory()) {
		printk(KERN_INFO "Successfully freed guest memory!\n");
	}
	else {
		printk(KERN_INFO "Error freeing guest memory!\n");
	}
	asm volatile ("vmxoff\n" : : : "cc");
	return true;
}

uint64_t init_ept(void) {
    
    cpu.vm_memory = kzalloc(MYPAGE_SIZE*512, GFP_KERNEL);
    // Allocate EPT structures
    cpu.pml4 = (EPT_PML4_ENTRY*)kzalloc(MYPAGE_SIZE, GFP_KERNEL); // 1 page for PML4
    cpu.pml3 = (EPT_PML3_ENTRY*)kzalloc(MYPAGE_SIZE, GFP_KERNEL); // 1 page for PDPT
    cpu.pml2 = (EPT_PML2_ENTRY*)kzalloc(MYPAGE_SIZE, GFP_KERNEL);   // 1 page for PD
    cpu.pml1 = (EPT_PML1_ENTRY*)kzalloc(MYPAGE_SIZE*512, GFP_KERNEL);   // 1 page for PD

    printk(KERN_INFO "VMX: pml4 %pK %llx\n", cpu.pml4, (unsigned long long)virt_to_phys(cpu.pml4));
    printk(KERN_INFO "VMX: pml3 %pK %llx\n", cpu.pml3, (unsigned long long)virt_to_phys(cpu.pml3));
    printk(KERN_INFO "VMX: pml2 %pK %llx\n", cpu.pml2, (unsigned long long)virt_to_phys(cpu.pml2));
    printk(KERN_INFO "VMX: pml1 %pK %llx\n", cpu.pml1, (unsigned long long)virt_to_phys(cpu.pml1));
    printk(KERN_INFO "VMX: vm_memory %pK %llx\n", cpu.vm_memory, (unsigned long long)virt_to_phys(cpu.vm_memory));

    // Setup EPT hierarchy
    cpu.pml4[0].Fields.PhysicalAddress = virt_to_phys(cpu.pml3) >> 12;
    cpu.pml4[0].Fields.Read = 1;
    cpu.pml4[0].Fields.Write = 1;
    cpu.pml4[0].Fields.Execute = 1;

    for (int i = 0; i < 512; i++) {
      cpu.pml3[i].Fields.PhysicalAddress = virt_to_phys(cpu.pml2) >> 12;
      cpu.pml3[i].Fields.Read = 1;
      cpu.pml3[i].Fields.Write = 1;
      cpu.pml3[i].Fields.Execute = 1;
    }

    for (int i = 0; i < 512; i++) {
      cpu.pml2[i].Fields.PhysicalAddress = virt_to_phys(cpu.pml1 + i*512) >> 12;
      cpu.pml2[i].Fields.Read = 1;
      cpu.pml2[i].Fields.Write = 1;
      cpu.pml2[i].Fields.Execute = 1;
    }

    for (int i = 0; i < 512*512; i++) {
      cpu.pml1[i].Fields.PhysicalAddress = virt_to_phys(cpu.vm_memory + i*0x1000) >> 12;
      cpu.pml1[i].Fields.Read = 1;
      cpu.pml1[i].Fields.Write = 1;
      cpu.pml1[i].Fields.Execute = 1;
      cpu.pml1[i].Fields.EPTMemoryType = 0;
    }

    return virt_to_phys(cpu.pml4);
}

void setup_guest_page_tables(void* page_table_addr) {
    guest_page_table_entry *pml4 = (guest_page_table_entry*)(page_table_addr+0x10000);
    guest_page_table_entry *pdpt = (guest_page_table_entry*)(page_table_addr+0x11000);
    guest_page_table_entry *pd   = (guest_page_table_entry*)(page_table_addr+0x12000);

    // PML4 at GPA 0x10000
    pml4[0].PhysicalAddress = 0x11000 >> 12; // Point to PDPT
    pml4[0].Present = 1;
    pml4[0].ReadWrite = 1;

    // PDPT at GPA 0x11000
    pdpt[0].PhysicalAddress = 0x12000 >> 12; // Point to PD
    pdpt[0].Present = 1;
    pdpt[0].ReadWrite = 1;
    pdpt[0].UserSupervisor = 1;

    pd[0].PhysicalAddress = 0; //(0 * 0x200000LL) >> 12;
    pd[0].Present = 1;
    pd[0].ReadWrite = 1;
    pd[0].UserSupervisor = 1;
    pd[0].PageSize = 1; // 2MB page
    pd[0].CacheDisable = 0;
    pd[0].NoExecute = 0;

    // PD at GPA 0x12000, map GVA 0x0 to GPA 0x0 (2MB page)
    //for (int i = 0; i < 512; i++) {
    //  pd[i].PhysicalAddress = (i*0x200000LL) >> 12;
    //  pd[i].Present = 1;
    //  pd[i].ReadWrite = 1;
    //  pd[i].UserSupervisor = 1;
    //  pd[i].PageSize = 1; // 2MB page
    //  pd[i].CacheDisable = 0;
    //  pd[i].NoExecute = 0;
    //}
}

// Initializing VMCS control field
bool initVmcsControlField(void) {
	// checking of any of the default1 controls may be 0:
	//not doing it for now.

	// CH A.3.1, Vol 3

  // setting pin based controls, proc based controls, vm exit controls
	// and vm entry controls

	uint32_t pinbased_control0 = __rdmsr1(MSR_IA32_VMX_PINBASED_CTLS);
	uint32_t pinbased_control1 = __rdmsr1(MSR_IA32_VMX_PINBASED_CTLS) >> 32;
	uint32_t procbased_control0 = __rdmsr1(MSR_IA32_VMX_PROCBASED_CTLS);
	uint32_t procbased_control1 = __rdmsr1(MSR_IA32_VMX_PROCBASED_CTLS) >> 32;
	uint32_t procbased_secondary_control0 = __rdmsr1(MSR_IA32_VMX_PROCBASED_CTLS2);
	uint32_t procbased_secondary_control1 = __rdmsr1(MSR_IA32_VMX_PROCBASED_CTLS2) >> 32;
	uint32_t vm_exit_control0 = __rdmsr1(MSR_IA32_VMX_EXIT_CTLS);
	uint32_t vm_exit_control1 = __rdmsr1(MSR_IA32_VMX_EXIT_CTLS) >> 32;
	uint32_t vm_entry_control0 = __rdmsr1(MSR_IA32_VMX_ENTRY_CTLS);
	uint32_t vm_entry_control1 = __rdmsr1(MSR_IA32_VMX_ENTRY_CTLS) >> 32;


	// setting final value to write to control fields
	uint32_t pinbased_control_final = (pinbased_control0 & pinbased_control1);
	uint32_t procbased_control_final = (procbased_control0 & procbased_control1);
	uint32_t procbased_secondary_control_final = (procbased_secondary_control0 & procbased_secondary_control1);
	uint32_t host_address_space = 1 << 9;
	uint32_t vm_exit_control_final = (vm_exit_control0 & vm_exit_control1);
	vm_exit_control_final = vm_exit_control_final | host_address_space;
	uint32_t vm_entry_control_final = (vm_entry_control0 & vm_entry_control1);

	/* CH 24.7.1, Vol 3
	// for supporting 64 bit host
	uint32_t host_address_space = 1 << 9;
	vm_exit_control_final = vm_exit_control_final | host_address_space;
	*/
	/* To enable secondary controls
	procbased_control_final = procbased_control_final | ACTIVATE_SECONDARY_CONTROLS;
	*/
	/* for enabling unrestricted guest mode
	uint64_t unrestricted_guest = 1 << 7;
	// for enabling ept
	uint64_t enabling_ept = 1 << 1;
	uint32_t procbased_secondary_control_final = procbased_secondary_control_final | unrestricted_guest | enabling_ept;
	*/
	
  uint64_t enabling_ept = 1 << 1;
	procbased_control_final = procbased_control_final | ACTIVATE_SECONDARY_CONTROLS;
	procbased_secondary_control_final = procbased_secondary_control_final | enabling_ept;

	// writing the value to control field
	vmwrite(PIN_BASED_VM_EXEC_CONTROLS, pinbased_control_final);
	vmwrite(PROC_BASED_VM_EXEC_CONTROLS, procbased_control_final);
	vmwrite(PROC2_BASED_VM_EXEC_CONTROLS, procbased_secondary_control_final);
	vmwrite(VM_EXIT_CONTROLS, vm_exit_control_final);
	vmwrite(VM_ENTRY_CONTROLS, vm_entry_control_final);
	// to ignore the guest exception
	// maybe optional
	vmwrite(EXCEPTION_BITMAP, 0);

	vmwrite(VIRTUAL_PROCESSOR_ID, 0);

	vmwrite(VM_EXIT_CONTROLS, __rdmsr1(MSR_IA32_VMX_EXIT_CTLS) |
		VM_EXIT_HOST_ADDR_SPACE_SIZE);
	vmwrite(VM_ENTRY_CONTROLS, __rdmsr1(MSR_IA32_VMX_ENTRY_CTLS) |
		VM_ENTRY_IA32E_MODE);

	// CH 26.2.2, Vol 3
	// Checks on Host Control Registers and MSRs
	vmwrite(HOST_CR0, get_cr0());
	vmwrite(HOST_CR3, get_cr3());
	vmwrite(HOST_CR4, get_cr4());

	//setting host selectors fields
	vmwrite(HOST_ES_SELECTOR, get_es1());
	vmwrite(HOST_CS_SELECTOR, get_cs1());
	vmwrite(HOST_SS_SELECTOR, get_ss1());
	vmwrite(HOST_DS_SELECTOR, get_ds1());
	vmwrite(HOST_FS_SELECTOR, get_fs1());
	vmwrite(HOST_GS_SELECTOR, get_gs1());
	vmwrite(HOST_TR_SELECTOR, get_tr1());
	vmwrite(HOST_FS_BASE, __rdmsr1(MSR_FS_BASE));
	vmwrite(HOST_GS_BASE, __rdmsr1(MSR_GS_BASE));
	vmwrite(HOST_TR_BASE, get_desc64_base((struct desc64 *)(get_gdt_base1() + get_tr1())));
	vmwrite(HOST_GDTR_BASE, get_gdt_base1());
	vmwrite(HOST_IDTR_BASE, get_idt_base1());
	vmwrite(HOST_IA32_SYSENTER_ESP, __rdmsr1(MSR_IA32_SYSENTER_ESP));
	vmwrite(HOST_IA32_SYSENTER_EIP, __rdmsr1(MSR_IA32_SYSENTER_EIP));
	vmwrite(HOST_IA32_SYSENTER_CS, __rdmsr(MSR_IA32_SYSENTER_CS));

	// CH 26.3, Vol 3
	// setting the guest control area
	vmwrite(GUEST_ES_SELECTOR, vmreadz(HOST_ES_SELECTOR));
	vmwrite(GUEST_CS_SELECTOR, vmreadz(HOST_CS_SELECTOR));
	vmwrite(GUEST_SS_SELECTOR, vmreadz(HOST_SS_SELECTOR));
	vmwrite(GUEST_DS_SELECTOR, vmreadz(HOST_DS_SELECTOR));
	vmwrite(GUEST_FS_SELECTOR, vmreadz(HOST_FS_SELECTOR));
	vmwrite(GUEST_GS_SELECTOR, vmreadz(HOST_GS_SELECTOR));
	vmwrite(GUEST_LDTR_SELECTOR, 0);
	vmwrite(GUEST_TR_SELECTOR, vmreadz(HOST_TR_SELECTOR));
	vmwrite(GUEST_INTR_STATUS, 0);
	vmwrite(GUEST_PML_INDEX, 0);

	vmwrite(VMCS_LINK_POINTER, -1ll);
	vmwrite(GUEST_IA32_DEBUGCTL, 0);
	vmwrite(GUEST_IA32_PAT, vmreadz(HOST_IA32_PAT));
	vmwrite(GUEST_IA32_EFER, vmreadz(HOST_IA32_EFER));
	vmwrite(GUEST_IA32_PERF_GLOBAL_CTRL,
	vmreadz(HOST_IA32_PERF_GLOBAL_CTRL));

	vmwrite(GUEST_ES_LIMIT, -1);
	vmwrite(GUEST_CS_LIMIT, -1);
	vmwrite(GUEST_SS_LIMIT, -1);
	vmwrite(GUEST_DS_LIMIT, -1);
	vmwrite(GUEST_FS_LIMIT, -1);
	vmwrite(GUEST_GS_LIMIT, -1);
	vmwrite(GUEST_LDTR_LIMIT, -1);
	vmwrite(GUEST_TR_LIMIT, 0x67);
	vmwrite(GUEST_GDTR_LIMIT, 0xffff);
	vmwrite(GUEST_IDTR_LIMIT, 0xffff);
	vmwrite(GUEST_ES_AR_BYTES,
	vmreadz(GUEST_ES_SELECTOR) == 0 ? 0x10000 : 0xc093);
	vmwrite(GUEST_CS_AR_BYTES, 0xa09b);
	vmwrite(GUEST_SS_AR_BYTES, 0xc093);
	vmwrite(GUEST_DS_AR_BYTES,
	vmreadz(GUEST_DS_SELECTOR) == 0 ? 0x10000 : 0xc093);
	vmwrite(GUEST_FS_AR_BYTES,
	vmreadz(GUEST_FS_SELECTOR) == 0 ? 0x10000 : 0xc093);
	vmwrite(GUEST_GS_AR_BYTES,
	vmreadz(GUEST_GS_SELECTOR) == 0 ? 0x10000 : 0xc093);
	vmwrite(GUEST_LDTR_AR_BYTES, 0x10000);
	vmwrite(GUEST_TR_AR_BYTES, 0x8b);
	vmwrite(GUEST_INTERRUPTIBILITY_INFO, 0);
	vmwrite(GUEST_ACTIVITY_STATE, 0);
	vmwrite(GUEST_SYSENTER_CS, vmreadz(HOST_IA32_SYSENTER_CS));
	vmwrite(VMX_PREEMPTION_TIMER_VALUE, 0);

  uint64_t guest_cr0 = vmreadz(HOST_CR0); 
  //guest_cr0 = (1UL << 0) | (1UL << 31);
	vmwrite(GUEST_CR0, guest_cr0);
	vmwrite(GUEST_CR3, 0x10000); //vmreadz(HOST_CR3));
	vmwrite(GUEST_CR4, vmreadz(HOST_CR4));
	vmwrite(GUEST_ES_BASE, 0);
	vmwrite(GUEST_CS_BASE, 0);
	vmwrite(GUEST_SS_BASE, 0);
	vmwrite(GUEST_DS_BASE, 0);
	vmwrite(GUEST_FS_BASE, vmreadz(HOST_FS_BASE));
	vmwrite(GUEST_GS_BASE, vmreadz(HOST_GS_BASE));
	vmwrite(GUEST_LDTR_BASE, 0);
	vmwrite(GUEST_TR_BASE, vmreadz(HOST_TR_BASE));
	vmwrite(GUEST_GDTR_BASE, vmreadz(HOST_GDTR_BASE));
	vmwrite(GUEST_IDTR_BASE, vmreadz(HOST_IDTR_BASE));
	vmwrite(GUEST_RFLAGS, 2);
	vmwrite(GUEST_SYSENTER_ESP, vmreadz(HOST_IA32_SYSENTER_ESP));
	vmwrite(GUEST_SYSENTER_EIP, vmreadz(HOST_IA32_SYSENTER_EIP));
	// setting up rip and rsp for guest
	void *costum_rip;
	void *costum_rsp;

	costum_rsp = (void*)0x1000;
	costum_rip = 0;
	vmwrite(GUEST_RSP, (uint64_t)costum_rsp);
	vmwrite(GUEST_RIP, (uint64_t)costum_rip);

  EPTP eptp = {0};
  uint64_t pml4_phys = init_ept();
  eptp.Fields.PML4Address = pml4_phys >> 12;
  eptp.Fields.MemoryType = 6; // uncached
  eptp.Fields.PageWalkLength = 3;
  eptp.Fields.DirtyAndAccessEnabled = 1;

  printk(KERN_INFO "VMX: main_ept: %llx", (unsigned long long)eptp.All);
  vmwrite(EPT_POINTER, eptp.All);
  setup_guest_page_tables(cpu.vm_memory);

	return true;
}

uint32_t vmresume(void) {
  uint64_t insn_length;
  uint64_t guest_rip;
  uint32_t status = vmread(VMX_VMEXIT_INSTRUCTION_LENGTH, &insn_length);
  if (status) {
    printk(KERN_INFO "VMX: failed adjusting");
    return 1;
  }

  status = vmread(GUEST_RIP, &guest_rip);
  if (status) {
    printk(KERN_INFO "VMX: failed adjusting");
    return 1;
  }

  vmwrite(GUEST_RIP, guest_rip+insn_length);

  restore_regs(&cpu.guest_gen_regs);
  __asm__ __volatile__ (
      "xorq %rdi, %rdi;"
      "vmresume;"
  );
  return 0;
}

inline void info(void) {
    uint64_t exit_reason, guest_physical_address, exit_qualification, guest_rip, guest_rsp, guest_cr3, guest_cr0;
    asm volatile("vmread %1, %0" : "=r"(exit_reason) : "r"((uint64_t)0x4402)); // VMCS_EXIT_REASON
    asm volatile("vmread %1, %0" : "=r"(guest_physical_address) : "r"((uint64_t)0x2400)); // VMCS_GUEST_PHYSICAL_ADDRESS
    asm volatile("vmread %1, %0" : "=r"(exit_qualification) : "r"((uint64_t)0x6400)); // VMCS_EXIT_QUALIFICATION
    asm volatile("vmread %1, %0" : "=r"(guest_rip) : "r"((uint64_t)0x681E)); // VMCS_GUEST_RIP
    asm volatile("vmread %1, %0" : "=r"(guest_rsp) : "r"((uint64_t)0x681C)); // VMCS_GUEST_RSP
    asm volatile("vmread %1, %0" : "=r"(guest_cr3) : "r"((uint64_t)0x6802)); // VMCS_GUEST_CR3
    asm volatile("vmread %1, %0" : "=r"(guest_cr0) : "r"((uint64_t)0x6800)); // VMCS_GUEST_CR0

    printk(KERN_INFO "VM Exit Reason: 0x%llx\n", exit_reason & 0xFFFF);
    if ((exit_reason & 0xFFFF) == 2) { // Triple fault
        printk(KERN_INFO "Triple Fault Detected!\n");
    } else if ((exit_reason & 0xFFFF) == 48) { // EPT violation
        printk(KERN_INFO "EPT Violation: Guest PA 0x%llx, Qual 0x%llx\n",
               guest_physical_address, exit_qualification);
        printk(KERN_INFO "Access: %s%s%s\n",
               exit_qualification & (1 << 0) ? "Read " : "",
               exit_qualification & (1 << 1) ? "Write " : "",
               exit_qualification & (1 << 2) ? "Execute " : "");
    }
    printk(KERN_INFO "Guest RIP: 0x%llx, RSP: 0x%llx, CR3: 0x%llx, CR0: 0x%llx\n",
           guest_rip, guest_rsp, guest_cr3, guest_cr0);
}

void vmexit_handler(void) {
  save_regs(&cpu.guest_gen_regs);
  uint32_t exit_reason = vmExit_reason();
  printk(KERN_INFO "VMX: vmexit_handler called: 0x%x 0x%llx\n", exit_reason, vmreadz(GUEST_RIP));
  info();

  uint64_t rax = cpu.guest_gen_regs.rax;
  uint64_t rbx = cpu.guest_gen_regs.rbx;
  switch (exit_reason) {
    case vmexit_vmcall:
      printk(KERN_INFO "VMX: vmcall or cpuid, RAX: 0x%llx\n", rax);
      switch (rax) {
        case 1:
          printk(KERN_INFO "Integer: 0x%llx\n", rbx);
          break;
        default:
          break;
      }
      break;
    default:
      restore_regs(&cpu.host_gen_regs);
      return;
  }
  if (!vmresume()) {
    restore_regs(&cpu.host_gen_regs);
  }
}

bool initVmLaunchProcess(void) {
  set_cpu_affinity(current, 0);
	_vmlaunch(&cpu.host_gen_regs, (uint64_t)vmexit_handler);
	printk(KERN_INFO "VM exit reason is %lu!\n", (unsigned long)vmExit_reason());
  memset(&cpu.guest_gen_regs, 0, sizeof(gen_regs));
	return true;
}

static long int proto_ioctl(struct file* file, uint32_t cmd, unsigned long arg) {
  int ret;
  unsigned long flags;

  set_cpu_affinity(current, 0);
  ret = mutex_lock_interruptible(&vmx_mutex);
  if (ret) {
    pr_err("Mutex lock failed: %d\n", ret);
    return ret;
  }
	if (!vmcsOperations()) {
		printk(KERN_INFO "VMCS Allocation failed! EXITING");
    mutex_unlock(&vmx_mutex);
		return 0;
	}
	else {
		printk(KERN_INFO "VMCS Allocation succeeded! CONTINUING");
	}
	if (!initVmcsControlField()) {
		printk(KERN_INFO "Initialization of VMCS Control field failed! EXITING");
    mutex_unlock(&vmx_mutex);
		return 0;
	}
	else {
		printk(KERN_INFO "Initializing of control fields to the most basic settings succeeded! CONTINUING");
	}
  if (copy_from_user(cpu.vm_memory, (void*)arg, 0x5000)) {
    mutex_unlock(&vmx_mutex);
    return -ENOENT;
  }

  local_irq_save(flags);

	if (!initVmLaunchProcess()) {
		printk(KERN_INFO "VMLAUNCH failed! EXITING");
    mutex_unlock(&vmx_mutex);
		return 0;
	}
	else {
		printk(KERN_INFO "VMLAUNCH succeeded! CONTINUING");
	}
  deallocate_guest_memory();
  local_irq_restore(flags);
  mutex_unlock(&vmx_mutex);

  return 0;
}

static const struct file_operations my_driver_fops = {
    .owner = THIS_MODULE,
    .unlocked_ioctl = proto_ioctl,
};

int __init start_init(void)
{
  int ret;
  set_cpu_affinity(current, 0);
  if (!vmxSupport()) {
		printk(KERN_INFO "VMX support not present! EXITING");
		return 0;
	}
	else {
		printk(KERN_INFO "VMX support present! CONTINUING");
	}
	if (!getVmxOperation()) {
		printk(KERN_INFO "VMX Operation failed! EXITING");
		return 0;
	}
	else {
		printk(KERN_INFO "VMX Operation succeeded! CONTINUING");
	}
  ret = alloc_chrdev_region(&dev, 0, 1, "protovirt");
  if (ret < 0) {
      pr_err("Failed to allocate chrdev region\n");
      return ret;
  }

  my_cdev = cdev_alloc();
  if (!my_cdev) {
      unregister_chrdev_region(dev, 1);
      return -ENOMEM;
  }

  cdev_init(my_cdev, &my_driver_fops);
  my_cdev->owner = THIS_MODULE;

  ret = cdev_add(my_cdev, dev, 1);
  if (ret < 0) {
      pr_err("Failed to add cdev\n");
      cdev_del(my_cdev);
      unregister_chrdev_region(dev, 1);
      return ret;
  }

  my_class = class_create("proto_class");
  if (IS_ERR(my_class)) {
      pr_err("Failed to create class: %ld\n", PTR_ERR(my_class));
      ret = PTR_ERR(my_class);
      return ret;
  }

  // Step 4: Create a device instance (triggers udev)
  my_device = device_create(my_class, NULL, dev, NULL, "proto");
  if (IS_ERR(my_device)) {
      pr_err("Failed to create device: %ld\n", PTR_ERR(my_device));
      ret = PTR_ERR(my_device);
      return ret;
  }

  pr_info("Proto driver initialized\n");
  return 0;
}

static void __exit end_exit(void)
{
  set_cpu_affinity(current, 0);
  printk(KERN_INFO "Unloading the driver\n");
  if (!vmxoffOperation()) {
		printk(KERN_INFO "VMXOFF operation failed! EXITING");
		return;
	}
	else {
		printk(KERN_INFO "VMXOFF Operation succeeded! CONTINUING\n");
	}
  device_destroy(my_class, dev);
  class_destroy(my_class);
  cdev_del(my_cdev);
  unregister_chrdev_region(dev, 1);
  printk(KERN_INFO "Driver unloaded\n");
	return;
}

module_init(start_init);
module_exit(end_exit);

// CH 23.7, Vol 3
// Enter in VMX mode
bool allocVmcsRegion(void) {
  uint64_t temp_region;
  if (!cpu.vmcsRegion) {
    cpu.vmcsRegion = kzalloc(MYPAGE_SIZE,GFP_KERNEL);
  } else {
    temp_region = (uint64_t)cpu.vmcsRegion;
    cpu.vmcsRegion = kzalloc(MYPAGE_SIZE, GFP_KERNEL);
    kfree((void*)temp_region);
  }
  if (cpu.vmcsRegion == NULL){
		printk(KERN_INFO "Error allocating vmcs region\n");
    return false;
  }
	return true;
}

// Ch A.2, Vol 3
// indicate whether any of the default1 controls may be 0
// if return 0, all the default1 controls are reserved and must be 1.
// if return 1,not all the default1 controls are reserved, and
// some (but not necessarily all) may be 0.
unsigned long long default1_controls(void){
	unsigned long long check_default1_controls = (unsigned long long)((__rdmsr1(MSR_IA32_VMX_BASIC) << 55) & 1);
	//printk(KERN_INFO "default1 controls value!---%llu\n", check_default1_controls);
	return check_default1_controls;
}

// CH 27.2.1, Vol 3
// Basic VM exit reason
uint32_t vmExit_reason(void) {
	uint32_t exit_reason = vmreadz(VM_EXIT_REASON);
	exit_reason = exit_reason & 0xffff;
	return exit_reason;
}

// Dealloc vmxon region
bool deallocate_vmxon_region(void) {
	if(cpu.vmxonRegion){
	    kfree(cpu.vmxonRegion);
      cpu.vmxonRegion = 0;
		return true;
  }
  return false;
}

/* Dealloc vmcs guest region*/
bool deallocate_vmcs_region(void) {
	if(cpu.vmcsRegion) {
    	printk(KERN_INFO "Freeing allocated vmcs region!\n");
    	kfree(cpu.vmcsRegion);
      cpu.vmcsRegion = 0;
		return true;
	}
	return false;
}

bool deallocate_guest_memory(void) {
  if (cpu.vm_memory) {
    kfree(cpu.vm_memory);
    cpu.vm_memory = 0;
  } else {
    return false;
  }
  if (cpu.pml4) {
    kfree(cpu.pml4);
    cpu.pml4 = 0;
  } else {
    return false;
  }
  if (cpu.pml3) {
    kfree(cpu.pml3);
    cpu.pml3 = 0;
  } else {
    return false;
  }
  if (cpu.pml2) {
    kfree(cpu.pml2);
    cpu.pml2 = 0;
  } else {
    return false;
  }
  if (cpu.pml1) {
    kfree(cpu.pml1);
    cpu.pml1 = 0;
  } else {
    return false;
  }
  return true;
}

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Pavel Blinnikov and Shubham Dubey");
MODULE_DESCRIPTION("Proto - A minimalistic Hypervisior with EPT support");
