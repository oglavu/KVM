#include <stdio.h>
#include <getopt.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <linux/kvm.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <sys/sendfile.h>
#include <fcntl.h>
#include <unistd.h>
#include <vector>
#include <string>
#include <filesystem>
#include <iostream>
#include <semaphore.h>

#include "syscall.h"

#define GREEN_PREFIX "\x1b[32m"
#define RED_PREFIX "\x1b[31m"
#define DARK_GREEN "\x1b[33m"
#define NORMAL_PREFIX "\x1b[0m"

#define LOG(src, txt, color) { \
    printf("%s%s%s %s\n", color, src, NORMAL_PREFIX, txt); \
}

#define PML4_OFF 0x1000
#define PDP_OFF  0x2000
#define PD_OFF   0x3000
#define PT_OFF   0x7000
#define GUEST_START_ADDR    0x0000
#define STACK_START_OFF     0x7000

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

static const uint16_t FIO_PORT = 0x278;
static const uint16_t CIO_PORT = 0xE9;

sem_t* mux;
sem_t* rwmux;

typedef struct {
    size_t memory_sz, page_sz;
    std::vector<std::string> guest_path;
    std::vector<std::string> file_path;
} args_t;

const std::string shared_sem = "/kvm_sem";
const std::string rw_sem = "/kvm_rw_sem";
const std::string drive = "drive";
const std::string shared_prtt = "drive/shared/";
const std::string proc_prtt = "drive/proc%d/";

static int collect_nonopts(
    int argc, char *argv[], int &idx,
    std::vector<std::string> &out, int access_mode
) {
    while (idx < argc && argv[idx] && argv[idx][0] != '-') {
        if (access(argv[idx], access_mode) != 0)
            return 4;
        out.emplace_back(argv[idx]);
        ++idx;
    }
    return 0;
}

int read_args(int argc, char *argv[], args_t &myArgs) {
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

    bool mem_set = false, 
        page_set = false, 
        guest_set = false, 
        file_set = false;

    static option longopts[] = {
        {"memory",  required_argument,  nullptr,    'm'},
        {"page",    required_argument,  nullptr,    'p'},
        {"guest",   required_argument,  nullptr,    'g'},
        {"file",    required_argument,  nullptr,    'f'},
        {nullptr,   0,                  nullptr,    0}
    };

    const char *optstring = "+m:p:g:f:";

    opterr = 0;
    optind = 1;

    int c;
    while (-1 != (c = getopt_long(argc, argv, optstring, longopts, nullptr))){

        switch (c) {
        case 'm': {
            if (mem_set)
                return 3;
            int val = std::atoi(optarg);
            if (val != 2 && val != 4 && val != 8)
                return 4;
            myArgs.memory_sz = static_cast<size_t>(val) << 20;
            mem_set = true;
            break;
        }
        case 'p': {
            if (page_set)
                return 3;
            int val = std::atoi(optarg);

            if (val == 2)
                myArgs.page_sz = static_cast<size_t>(val) << 20;
            else if (val == 4)
                myArgs.page_sz = static_cast<size_t>(val) << 10;
            else
                return 4;

            page_set = true;
            break;
        }
        case 'g': {
            if (guest_set)
                return 3;

            if (access(optarg, X_OK) != 0)
                return 4;
            myArgs.guest_path.emplace_back(optarg);

            int idx = optind;
            int e = collect_nonopts(argc, argv, idx, myArgs.guest_path, X_OK);
            if (e != 0) {
                return e; // only 4
            }
            optind = idx;
            guest_set = true;
            break;
        }
        case 'f': {
            if (file_set)
                return 3;

            if (access(optarg, F_OK) != 0)
                return 4;
            myArgs.file_path.emplace_back(optarg);

            int idx = optind;
            int e = collect_nonopts(argc, argv, idx, myArgs.file_path, F_OK);
            if (e != 0) {
                return e; // only 4
            }
            optind = idx;
            file_set = true;
            break;
        }
        case '?':
        default:
            return 2; // unknown option or missing required_argument
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

	if (v.vm_fd >= 0) {
		close(v.vm_fd);
		v.vm_fd = -1;
	}

	if (v.kvm_fd >= 0) {
		close(v.kvm_fd);
		v.kvm_fd = -1;
	}
}

static void setup_segments_64(struct kvm_sregs* sregs) {

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

	sregs->cs = code;
	sregs->ds = sregs->es = sregs->fs = sregs->gs = sregs->ss = data;
}

int setup_long_mode(struct vm &v, struct kvm_sregs* sregs) {

    if (ioctl(v.vcpu_fd, KVM_GET_SREGS, sregs) != 0)
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

    sregs->cr3  = pml4_addr; 
	sregs->cr4  = CR4_PAE; // "Physical Address Extension" mora biti 1 za long mode.
	sregs->cr0  = CR0_PE | CR0_PG; // Postavljanje "Protected Mode" i "Paging" 
	sregs->efer = EFER_LME | EFER_LMA; // Postavljanje  "Long Mode Active" i "Long Mode Enable"

    setup_segments_64(sregs);

    if (ioctl(v.vcpu_fd, KVM_SET_SREGS, sregs) != 0) {
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

static int vm_id;

typedef struct {
    FILE* dsc;
    std::string filename;
    char mode[5];
    bool is_shared;
} ftable_e;

std::vector<ftable_e> ftable;

inline static std::string& proc_path(std::string& path, int id) {
    return path.replace(path.find("%d"), 2, std::to_string(id));
}

static int filesys_setup(args_t& myArgs) {

    sem_unlink(shared_sem.c_str());
    sem_unlink(rw_sem.c_str());
    mux = sem_open(shared_sem.c_str(), O_CREAT, 0666, 1);
    if (mux == SEM_FAILED) {
        LOG("[HOST]", "Couldn't open mutex", RED_PREFIX);
        return -1;
    }
    rwmux = sem_open(rw_sem.c_str(), O_CREAT, 0666, 1);
    if (rwmux == SEM_FAILED) {
        LOG("[HOST]", "Couldn't open mutex", RED_PREFIX);
        return -1;
    }

    // create folders
    std::filesystem::remove_all(drive);                 // remove mounted drive
    std::filesystem::create_directories(shared_prtt);   // create shared dir
    for (size_t i=0; i<myArgs.guest_path.size(); ++i) {
        std::string proc_dir(proc_prtt);
        proc_path(proc_dir, i);
        std::filesystem::create_directories(proc_dir);
    }

    // create/copy files
    for (size_t i=0; i<myArgs.file_path.size(); ++i) {
        std::string vfilepath(shared_prtt);
        std::string rfilepath(myArgs.file_path[i]);
        vfilepath.append(rfilepath);

        if (0 == access(rfilepath.c_str(), R_OK)) {
            std::filesystem::copy(rfilepath, vfilepath, std::filesystem::copy_options::recursive);
        } else {
            FILE* fd = fopen(vfilepath.c_str(), "w+");
            fclose(fd);
        }
    }

    ftable_e entry = {
        .dsc = 0,
        .filename = "",
        .mode = "w+",
        .is_shared = false,
    };
    ftable.push_back(entry);
    return 0;
}

static int fopen_routine(const char* filename, const char* mode) {
    std::string shared_filename(shared_prtt);
    shared_filename.append(filename);
    std::string proc_filename(proc_prtt);
    proc_path(proc_filename, vm_id).append(filename);
    
    ftable_e entry;
    strcpy(entry.mode, mode);
    entry.filename = {filename};
    if (access(shared_filename.c_str(), F_OK) == 0 ) {
        if (mode[0] == 'w') {
            std::filesystem::copy(shared_filename, proc_filename, std::filesystem::copy_options::recursive);
            filename = proc_filename.c_str();
            entry.is_shared = false;
        } else {
            filename = shared_filename.c_str();
            entry.is_shared = true;
        }
        
    } else {
        filename = proc_filename.c_str();
        entry.is_shared = false;
    }

    int ret = 0;
    sem_wait(rwmux);
    try {
        entry.dsc = fopen(filename, mode);
        ftable.push_back(entry);
        ret = ftable.size()-1;
    } catch (std::exception const&) { }
    sem_post(rwmux);
    return ret;
}

static int fclose_routine(int vfd) {
    int ret = -1;
    sem_wait(rwmux);
    try {
        ftable_e fd = ftable[vfd];
        ret = fclose(fd.dsc);
    } catch (std::exception const&) { }
    sem_post(rwmux);
    return ret;
}

static int fputc_routine(int c, int vfd) {
    int ret = -1;
    sem_wait(rwmux);
    try {
        ftable_e& fd = ftable[vfd];
        if (fd.is_shared) {
            std::string shared_filename(shared_prtt);
            shared_filename.append(ftable[vfd].filename);
            std::string proc_filename(proc_prtt);
            proc_path(proc_filename, vm_id).append(ftable[vfd].filename);

            std::filesystem::copy(shared_filename, proc_filename);
            
            long cursor = ftell(fd.dsc);
            fclose(fd.dsc);

            fd.dsc = fopen(proc_filename.c_str(), fd.mode);
            fd.is_shared = false;
            fseek(fd.dsc, cursor, SEEK_SET);
        }
        ret = fputc(c, fd.dsc);
    } catch(std::exception const&) { }
    sem_post(rwmux);
    return ret;
}

static int fgetc_routine(int vfd) {
    int ret = -1;
    sem_wait(rwmux);
    try {
        ftable_e fd = ftable[vfd];
        ret = fgetc(fd.dsc);
    } catch(std::exception const&) { }
    sem_post(rwmux);
    return ret;
}

static long ftell_routine(int vfd) {
    long ret = -1;
    sem_wait(rwmux);
    try {
        ftable_e fd = ftable[vfd];
        ret = ftell(fd.dsc);
    } catch(std::exception const&) { }
    sem_post(rwmux);
    return ret;
}

static int fseek_routine(int vfd, long offset) {
    int ret = -1;
    try {
        ftable_e fd = ftable[vfd];
        ret = fseek(fd.dsc, offset, SEEK_SET);
    } catch(std::exception const&) { }
    return ret;
}

static void system_call_routine(struct vm &v) {
    
    struct kvm_regs regs;
    if (ioctl(v.vcpu_fd, KVM_GET_REGS, &regs) < 0) {
        LOG("[HOST]", "Host couldn't get VM regs in syscall.", RED_PREFIX);
        return;
    }

    long op_code = regs.rax;
    long arg1r = regs.rbx,
         arg2r = regs.rcx;
    char *filename, *mode;
    int vfd, c;
    //printf("op_code: %ld\targ1r: %ld\targ2r: %ld\n", op_code, arg1r, arg2r);
    switch(op_code) {
        case SYS_FOPEN: // fopen
            filename = (char*)(arg1r+v.mem_start);
            mode = (char*)(arg2r+v.mem_start);
            regs.rax = (uint64_t)fopen_routine(filename, mode);
            break;
        case SYS_FCLOSE: // fclose
            vfd = (int)arg1r;
            regs.rax = (uint64_t)fclose_routine(vfd);
            break;
        case SYS_FPUTC: // fputc
            c = (int)arg1r;
            vfd = (int)arg2r;
            regs.rax = (uint64_t)fputc_routine(c, vfd);
            break;
        case SYS_FGETC: // fgetc
            vfd = (int)arg1r;
            regs.rax = (uint64_t)fgetc_routine(vfd);
            break;
        case SYS_FTELL:
            vfd = (int)arg1r;
            regs.rax = (uint64_t)ftell_routine(vfd);
            break;
        case SYS_FSEEK:
            vfd = (int)arg1r;
            regs.rax = (uint64_t)fseek_routine(vfd, arg2r);
            break;
        default: 
            LOG("[VM]", "Unknown syscall.", RED_PREFIX);
            break;
    }

    if (ioctl(v.vcpu_fd, KVM_SET_REGS, &regs) < 0) {
        LOG("[HOST]", "Host couldn't set VM regs in syscall.", RED_PREFIX);
        return;
    }
}


int run(struct vm &v) {
    char src[20];
    char vm_src[20];
    sprintf(src, "[GUEST-%d]", vm_id);
    sprintf(vm_src, "[VM-%d]", vm_id);
    struct kvm_regs regs;
    int stop = 0;
    int msg_recv = 0;
    int status = 0;
    static const int N = 100;
    char buf[N + 1];
    int cur = 0;
    std::string bufi;

    auto flush = [&](void) -> void {
        sem_wait(mux);
        if (cur > 0) {
            buf[cur] = '\0';
            LOG(src, buf, NORMAL_PREFIX);
            cur = 0;
        }
        sem_post(mux);
    };
    
	while(stop == 0) {
		int ret = ioctl(v.vcpu_fd, KVM_RUN, 0);
		if (ret != 0) {
            status = 0x50;
            break;
        }

		switch (v.run->exit_reason) {
			case KVM_EXIT_IO:
                msg_recv = 1;
                if (v.run->io.direction == KVM_EXIT_IO_IN && 
                        v.run->io.port == CIO_PORT) {
                    flush();
                    char *p = (char *)v.run;
                    char *c = (p + v.run->io.data_offset);
                    if (bufi.size() == 0) {
                        sem_wait(mux);
                        printf("%s%s%s ", DARK_GREEN, vm_src, NORMAL_PREFIX);
                        std::getline(std::cin, bufi);
                        sem_post(mux);
                        bufi.append("\n");
                    }
                    *c = bufi[0];
                    bufi.replace(0, 1, "");
                } else if (v.run->io.direction == KVM_EXIT_IO_OUT && 
                        v.run->io.port == CIO_PORT) {
					char *p = (char *)v.run;
                    char  c = *(p + v.run->io.data_offset);
                    if (cur < N && c != '\n') 
                        buf[cur++] = c;
                    if (c == '\n' || cur == N) {
                        flush();
                    }
                } else if (v.run->io.direction == KVM_EXIT_IO_OUT && 
                        v.run->io.port == FIO_PORT) {
                    system_call_routine(v);
				}
                
				continue;
			case KVM_EXIT_HLT:
                status = 0; stop = 1; break;
			case KVM_EXIT_SHUTDOWN:
                ioctl(v.vcpu_fd, KVM_GET_REGS, &regs);
                printf("RIP=0x%.16llx RSP=0x%.16llx RAX=0x%.16llx\n",
                    regs.rip, regs.rsp, regs.rax);
                status = 0x51; stop = 1; break;
			default:
                status = 0x52; stop = 1; break;
    	}
  	}

    // empty the buf before exit
    flush();

    if (!msg_recv && !status) {
        status = 0x53;
    }

    return status;
}

int child_main(args_t& myArgs) {
    int status;
    char src[20];
    sprintf(src, "[VM-%d]", vm_id);

    // init vm
    struct vm v;
    status = vm_init(v, myArgs.memory_sz, myArgs.page_sz);
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
    status = setup_long_mode(v, &sregs);
    switch (status) {
        case 0x00: LOG(src, "Long mode setup successfully.", GREEN_PREFIX); break;
        case 0x20: LOG(src, "KVM_GET_SREGS", RED_PREFIX) break;
        case 0x21: LOG(src, "Invalid mem_size", RED_PREFIX); break;
        case 0x22: LOG(src, "KVM_SET_SREGS", RED_PREFIX); break;
        default:   LOG(src, "Unexpected error.", RED_PREFIX); break;
    }

    if (status != 0)
        goto cleanup;

    status = load_guest_image(v, myArgs.guest_path[vm_id].c_str());
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
    
    status = set_context(v);
    switch (status) {
        case 0x00: LOG(src, "Regs set successfully.", GREEN_PREFIX); break;
        case 0x40: LOG(src, "KVM_SET_REGS", RED_PREFIX); break;
        default:   LOG(src, "Unexpected error.", RED_PREFIX); break;
    }

    if (status != 0)
        goto cleanup;

    status = run(v);
    switch(status) {
        case 0x00: LOG(src, "Graceful exit - HLT reached.", GREEN_PREFIX); break;
        case 0x50: LOG(src, "KVM_RUN", RED_PREFIX); break;
        case 0x51: LOG(src, "Hard exit - forcefull shutdown.", RED_PREFIX); break;
        case 0x52: LOG(src, "Unexpected exit cause.", RED_PREFIX); break;
        case 0x53: LOG(src, "Received no message.", RED_PREFIX); break;
        default:   LOG(src, "Unexpected error.", RED_PREFIX); break;
    }

cleanup:
    vm_destroy(v);

    return status;

}

int main(int argc, char* argv[]) {

    // read args
    char src[50];
    args_t myArgs;
    int status = read_args(argc, argv, myArgs);
    switch (status) {
        case 0: LOG("[HOST]", "Args read successfully.", GREEN_PREFIX); break;
        case 1: LOG("[HOST]", "Invalid arg format. Try:\nexe (--memory|-m) <m> (--page|-p) <p> (--guest|-g) <g> [--file|-f <f>]\nwhere <m> is either 2, 4 or 8 and <p> either 2 or 4, and <g> an executable file", RED_PREFIX); break;
        case 2: LOG("[HOST]", "Invalid number of args. Try:\nexe (--memory|-m) <m> (--page|-p) <p> (--guest|-g) <g> [--file|-f <f>]\nwhere <m> is either 2, 4 or 8 and <p> either 2 or 4, and <g> an executable file", RED_PREFIX); break;
        case 3: LOG("[HOST]", "Arg set twice", RED_PREFIX); break;
        case 4: LOG("[HOST]", "Invalid arg value.", RED_PREFIX); break;
        default:LOG("[HOST]", "Unexpected read args status.", RED_PREFIX); break;
    };

    if (status != 0) 
        return status;

    // printf("m: %d, p: %d", myArgs.memory_sz, myArgs.page_sz);
    // for (std::string& e : myArgs.file_path) {
    //     printf("f: %s\n", e.c_str());
    // }
    // for (std::string& e : myArgs.guest_path) {
    //     printf("g: %s\n", e.c_str());
    // }

    status = filesys_setup(myArgs);
    switch(status) {
        case 0: LOG("[HOST]", "Disk partitioned successfully.", GREEN_PREFIX); break;
        default:LOG("[HOST]", "Disk partitioned failed", RED_PREFIX) break;
    }

    if (status != 0)
        return status;

    for (uint8_t p_ix = 0; p_ix < myArgs.guest_path.size(); ++p_ix) {
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
            vm_id = p_ix;

            return child_main(myArgs);
        } else {
            sprintf(src, "VM-%d started with PID %d", p_ix, pid);
            LOG("[HOST]", src, GREEN_PREFIX);
        }
    }

    int p_status;
    for (size_t ix = 0; ix < myArgs.guest_path.size(); ++ix) {
        pid_t pid = wait(&p_status);
        
        sprintf(src, "VM-%d returned with status: %d", pid, p_status);
        LOG("[HOST]", src, (p_status == 0 ? GREEN_PREFIX : RED_PREFIX));
        status |= p_status;
    }
    sem_unlink(shared_sem.c_str());

    return status;
}
