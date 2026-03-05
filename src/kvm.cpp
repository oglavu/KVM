#include "kvm.hpp"

#include <sys/ioctl.h>
#include <sys/mman.h>

#include <linux/kvm.h>

#include <fcntl.h>
#include <unistd.h>
#include <filesystem>


int vm_init(struct vm &v, size_t mem_size, size_t page_size) {

	memset(&v, 0, sizeof(v));
	v.kvm_fd = v.vm_fd = v.vcpu_fd = -1;
	v.mem_start = (uint8_t*)MAP_FAILED;
	v.run = (struct kvm_run*)MAP_FAILED;
	v.run_mmap_size = 0;
	v.mem_size = mem_size;
    v.page_size = page_size;

	v.kvm_fd = open("/dev/kvm", O_RDWR);
	if (v.kvm_fd < 0) return 0x10;

    int api = ioctl(v.kvm_fd, KVM_GET_API_VERSION, 0);
    if (api != KVM_API_VERSION) return 0x11;

	v.vm_fd = ioctl(v.kvm_fd, KVM_CREATE_VM, 0);
	if (v.vm_fd < 0) return 0x12;

	v.mem_start = (uint8_t*)mmap(0, mem_size, PROT_READ | PROT_WRITE,
		   MAP_SHARED | MAP_ANONYMOUS, -1, 0);
	if (v.mem_start == MAP_FAILED) return 0x13;

    struct kvm_userspace_memory_region region = {
        .slot = 0,
        .flags = KVM_MEM_LOG_DIRTY_PAGES,
        .guest_phys_addr = 0,
        .memory_size = v.mem_size, /* bytes */
        .userspace_addr = (uintptr_t)v.mem_start,
    };

    if (ioctl(v.vm_fd, KVM_SET_USER_MEMORY_REGION, &region) < 0) return 0x14;

	v.vcpu_fd = ioctl(v.vm_fd, KVM_CREATE_VCPU, 0);
    if (v.vcpu_fd < 0) return 0x15;

	v.run_mmap_size = ioctl(v.kvm_fd, KVM_GET_VCPU_MMAP_SIZE, 0);
    if (v.run_mmap_size <= 0) return 0x16;

	v.run = (struct kvm_run*)mmap(NULL, v.run_mmap_size, PROT_READ | PROT_WRITE,
			     MAP_SHARED, v.vcpu_fd, 0);
	if (v.run == MAP_FAILED) return 0x17;

	return 0;
}

void vm_destroy(struct vm &v) {
    if (v.run && v.run != MAP_FAILED) {
		munmap(v.run, (size_t)v.run_mmap_size);
		v.run = (struct kvm_run*)MAP_FAILED;
	}

	if(v.mem_start && v.mem_start != MAP_FAILED) {
		munmap(v.mem_start, v.mem_size);
		v.mem_start = (uint8_t*)MAP_FAILED;
	}

	if (v.vcpu_fd >= 0) {
		close(v.vcpu_fd);
		v.vcpu_fd = -1;
	}
}

static void setup_segments_64(struct kvm_sregs &sregs) {

	struct kvm_segment code = {
		.base = 0,
		.limit = ~0U,
        .selector = 0x8,
        .type = 11,     // Code: execute, read, accessed
		.present = 1,   // Prisutan ili učitan u memoriji
		.dpl = 0,       // Descriptor Privilage Level: 0 (0, 1, 2, 3)
		.db = 0,        // Default size - ima vrednost 0 u long modu
		.s = 1,         // Code/data tip segmenta
		.l = 1,         // Long mode - 1
		.g = 1,         // 4KB granularnost
        .avl = 0,
        .unusable = 0,
        .padding = 0,
	};
	struct kvm_segment data = code;
	data.type = 3; // Data: read, write, accessed
	data.l = 0;
	data.selector = 0x10; // Data segment selector

	sregs.cs = code;
	sregs.ds = sregs.es = sregs.fs = sregs.gs = sregs.ss = data;
}

int setup_long_mode(struct vm &v) {

    if (ioctl(v.vcpu_fd, KVM_GET_SREGS, &v.sregs) != 0)
		return 0x20;

    const static uint64_t MEM_END = v.mem_size;

	uint64_t pml4_addr = MEM_END - PML4_OFF;
	uint64_t *pml4 = (uint64_t *)(v.mem_start + pml4_addr);

	uint64_t pdpt_addr = MEM_END - PDP_OFF;
	uint64_t *pdpt = (uint64_t *)(v.mem_start + pdpt_addr);

	uint64_t pd_addr = MEM_END - PD_OFF;
	uint64_t *pd = (uint64_t *)(v.mem_start + pd_addr);

	pml4[0] = PDE64_PRESENT | PDE64_RW | PDE64_USER | pdpt_addr;
	pdpt[0] = PDE64_PRESENT | PDE64_RW | PDE64_USER | pd_addr;

    if (v.page_size == 0x200000) {
        // page size is 2 MB
        // pd[0] = PDE64_PRESENT | PDE64_RW | PDE64_USER | PDE64_PS;
        uint8_t n_pages = v.mem_size >> 21;
        for (size_t ix = 0; ix < n_pages; ++ix) {
            uint64_t page = (ix << 21);
            pd[ix] = page | PDE64_PRESENT | PDE64_RW | PDE64_USER | PDE64_PS;
        }
    } else if (v.page_size == 0x1000) {
        // page size is 4 KB
        uint8_t n_pde = v.mem_size >> 21;
        for (uint64_t ix = 0; ix < n_pde; ++ix) {
            const uint16_t n_pages = 512;

            uint64_t pt_addr = MEM_END - PT_OFF + (ix * 0x1000);
            uint64_t *pt = (uint64_t *)(v.mem_start + pt_addr);

            pd[ix] = pt_addr | PDE64_PRESENT | PDE64_RW | PDE64_USER;
            for (uint64_t jx = 0; jx < n_pages; ++jx) {
                uint64_t page = (ix<<21) | (jx<<12);
                pt[jx] = page | PDE64_PRESENT | PDE64_RW | PDE64_USER;
            }

        }
    } else return 0x21;

    v.sregs.cr3  = pml4_addr; 
	v.sregs.cr4  = CR4_PAE; // "Physical Address Extension" mora biti 1 za long mode.
	v.sregs.cr0  = CR0_PE | CR0_PG; // Postavljanje "Protected Mode" i "Paging" 
	v.sregs.efer = EFER_LME | EFER_LMA; // Postavljanje  "Long Mode Active" i "Long Mode Enable"

    setup_segments_64(v.sregs);

    if (ioctl(v.vcpu_fd, KVM_SET_SREGS, &v.sregs) != 0) {
        return 0x22;
    }

    return 0;
	
}

int load_guest_image(struct vm &v, const char* path) {
    FILE *f = fopen(path, "rb");
	if (!f)
		return 0x30;

	if (fseek(f, 0, SEEK_END) < 0) {
		fclose(f);
		return 0x31;
    }

	long fsz = ftell(f);
	if (fsz < 0) {
		fclose(f);
		return 0x32;
	}
	rewind(f);

	if((uint64_t)fsz > v.mem_size - GUEST_START_ADDR) {
		fclose(f);
		return 0x33;
	}

	if (fread(v.mem_start + GUEST_START_ADDR, 1, (size_t)fsz, f) != (size_t)fsz) {
		fclose(f);
		return 0x34;
	}
	fclose(f);

	return 0;
}

int set_context(struct vm &v) {
    struct kvm_regs regs;
    memset(&regs, 0, sizeof(regs));

    regs.rip = GUEST_START_ADDR; 
	regs.rsp = v.mem_size - STACK_START_OFF; // SP raste nadole
    regs.rflags = 0x2;

	if (ioctl(v.vcpu_fd, KVM_SET_REGS, &regs) < 0) {
		return 0x40;
	}
    return 0;
}