
#include <sys/wait.h>
#include <semaphore.h>

#include "syscall_routines.hpp"
#include "cli.hpp"
#include "kvm.hpp"
#include "syscall.h"
#include "log.hpp"


int child_main(args_t& myArgs) {
    int status;
    char src[20];
    sprintf(src, "[VM-%d]", vm_id);

    // init vm
    vm v;
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
    status = setup_long_mode(v);
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

    status = filesys_setup(myArgs.guest_path, myArgs.file_path);
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
