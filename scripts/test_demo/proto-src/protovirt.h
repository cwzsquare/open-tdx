#include <asm/asm.h>
#include <asm/io.h>
#include "macro.h"
#include "ept.h"

struct desc64 {
	uint16_t limit0;
	uint16_t base0;
	unsigned base1:8, s:1, type:4, dpl:2, p:1;
	unsigned limit1:4, avl:1, l:1, db:1, g:1, base2:8;
	uint32_t base3;
	uint32_t zero1;
} __attribute__((packed));

typedef struct guest_page_table_entry {
    uint64_t Present : 1;          // Bit 0: Present
    uint64_t ReadWrite : 1;       // Bit 1: Read/Write
    uint64_t UserSupervisor : 1;  // Bit 2: User/Supervisor
    uint64_t WriteThrough : 1;    // Bit 3: PWT
    uint64_t CacheDisable : 1;    // Bit 4: PCD
    uint64_t Accessed : 1;        // Bit 5: Accessed
    uint64_t Dirty : 1;           // Bit 6: Dirty (for PT entries)
    uint64_t PageSize : 1;        // Bit 7: PS (for PDPT/PD)
    uint64_t Ignored1 : 4;        // Bits 11:8 (Protection Key or Ignored)
    uint64_t PhysicalAddress : 40; // Bits 51:12
    uint64_t Available : 7;      // Bits 62:52 (Available for software)
    uint64_t NoExecute : 1;       // Bit 63: NX
    uint64_t Reserved : 4;       // Bit 63: NX
} guest_page_table_entry;

union __rflags_t
{
	uint64_t flags;
	struct
	{
		uint64_t cf : 1;
		uint64_t always_1 : 1;
		uint64_t pf : 1;
		uint64_t reserved_0 : 1;
		uint64_t af : 1;
		uint64_t reserved_1 : 1;
		uint64_t zf : 1;
		uint64_t sf : 1;
		uint64_t tf : 1;
		uint64_t intf : 1;
		uint64_t df : 1;
		uint64_t of : 1;
		uint64_t iopl : 2;
		uint64_t nt : 1;
		uint64_t reserved_2 : 1;
		uint64_t rf : 1;
		uint64_t vf : 1;
		uint64_t ac : 1;
		uint64_t vif : 1;
		uint64_t vip : 1;
		uint64_t idf : 1;
		uint64_t reserved_3 : 10;
	} bits;
};

typedef struct gen_regs {
  uint64_t rbp;
  uint64_t rbx;
  uint64_t rcx;
  uint64_t rdx;
  uint64_t rsi;
  uint64_t rdi;
  uint64_t r15;
  uint64_t r14;
  uint64_t r13;
  uint64_t r12;
  uint64_t r11;
  uint64_t r10;
  uint64_t r9;
  uint64_t r8;
  uint64_t rax;
  union __rflags_t rflags;
} gen_regs;

typedef struct spec_regs {
  uint64_t rsp;
  uint64_t rip;
} spec_regs;

typedef struct vcpu {
  gen_regs guest_gen_regs;
  gen_regs host_gen_regs;
  
  uint64_t* vmxonRegion;
  uint64_t* vmcsRegion;
  uint8_t* vm_memory;

  EPT_PML4_ENTRY* pml4;
  EPT_PML3_ENTRY* pml3;
  EPT_PML2_ENTRY* pml2;
  EPT_PML1_ENTRY* pml1;
} vcpu;

struct vcpu cpu = {0};

static inline unsigned long long notrace __rdmsr1(unsigned int msr)
{
  return __rdmsr(msr);
}

// CH 30.3, Vol 3
// VMXON instruction - Enter VMX operation
static inline int _vmxon(uint64_t phys)
{
	uint8_t ret;

	__asm__ __volatile__ ("vmxon %[pa]; setna %[ret]"
		: [ret]"=rm"(ret)
		: [pa]"m"(phys)
		: "cc", "memory");
	return ret;
}

// CH 24.11.2, Vol 3
static inline int vmread(uint64_t encoding, uint64_t *value)
{
	uint64_t tmp;
	uint8_t ret;
	/*
	if (enable_evmcs)
		return evmcs_vmread(encoding, value);
	*/
	__asm__ __volatile__("vmread %[encoding], %[value]; setna %[ret]"
		: [value]"=rm"(tmp), [ret]"=rm"(ret)
		: [encoding]"r"(encoding)
		: "cc", "memory");

	*value = tmp;
	return ret;
}
/*
 * A wrapper around vmread (taken from kvm vmx.c) that ignores errors
 * and returns zero if the vmread instruction fails.
 */
static inline uint64_t vmreadz(uint64_t encoding)
{
	uint64_t value = 0;
	vmread(encoding, &value);
	return value;
}

static inline int vmwrite(uint64_t encoding, uint64_t value)
{
	uint8_t ret;
	__asm__ __volatile__ ("vmwrite %[value], %[encoding]; setna %[ret]"
		: [ret]"=rm"(ret)
		: [value]"rm"(value), [encoding]"r"(encoding)
		: "cc", "memory");

	return ret;
}


// CH 24.2, Vol 3
// getting vmcs revision identifier
static inline uint32_t vmcs_revision_id(void)
{
	return __rdmsr1(MSR_IA32_VMX_BASIC);
}


// CH 23.7, Vol 3
// Enter in VMX mode


static inline int _vmptrld(uint64_t vmcs_pa)
{
	uint8_t ret;

	__asm__ __volatile__ ("vmptrld %[pa]; setna %[ret]"
		: [ret]"=rm"(ret)
		: [pa]"m"(vmcs_pa)
		: "cc", "memory");
	return ret;
}

// Ch A.2, Vol 3
// indicate whether any of the default1 controls may be 0
// if return 0, all the default1 controls are reserved and must be 1.
// if return 1,not all the default1 controls are reserved, and
// some (but not necessarily all) may be 0.

static inline uint64_t get_desc64_base(const struct desc64 *desc)
{
	return ((uint64_t)desc->base3 << 32) |
		(desc->base0 | ((desc->base1) << 16) | ((desc->base2) << 24));
}

extern inline void _vmlaunch(gen_regs* regs, uint64_t vmexit_addr);
extern inline void clear_regs(void);
extern inline void save_regs(gen_regs* regs);
extern inline void restore_regs(gen_regs* regs);

// Function prototypes
bool vmxSupport(void);
bool getVmxOperation(void);
bool vmcsOperations(void);
bool vmxoffOperation(void);
uint64_t init_ept(void);
void setup_guest_page_tables(void* page_table_addr);
bool initVmcsControlField(void);
uint32_t vmresume(void);
void vmexit_handler(void);
bool initVmLaunchProcess(void);
int __init start_init(void);
bool allocVmcsRegion(void);
unsigned long long default1_controls(void);
uint32_t vmExit_reason(void);
bool deallocate_vmxon_region(void);
bool deallocate_vmcs_region(void);
bool deallocate_guest_memory(void);

static inline uint64_t get_cr0(void)
{
	uint64_t cr0;

	__asm__ __volatile__("mov %%cr0, %[cr0]"
			     : /* output */ [cr0]"=r"(cr0));
	return cr0;
}

static inline uint64_t get_cr3(void)
{
	uint64_t cr3;

	__asm__ __volatile__("mov %%cr3, %[cr3]"
			     : /* output */ [cr3]"=r"(cr3));
	return cr3;
}

static inline uint64_t get_cr4(void)
{
	uint64_t cr4;

	__asm__ __volatile__("mov %%cr4, %[cr4]"
			     : /* output */ [cr4]"=r"(cr4));
	return cr4;
}


static inline uint16_t get_es1(void)
{
	uint16_t es;

	__asm__ __volatile__("mov %%es, %[es]"
			     : /* output */ [es]"=rm"(es));
	return es;
}

static inline uint16_t get_cs1(void)
{
	uint16_t cs;

	__asm__ __volatile__("mov %%cs, %[cs]"
			     : /* output */ [cs]"=rm"(cs));
	return cs;
}

static inline uint16_t get_ss1(void)
{
	uint16_t ss;

	__asm__ __volatile__("mov %%ss, %[ss]"
			     : /* output */ [ss]"=rm"(ss));
	return ss;
}

static inline uint16_t get_ds1(void)
{
	uint16_t ds;

	__asm__ __volatile__("mov %%ds, %[ds]"
			     : /* output */ [ds]"=rm"(ds));
	return ds;
}

static inline uint16_t get_fs1(void)
{
	uint16_t fs;

	__asm__ __volatile__("mov %%fs, %[fs]"
			     : /* output */ [fs]"=rm"(fs));
	return fs;
}

static inline uint16_t get_gs1(void)
{
	uint16_t gs;

	__asm__ __volatile__("mov %%gs, %[gs]"
			     : /* output */ [gs]"=rm"(gs));
	return gs;
}

static inline uint16_t get_tr1(void)
{
	uint16_t tr;

	__asm__ __volatile__("str %[tr]"
			     : /* output */ [tr]"=rm"(tr));
	return tr;
}

static inline uint64_t get_gdt_base1(void)
{
	struct desc_ptr gdt;
	__asm__ __volatile__("sgdt %[gdt]"
			     : /* output */ [gdt]"=m"(gdt));
	return gdt.address;
}

static inline uint64_t get_idt_base1(void)
{
	struct desc_ptr idt;
	__asm__ __volatile__("sidt %[idt]"
			     : /* output */ [idt]"=m"(idt));
	return idt.address;
}

// CH 27.2.1, Vol 3
// Basic VM exit reason

// Dealloc vmxon region

/* Dealloc vmcs guest region*/

