#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <linux/kvm.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <unistd.h>

#define GREEN_PREFIX "\x1b[32m"
#define RED_PREFIX "\x1b[31m"
#define NORMAL_PREFIX "\x1b[0m"

#define LOG(src, txt, color) { \
    printf("%s%s%s %s\n", color, src, NORMAL_PREFIX, txt); \
}

#define PML4_ADDR 0x1FF000
#define PDP_ADDR  0x1FE000
#define PD_ADDR   0x1FD000
#define PT_ADDR   0x1F9000
#define GUEST_START_ADDR    0x000000
#define STACK_START_ADDR     0x1F9000

// PDE bitovi
#define PDE64_PRESENT (1ULL << 0)
#define PDE64_RW (1ULL << 1)
#define PDE64_USER (1ULL << 2)
#define PDE64_PS (1ULL << 7)

// CR4 i CR0
#define CR0_PE (1ULL << 0)
#define CR0_PG (1ULL << 31)
#define CR4_PAE (1ULL << 5)

#define EFER_LME (1ULL << 8)
#define EFER_LMA (1ULL << 10)

#define MAX_VM 10

#define CIO_PORT 0xE9

typedef struct {
    size_t memory_sz, page_sz;
    uint8_t n_guests;
    char guest_path[MAX_VM][50];
} args_t;

int read_args(int argc, char* argv[], args_t* myArgs) {
    /* read args
       ./mini_hypervisor --memory 4 --page 2 --guest guest.img [guest2.img]
       error codes:
       (1) invalid args number. At least 7 args required.
       (2) invalid format of the supplied options. Every option requires an argument.
       (3) trying to set the arg twice. Every arg should be set with shorter (-) or longer (--) format.
       (4) invalid arg value. Mem must be 2, 4, or 8, Page 2 or 4 and guest must be an exe file.
    */

    if (argc < 7)
        return 1;

    struct {
        uint8_t mem, pg, guest;
    } is_set = {0};
    
    for (int i=1; i<argc; i += 2) {
        if (argv[i][0] != '-') {
            return 2;
        } 
        if (strcmp(argv[i], "-m") == 0 || strcmp(argv[i], "--memory") == 0) {
            if (is_set.mem) return 3;
            int val = atoi(argv[i+1]);
            if (val != 2 && val != 4 && val != 8) return 4;
            myArgs->memory_sz = val << 20; 
            is_set.mem = 1;
        } else if (strcmp(argv[i], "-p") == 0 || strcmp(argv[i], "--page") == 0) {
            if (is_set.pg) return 3;
            int val = atoi(argv[i+1]);
            if (val == 2)
                myArgs->page_sz = val << 20;
            else if (val == 4)
                myArgs->page_sz = val << 10;
            else 
                return 4;
            is_set.pg = 1;
        } else if (strcmp(argv[i], "-g") == 0 || strcmp(argv[i], "--guest") == 0) {
            if (is_set.guest) return 3;
            myArgs->n_guests = 0;
            for (int j = 0; j < MAX_VM; ++j) {


                if (i+j+1 >= argc || argv[i+j+1][0] == '-') break;
                if (access(argv[i+j+1], X_OK) != 0) return 4;
                
                myArgs->n_guests++;
                strcpy(myArgs->guest_path[j], argv[i+j+1]);
            }
            is_set.guest = 1;
            i += myArgs->n_guests-1;
            
        } 
    }
    return 0;
}


struct vm {
    int kvm_fd,
        vm_fd,
        vcpu_fd;
    uint8_t* mem_start;
    size_t mem_size;
    size_t page_size;
    struct kvm_run* run;
    int run_mmap_size;
};

int vm_init(struct vm *v, size_t mem_size, size_t page_size) {

	memset(v, 0, sizeof(*v));
	v->kvm_fd = v->vm_fd = v->vcpu_fd = -1;
	v->mem_start = MAP_FAILED;
	v->run = MAP_FAILED;
	v->run_mmap_size = 0;
	v->mem_size = mem_size;
    v->page_size = page_size;

	v->kvm_fd = open("/dev/kvm", O_RDWR);
	if (v->kvm_fd < 0) return 0x10;

    int api = ioctl(v->kvm_fd, KVM_GET_API_VERSION, 0);
    if (api != KVM_API_VERSION) return 0x11;

	v->vm_fd = ioctl(v->kvm_fd, KVM_CREATE_VM, 0);
	if (v->vm_fd < 0) return 0x12;

	v->mem_start = (uint8_t*)mmap(0, mem_size, PROT_READ | PROT_WRITE,
		   MAP_SHARED | MAP_ANONYMOUS, -1, 0);
	if (v->mem_start == MAP_FAILED) return 0x13;

    struct kvm_userspace_memory_region region = {
        .slot = 0,
        .guest_phys_addr = 0,
        .memory_size = v->mem_size, /* bytes */
        .userspace_addr = (uintptr_t)v->mem_start, 
    };

    if (ioctl(v->vm_fd, KVM_SET_USER_MEMORY_REGION, &region) < 0) return 0x14;

	v->vcpu_fd = ioctl(v->vm_fd, KVM_CREATE_VCPU, 0);
    if (v->vcpu_fd < 0) return 0x15;

	v->run_mmap_size = ioctl(v->kvm_fd, KVM_GET_VCPU_MMAP_SIZE, 0);
    if (v->run_mmap_size <= 0) return 0x16;

	v->run = mmap(NULL, v->run_mmap_size, PROT_READ | PROT_WRITE,
			     MAP_SHARED, v->vcpu_fd, 0);
	if (v->run == MAP_FAILED) return 0x17;

	return 0;
}

void vm_destroy(struct vm* v) {
    if (v->run && v->run != MAP_FAILED) {
		munmap(v->run, (size_t)v->run_mmap_size);
		v->run = MAP_FAILED;
	}

	if(v->mem_start && v->mem_start != MAP_FAILED) {
		munmap(v->mem_start, v->mem_size);
		v->mem_start = MAP_FAILED;
	}

	if (v->vcpu_fd >= 0) {
		close(v->vcpu_fd);
		v->vcpu_fd = -1;
	}

	if (v->vm_fd >= 0) {
		close(v->vm_fd);
		v->vm_fd = -1;
	}

	if (v->kvm_fd >= 0) {
		close(v->kvm_fd);
		v->kvm_fd = -1;
	}
}

static void setup_segments_64(struct kvm_sregs* sregs) {

    // .selector = 0x8,
	struct kvm_segment code = {
		.base = 0,
		.limit = ~0U,
		.present = 1, // Prisutan ili učitan u memoriji
		.type = 11, // Code: execute, read, accessed
		.dpl = 0, // Descriptor Privilage Level: 0 (0, 1, 2, 3)
		.db = 0, // Default size - ima vrednost 0 u long modu
		.s = 1, // Code/data tip segmenta
		.l = 1, // Long mode - 1
		.g = 1, // 4KB granularnost
        .unusable = 0,
        .selector = 0x8,
	};
	struct kvm_segment data = code;
	data.type = 3; // Data: read, write, accessed
	data.l = 0;
	data.selector = 0x10; // Data segment selector

	sregs->cs = code;
	sregs->ds = sregs->es = sregs->fs = sregs->gs = sregs->ss = data;
}

int setup_long_mode(struct vm* v, struct kvm_sregs* sregs) {

    if (ioctl(v->vcpu_fd, KVM_GET_SREGS, sregs) != 0)
		return 0x20;

	uint64_t pml4_addr = PML4_ADDR;
	uint64_t *pml4 = (void *)(v->mem_start + pml4_addr);

	uint64_t pdpt_addr = PDP_ADDR;
	uint64_t *pdpt = (void *)(v->mem_start + pdpt_addr);

	uint64_t pd_addr = PD_ADDR;
	uint64_t *pd = (void *)(v->mem_start + pd_addr);

	pml4[0] = PDE64_PRESENT | PDE64_RW | PDE64_USER | pdpt_addr;
	pdpt[0] = PDE64_PRESENT | PDE64_RW | PDE64_USER | pd_addr;

    if (v->page_size == 0x200000) {
        // page size is 2 MB
        // pd[0] = PDE64_PRESENT | PDE64_RW | PDE64_USER | PDE64_PS;
        uint8_t n_pages = v->mem_size >> 21;
        for (size_t ix = 0; ix < n_pages; ++ix) {
            uint64_t page = (ix << 21);
            pd[ix] = page | PDE64_PRESENT | PDE64_RW | PDE64_USER | PDE64_PS;
        }
    } else if (v->page_size == 0x1000) {
        // page size is 4 KB
        uint8_t n_pde = v->mem_size >> 21;
        for (uint64_t ix = 0; ix < n_pde; ++ix) {
            const uint16_t n_pages = 512;

            uint64_t pt_addr = PT_ADDR + (ix * 0x1000);
            uint64_t *pt = (void *)(v->mem_start + pt_addr);

            pd[ix] = pt_addr | PDE64_PRESENT | PDE64_RW | PDE64_USER;
            for (uint64_t jx = 0; jx < n_pages; ++jx) {
                uint64_t page = (ix<<21) | (jx<<12);
                pt[jx] = page | PDE64_PRESENT | PDE64_RW | PDE64_USER;
            }

        }
    } else return 0x21;

    sregs->cr3  = pml4_addr; 
	sregs->cr4  = CR4_PAE; // "Physical Address Extension" mora biti 1 za long mode.
	sregs->cr0  = CR0_PE | CR0_PG; // Postavljanje "Protected Mode" i "Paging" 
	sregs->efer = EFER_LME | EFER_LMA; // Postavljanje  "Long Mode Active" i "Long Mode Enable"

    setup_segments_64(sregs);

    if (ioctl(v->vcpu_fd, KVM_SET_SREGS, sregs) != 0) {
        return 0x22;
    }

    return 0;
	
}

int load_guest_image(struct vm* v, const char* path) {
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

	if((uint64_t)fsz > v->mem_size - GUEST_START_ADDR) {
		fclose(f);
		return 0x33;
	}

	if (fread(v->mem_start + GUEST_START_ADDR, 1, (size_t)fsz, f) != (size_t)fsz) {
		fclose(f);
		return 0x34;
	}
	fclose(f);

	return 0;
}

int set_context(struct vm* v) {
    struct kvm_regs regs;
    memset(&regs, 0, sizeof(regs));

    regs.rip = GUEST_START_ADDR; 
	regs.rsp = STACK_START_ADDR; // SP raste nadole
    regs.rflags = 0x2;

	if (ioctl(v->vcpu_fd, KVM_SET_REGS, &regs) < 0) {
		return 0x40;
	}
    return 0;
}

int run(struct vm* v, uint8_t p_ix) {
    char src[20];
    sprintf(src, "[GUEST-%d]", p_ix);
    struct kvm_regs regs;
    int stop = 0;
    int msg_recv = 0;
    int status = 0;
    static const int N = 100;
    char buf[N + 1];
    int cur = 0;
    
	while(stop == 0) {
		int ret = ioctl(v->vcpu_fd, KVM_RUN, 0);
		if (ret != 0) {
            status = 0x50;
            break;
        }

		switch (v->run->exit_reason) {
			case KVM_EXIT_IO:
            msg_recv = 1;
            if (v->run->io.direction == KVM_EXIT_IO_IN && 
                v->run->io.port == CIO_PORT) {
                char *p = (char *)v->run;
                *(p + v->run->io.data_offset) = getchar();
            } else if (v->run->io.direction == KVM_EXIT_IO_OUT && 
                    v->run->io.port == CIO_PORT) {
                char *p = (char *)v->run;
                char  c = *(p + v->run->io.data_offset);
                if (cur < N && c != '\n') 
                    buf[cur++] = c;
                if (c == '\n' || cur == N) {
                    buf[cur] = '\0';
                    LOG(src, buf, NORMAL_PREFIX);
                    cur = 0;
                }
            }
			case KVM_EXIT_HLT:
                status = 0; stop = 1; break;
			case KVM_EXIT_SHUTDOWN:
                ioctl(v->vcpu_fd, KVM_GET_REGS, &regs);
                printf("RIP=0x%.16llx RSP=0x%.16llx RAX=0x%.16llx\n",
                    regs.rip, regs.rsp, regs.rax);
                status = 0x51; stop = 1; break;
			default:
                status = 0x52; stop = 1; break;
    	}
  	}

    // empty the buf before exit
    if (cur > 0) {
        buf[cur] = '\0';
        LOG(src, buf, NORMAL_PREFIX);
        fflush(stdout); fflush(stderr);
    }

    if (!msg_recv && !status) {
        status = 0x53;
    }

    return status;
}

int child_main(args_t* myArgs, uint8_t p_ix) {
    int status;
    char src[20];
    sprintf(src, "[VM-%d]", p_ix);

    // init vm
    struct vm v;
    status = vm_init(&v, myArgs->memory_sz, myArgs->page_sz);
    switch (status) {
        case 0x00: LOG(src, "VM inited successfully.", GREEN_PREFIX); break;
        case 0x10: LOG(src, "Couldn't open /dev/kvm.", RED_PREFIX); break;
        case 0x11: LOG(src, "KVM API mismatch.", RED_PREFIX); break;
        case 0x12: LOG(src, "KVM_CREATE_VM", RED_PREFIX); break;
        case 0x13: LOG(src, "MMAP MEM", RED_PREFIX); break;
        case 0x14: LOG(src, "KVM_SET_USER_MEMORY_REGION", RED_PREFIX); break;
        case 0x15: LOG(src, "KVM_CREATE_VCPU", RED_PREFIX); break;
        case 0x16: LOG(src, "KVM_GET_VCPU_MMAP_SIZE", RED_PREFIX); break;
        case 0x17: LOG(src, "MMAP KVM_RUN", RED_PREFIX); break;
        default:   LOG(src, "Unexepected error.", RED_PREFIX); break;
    }

    if (status != 0)
        goto cleanup;

    // setup long mode & paging
    struct kvm_sregs sregs;
    status = setup_long_mode(&v, &sregs);
    switch (status) {
        case 0x00: LOG(src, "Long mode setup successfully.", GREEN_PREFIX); break;
        case 0x20: LOG(src, "KVM_GET_SREGS", RED_PREFIX) break;
        case 0x21: LOG(src, "Invalid mem_size", RED_PREFIX); break;
        case 0x22: LOG(src, "KVM_SET_SREGS", RED_PREFIX); break;
        default:   LOG(src, "Unexpected error.", RED_PREFIX); break;
    }

    if (status != 0)
        goto cleanup;

    status = load_guest_image(&v, myArgs->guest_path[p_ix]);
    switch (status) {
        case 0x00: LOG(src, "Guest Image loaded successfully.", GREEN_PREFIX); break;
        case 0x30: LOG(src, "Failed to open guest image.", RED_PREFIX); break;
        case 0x31: LOG(src, "Failed to reach the EOF.", RED_PREFIX); break;
        case 0x32: LOG(src, "Failed to get size of guest image.", RED_PREFIX); break;
        case 0x33: LOG(src, "Guest image is too large.", RED_PREFIX); break;
        case 0x34: LOG(src, "Failed to read guest image.", RED_PREFIX); break;
        default:   LOG(src, "Unexpected error.", RED_PREFIX); break;
    }

    if (status != 0)
        goto cleanup;
    
    status = set_context(&v);
    switch (status) {
        case 0x00: LOG(src, "Regs set successfully.", GREEN_PREFIX); break;
        case 0x40: LOG(src, "KVM_SET_REGS", RED_PREFIX); break;
        default:   LOG(src, "Unexpected error.", RED_PREFIX); break;
    }

    if (status != 0)
        goto cleanup;

    status = run(&v, p_ix);
    switch(status) {
        case 0x00: LOG(src, "Graceful exit - HLT reached.", GREEN_PREFIX); break;
        case 0x50: LOG(src, "KVM_RUN", RED_PREFIX); break;
        case 0x51: LOG(src, "Hard exit - forcefull shutdown.", RED_PREFIX); break;
        case 0x52: LOG(src, "Unexpected exit cause.", RED_PREFIX); break;
        case 0x53: LOG(src, "Received no message.", RED_PREFIX); break;
        default:   LOG(src, "Unexpected error.", RED_PREFIX); break;
    }

cleanup:
    vm_destroy(&v);

    return status;

}

int main(int argc, char* argv[]) {

    // read args
    char src[50];
    args_t myArgs = {0};
    int status = read_args(argc, argv, &myArgs);
    switch (status) {
        case 0: LOG("[HOST]", "Args read successfully.", GREEN_PREFIX); break;
        case 1: LOG("[HOST]", "Invalid arg format. Try:\nexe (--memory|-m) <m> (--page|-p) <p> (--guest|-g) <g>\nwhere <m> is either 2, 4 or 8 and <p> either 2 or 4, and <g> an executable file", RED_PREFIX); break;
        case 2: LOG("[HOST]", "Invalid number of args. Try:\nexe (--memory|-m) <m> (--page|-p) <p> (--guest|-g) <g>\nwhere <m> is either 2, 4 or 8 and <p> either 2 or 4, and <g> an executable file", RED_PREFIX); break;
        case 3: LOG("[HOST]", "Arg set twice", RED_PREFIX); break;
        case 4: LOG("[HOST]", "Invalid arg value.", RED_PREFIX); break;
        default:LOG("[HOST]", "Unexpected read args status.", RED_PREFIX); break;
    };

    if (status != 0) 
        return status;

    for (uint8_t p_ix = 0; p_ix < myArgs.n_guests; ++p_ix) {
        pid_t pid = fork();

        if (pid < 0) {
            // failed
            sprintf(src, "[HOST-%d]", p_ix);
            LOG(src, "Process not started", RED_PREFIX);
            return -1;
        } else if (pid == 0) {
            // child process
            sprintf(src, "[VM-%d]", p_ix);
            LOG(src, "Starting process", GREEN_PREFIX);

            return child_main(&myArgs, p_ix);
        } else {
            sprintf(src, "VM-%d started with PID %d", p_ix, pid);
            LOG("[HOST]", src, GREEN_PREFIX);
        }
    }

    int p_status;
    for (int ix = 0; ix < myArgs.n_guests; ++ix) {
        pid_t pid = wait(&p_status);
        
        sprintf(src, "VM-%d returned with status: %d", pid, p_status);
        LOG("[HOST]", src, (p_status == 0 ? GREEN_PREFIX : RED_PREFIX));
        status |= p_status;
    }

    return status;
}
