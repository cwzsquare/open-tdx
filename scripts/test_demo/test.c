/*
 * LKL VMX Test Program
 * 
 * This program tests the LKL VMX ioctl functionality by:
 * 1. Opening /dev/kvm device
 * 2. Setting up initial guest state
 * 3. Calling KVM_LKL_VMLAUNCH
 * 4. Handling vmexit and calling KVM_LKL_VMRESUME
 * 5. Testing basic VMX functionality
 */

 #include <stdio.h>
 #include <stdlib.h>
 #include <string.h>
 #include <unistd.h>
 #include <fcntl.h>
 #include <errno.h>
 #include <sys/ioctl.h>
 #include <sys/mman.h>
 #include <stdint.h>
 #include <assert.h>
 
 /* KVM ioctl definitions */
 #define KVMIO 0xAE
 #define KVM_LKL_VMLAUNCH  _IOWR(KVMIO, 0xc0, struct kvm_lkl_vmx_state)
 #define KVM_LKL_VMEXIT    _IOWR(KVMIO, 0xc1, struct kvm_lkl_vmx_state)
 #define KVM_LKL_VMRESUME  _IOWR(KVMIO, 0xc2, struct kvm_lkl_vmx_state)
 
 /* KVM exit reasons */
 #define EXIT_REASON_EXCEPTION_NMI    1
 #define EXIT_REASON_EXTERNAL_INTERRUPT 2
 #define EXIT_REASON_TRIPLE_FAULT     3
 #define EXIT_REASON_INIT              4
 #define EXIT_REASON_SIPI             5
 #define EXIT_REASON_IO_SMI           6
 #define EXIT_REASON_OTHER_SMI        7
 #define EXIT_REASON_PENDING_INTERRUPT 8
 #define EXIT_REASON_NMI_WINDOW       9
 #define EXIT_REASON_TASK_SWITCH      10
 #define EXIT_REASON_CPUID            11
 #define EXIT_REASON_HLT              12
 #define EXIT_REASON_INVD             13
 #define EXIT_REASON_INVLPG           14
 #define EXIT_REASON_RDPMC            15
 #define EXIT_REASON_RDTSC            16
 #define EXIT_REASON_VMCALL           18
 #define EXIT_REASON_VMCLEAR          19
 #define EXIT_REASON_VMLAUNCH         20
 #define EXIT_REASON_VMPTRLD          21
 #define EXIT_REASON_VMPTRST          22
 #define EXIT_REASON_VMREAD           23
 #define EXIT_REASON_VMRESUME         24
 #define EXIT_REASON_VMWRITE          25
 #define EXIT_REASON_VMOFF            26
 #define EXIT_REASON_VMON             27
 #define EXIT_REASON_CR_ACCESS        28
 #define EXIT_REASON_DR_ACCESS        29
 #define EXIT_REASON_IO_INSTRUCTION   30
 #define EXIT_REASON_MSR_READ         31
 #define EXIT_REASON_MSR_WRITE        32
 #define EXIT_REASON_INVALID_STATE   33
 #define EXIT_REASON_MSR_LOADING      34
 #define EXIT_REASON_MWAIT_INSTRUCTION 36
 #define EXIT_REASON_MONITOR_TRAP_FLAG 37
 #define EXIT_REASON_MONITOR_INSTRUCTION 39
 #define EXIT_REASON_PAUSE_INSTRUCTION 40
 #define EXIT_REASON_MCE_DURING_VMENTRY 41
 #define EXIT_REASON_TPR_BELOW_THRESHOLD 43
 #define EXIT_REASON_APIC_ACCESS      44
 #define EXIT_REASON_EOI_INDUCED      45
 #define EXIT_REASON_GDTR_IDTR        46
 #define EXIT_REASON_LDTR_TR          47
 #define EXIT_REASON_EPT_VIOLATION    48
 #define EXIT_REASON_EPT_MISCONFIG    49
 #define EXIT_REASON_INVEPT           50
 #define EXIT_REASON_RDTSCP           51
 #define EXIT_REASON_PREEMPTION_TIMER 52
 #define EXIT_REASON_INVVPID          53
 #define EXIT_REASON_WBINVD           54
 #define EXIT_REASON_XSETBV           55
 #define EXIT_REASON_APIC_WRITE       56
 #define EXIT_REASON_RDRAND           57
 #define EXIT_REASON_INVPCID          58
 #define EXIT_REASON_VMFUNC           59
 #define EXIT_REASON_ENCLS            60
 #define EXIT_REASON_RDSEED           61
 #define EXIT_REASON_PML_FULL         62
 #define EXIT_REASON_XSAVES           63
 #define EXIT_REASON_XRSTORS          64
 #define EXIT_REASON_UMIP             67
 #define EXIT_REASON_PAUSE_LOOP_EXITING 68
 #define EXIT_REASON_LOAD_CET_STATE   69
 #define EXIT_REASON_ENCLV            70
 #define EXIT_REASON_BUS_LOCK         71
 #define EXIT_REASON_NOTIFY           72
 
 /* Segment descriptor structure */
 struct kvm_segment {
     uint64_t base;
     uint32_t limit;
     uint16_t selector;
     uint8_t  type;
     uint8_t  present, dpl, db, s, l, g, avl;
     uint8_t  unusable;
     uint8_t  padding;
 };
 
 struct kvm_dtable {
     uint64_t base;
     uint16_t limit;
     uint16_t padding[3];
 };
 
 /* LKL VMX state structure */
 struct kvm_lkl_vmx_state {
     /* General purpose registers */
     uint64_t rax, rbx, rcx, rdx;
     uint64_t rsi, rdi, rsp, rbp;
     uint64_t r8,  r9,  r10, r11;
     uint64_t r12, r13, r14, r15;
     uint64_t rip, rflags;
     
     /* Segment registers */
     struct kvm_segment cs, ds, es, fs, gs, ss;
     struct kvm_segment tr, ldt;
     struct kvm_dtable gdt, idt;
     
     /* Control registers */
     uint64_t cr0, cr2, cr3, cr4;
     uint64_t efer;
     
     /* Exit information (filled by kernel on vmexit) */
     uint32_t exit_reason;
     uint32_t exit_qualification_valid;
     uint64_t exit_qualification;
     uint64_t guest_physical_address;  /* For EPT violations */
     uint32_t exit_interruption_info;
     uint32_t exit_interruption_error_code;
     
     /* Reserved for future use */
     uint64_t reserved[8];
 };
 
 /* Global variables */
 static int kvm_fd = -1;
 static struct kvm_lkl_vmx_state guest_state;
 
 /* Function prototypes */
 static int open_kvm_device(void);
 static void setup_initial_guest_state(void);
 static int test_vmlaunch(void);
 static int test_vmresume(void);
 static int test_vmexit(void);
 static void print_guest_state(const struct kvm_lkl_vmx_state *state);
 static void print_exit_reason(uint32_t exit_reason);
 
 /* Open /dev/kvm device */
 static int open_kvm_device(void)
 {
     kvm_fd = open("/dev/kvm", O_RDWR);
     if (kvm_fd < 0) {
         perror("Failed to open /dev/kvm");
         return -1;
     }
     printf("Successfully opened /dev/kvm (fd=%d)\n", kvm_fd);
     return 0;
 }
 
/* Setup initial guest state for testing */
static void setup_initial_guest_state(void)
{
    /* Initialize state structure - kernel will capture current process state */
    memset(&guest_state, 0, sizeof(guest_state));
    
    printf("Guest state structure initialized - kernel will capture current process state\n");
}
 
 /* Test KVM_LKL_VMLAUNCH */
 static int test_vmlaunch(void)
 {
     int ret;
     
     printf("\n=== Testing KVM_LKL_VMLAUNCH ===\n");
     
     ret = ioctl(kvm_fd, KVM_LKL_VMLAUNCH, &guest_state);
     if (ret < 0) {
         perror("KVM_LKL_VMLAUNCH failed");
         return -1;
     }
     
     printf("KVM_LKL_VMLAUNCH returned: %d\n", ret);
     print_exit_reason(guest_state.exit_reason);
     
     if (guest_state.exit_reason != 0) {
         printf("VM exit occurred with reason: %u\n", guest_state.exit_reason);
         print_guest_state(&guest_state);
     }
     
     return 0;
 }
 
 /* Test KVM_LKL_VMRESUME */
 static int test_vmresume(void)
 {
     int ret;
     
     printf("\n=== Testing KVM_LKL_VMRESUME ===\n");
     
     ret = ioctl(kvm_fd, KVM_LKL_VMRESUME, &guest_state);
     if (ret < 0) {
         perror("KVM_LKL_VMRESUME failed");
         return -1;
     }
     
     printf("KVM_LKL_VMRESUME returned: %d\n", ret);
     print_exit_reason(guest_state.exit_reason);
     
     if (guest_state.exit_reason != 0) {
         printf("VM exit occurred with reason: %u\n", guest_state.exit_reason);
         print_guest_state(&guest_state);
     }
     
     return 0;
 }
 
 /* Test KVM_LKL_VMEXIT */
 static int test_vmexit(void)
 {
     int ret;
     
     printf("\n=== Testing KVM_LKL_VMEXIT ===\n");
     
     ret = ioctl(kvm_fd, KVM_LKL_VMEXIT, &guest_state);
     if (ret < 0) {
         perror("KVM_LKL_VMEXIT failed");
         return -1;
     }
     
     printf("KVM_LKL_VMEXIT returned: %d\n", ret);
     print_exit_reason(guest_state.exit_reason);
     
     return 0;
 }
 
 /* Print guest state for debugging */
 static void print_guest_state(const struct kvm_lkl_vmx_state *state)
 {
     printf("Guest State:\n");
     printf("  RIP: 0x%016lx\n", state->rip);
     printf("  RSP: 0x%016lx\n", state->rsp);
     printf("  RFLAGS: 0x%016lx\n", state->rflags);
     printf("  CR0: 0x%016lx\n", state->cr0);
     printf("  CR3: 0x%016lx\n", state->cr3);
     printf("  CR4: 0x%016lx\n", state->cr4);
     printf("  EFER: 0x%016lx\n", state->efer);
     printf("  CS: sel=0x%04x, base=0x%016lx, limit=0x%08x\n",
            state->cs.selector, state->cs.base, state->cs.limit);
     printf("  DS: sel=0x%04x, base=0x%016lx, limit=0x%08x\n",
            state->ds.selector, state->ds.base, state->ds.limit);
 }
 
 /* Print exit reason */
 static void print_exit_reason(uint32_t exit_reason)
 {
     const char *reason_str = "UNKNOWN";
     
     switch (exit_reason) {
     case 0: reason_str = "NO_EXIT"; break;
     case EXIT_REASON_EXCEPTION_NMI: reason_str = "EXCEPTION_NMI"; break;
     case EXIT_REASON_EXTERNAL_INTERRUPT: reason_str = "EXTERNAL_INTERRUPT"; break;
     case EXIT_REASON_TRIPLE_FAULT: reason_str = "TRIPLE_FAULT"; break;
     case EXIT_REASON_INIT: reason_str = "INIT"; break;
     case EXIT_REASON_SIPI: reason_str = "SIPI"; break;
     case EXIT_REASON_IO_SMI: reason_str = "IO_SMI"; break;
     case EXIT_REASON_OTHER_SMI: reason_str = "OTHER_SMI"; break;
     case EXIT_REASON_PENDING_INTERRUPT: reason_str = "PENDING_INTERRUPT"; break;
     case EXIT_REASON_NMI_WINDOW: reason_str = "NMI_WINDOW"; break;
     case EXIT_REASON_TASK_SWITCH: reason_str = "TASK_SWITCH"; break;
     case EXIT_REASON_CPUID: reason_str = "CPUID"; break;
     case EXIT_REASON_HLT: reason_str = "HLT"; break;
     case EXIT_REASON_INVD: reason_str = "INVD"; break;
     case EXIT_REASON_INVLPG: reason_str = "INVLPG"; break;
     case EXIT_REASON_RDPMC: reason_str = "RDPMC"; break;
     case EXIT_REASON_RDTSC: reason_str = "RDTSC"; break;
     case EXIT_REASON_VMCALL: reason_str = "VMCALL"; break;
     case EXIT_REASON_VMCLEAR: reason_str = "VMCLEAR"; break;
     case EXIT_REASON_VMLAUNCH: reason_str = "VMLAUNCH"; break;
     case EXIT_REASON_VMPTRLD: reason_str = "VMPTRLD"; break;
     case EXIT_REASON_VMPTRST: reason_str = "VMPTRST"; break;
     case EXIT_REASON_VMREAD: reason_str = "VMREAD"; break;
     case EXIT_REASON_VMRESUME: reason_str = "VMRESUME"; break;
     case EXIT_REASON_VMWRITE: reason_str = "VMWRITE"; break;
     case EXIT_REASON_VMOFF: reason_str = "VMOFF"; break;
     case EXIT_REASON_VMON: reason_str = "VMON"; break;
     case EXIT_REASON_CR_ACCESS: reason_str = "CR_ACCESS"; break;
     case EXIT_REASON_DR_ACCESS: reason_str = "DR_ACCESS"; break;
     case EXIT_REASON_IO_INSTRUCTION: reason_str = "IO_INSTRUCTION"; break;
     case EXIT_REASON_MSR_READ: reason_str = "MSR_READ"; break;
     case EXIT_REASON_MSR_WRITE: reason_str = "MSR_WRITE"; break;
     case EXIT_REASON_INVALID_STATE: reason_str = "INVALID_STATE"; break;
     case EXIT_REASON_MSR_LOADING: reason_str = "MSR_LOADING"; break;
     case EXIT_REASON_MWAIT_INSTRUCTION: reason_str = "MWAIT_INSTRUCTION"; break;
     case EXIT_REASON_MONITOR_TRAP_FLAG: reason_str = "MONITOR_TRAP_FLAG"; break;
     case EXIT_REASON_MONITOR_INSTRUCTION: reason_str = "MONITOR_INSTRUCTION"; break;
     case EXIT_REASON_PAUSE_INSTRUCTION: reason_str = "PAUSE_INSTRUCTION"; break;
     case EXIT_REASON_MCE_DURING_VMENTRY: reason_str = "MCE_DURING_VMENTRY"; break;
     case EXIT_REASON_TPR_BELOW_THRESHOLD: reason_str = "TPR_BELOW_THRESHOLD"; break;
     case EXIT_REASON_APIC_ACCESS: reason_str = "APIC_ACCESS"; break;
     case EXIT_REASON_EOI_INDUCED: reason_str = "EOI_INDUCED"; break;
     case EXIT_REASON_GDTR_IDTR: reason_str = "GDTR_IDTR"; break;
     case EXIT_REASON_LDTR_TR: reason_str = "LDTR_TR"; break;
     case EXIT_REASON_EPT_VIOLATION: reason_str = "EPT_VIOLATION"; break;
     case EXIT_REASON_EPT_MISCONFIG: reason_str = "EPT_MISCONFIG"; break;
     case EXIT_REASON_INVEPT: reason_str = "INVEPT"; break;
     case EXIT_REASON_RDTSCP: reason_str = "RDTSCP"; break;
     case EXIT_REASON_PREEMPTION_TIMER: reason_str = "PREEMPTION_TIMER"; break;
     case EXIT_REASON_INVVPID: reason_str = "INVVPID"; break;
     case EXIT_REASON_WBINVD: reason_str = "WBINVD"; break;
     case EXIT_REASON_XSETBV: reason_str = "XSETBV"; break;
     case EXIT_REASON_APIC_WRITE: reason_str = "APIC_WRITE"; break;
     case EXIT_REASON_RDRAND: reason_str = "RDRAND"; break;
     case EXIT_REASON_INVPCID: reason_str = "INVPCID"; break;
     case EXIT_REASON_VMFUNC: reason_str = "VMFUNC"; break;
     case EXIT_REASON_ENCLS: reason_str = "ENCLS"; break;
     case EXIT_REASON_RDSEED: reason_str = "RDSEED"; break;
     case EXIT_REASON_PML_FULL: reason_str = "PML_FULL"; break;
     case EXIT_REASON_XSAVES: reason_str = "XSAVES"; break;
     case EXIT_REASON_XRSTORS: reason_str = "XRSTORS"; break;
     case EXIT_REASON_UMIP: reason_str = "UMIP"; break;
     case EXIT_REASON_PAUSE_LOOP_EXITING: reason_str = "PAUSE_LOOP_EXITING"; break;
     case EXIT_REASON_LOAD_CET_STATE: reason_str = "LOAD_CET_STATE"; break;
     case EXIT_REASON_ENCLV: reason_str = "ENCLV"; break;
     case EXIT_REASON_BUS_LOCK: reason_str = "BUS_LOCK"; break;
     case EXIT_REASON_NOTIFY: reason_str = "NOTIFY"; break;
     }
     
     printf("Exit reason: %u (%s)\n", exit_reason, reason_str);
 }
 
/* Main test function */
int main(int argc, char *argv[])
{
    int ret = 0;
    
    printf("LKL VMX Test Program - Process State Capture Mode\n");
    printf("==================================================\n");
    printf("This program tests LKL VMX with automatic process state capture.\n");
    printf("The kernel will capture the current process state instead of\n");
    printf("requiring manual state setup from userspace.\n\n");
    
    /* Check if running as root */
    if (geteuid() != 0) {
        printf("Error: This program must be run as root\n");
        return 1;
    }
    
    /* Open KVM device */
    if (open_kvm_device() < 0) {
        return 1;
    }
    
    /* Setup initial guest state (minimal - kernel will capture actual state) */
    setup_initial_guest_state();
    
    /* Test VMLAUNCH - kernel captures current process state */
    printf("\n=== Testing VMLAUNCH with Process State Capture ===\n");
    if (test_vmlaunch() < 0) {
        ret = 1;
        goto cleanup;
    }
    
    /* Test VMEXIT - check for VMCALL exits */
    printf("\n=== Testing VMEXIT (VMCALL handling) ===\n");
    if (test_vmexit() < 0) {
        ret = 1;
        goto cleanup;
    }
    
    /* Test VMRESUME - continue execution */
    printf("\n=== Testing VMRESUME ===\n");
    if (test_vmresume() < 0) {
        ret = 1;
        goto cleanup;
    }
    
    printf("\n=== Test Summary ===\n");
    printf("LKL VMX tests with process state capture completed\n");
    printf("The kernel automatically captured and used the current process state\n");
    printf("for VMX execution in non-root ring3 mode.\n");
    
cleanup:
    if (kvm_fd >= 0) {
        close(kvm_fd);
    }
    
    return ret;
}