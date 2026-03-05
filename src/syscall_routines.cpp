
#include "syscall_routines.hpp"

#include "log.hpp"
#include "syscall.h"

#include <filesystem>
#include <iostream>

#include <semaphore.h>
#include <fcntl.h>
#include <unistd.h>

#include <linux/kvm.h>
#include <sys/ioctl.h>

int vm_id;

static sem_t* mux;
static sem_t* rwmux;

static std::vector<ftable_e> ftable;

inline static std::string& proc_path(std::string& path, int id) {
    return path.replace(path.find("%d"), 2, std::to_string(id));
}

int filesys_setup(
    std::vector<std::string>& guest_paths, 
    std::vector<std::string>& file_paths
) {

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
    for (size_t i=0; i<guest_paths.size(); ++i) {
        std::string proc_dir(proc_prtt);
        proc_path(proc_dir, i);
        std::filesystem::create_directories(proc_dir);
    }

    // create/copy files
    for (size_t i=0; i<file_paths.size(); ++i) {
        std::string vfilepath(shared_prtt);
        std::string rfilepath(file_paths[i]);
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

void system_call_routine(struct vm &v) {
    
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
            perror("KVM_RUN failed");
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