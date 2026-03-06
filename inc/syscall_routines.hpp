#pragma once

#include "kvm.hpp"

#include <vector>
#include <string>

typedef struct {
    FILE* dsc;
    std::string filename;
    char mode[5];
    bool is_shared;
} ftable_e;

static const uint16_t FIO_PORT = 0x278;
static const uint16_t CIO_PORT = 0xE9;

const std::string shared_sem = "/kvm_sem";
const std::string rw_sem = "/kvm_rw_sem";
const std::string drive = "drive";
const std::string shared_prtt = "drive/shared/";
const std::string proc_prtt = "drive/proc%d/";

extern int vm_id;

int filesys_setup(
    std::vector<std::string>& guest_paths, 
    std::vector<std::string>& file_paths
);

int run_vcpu(struct vm &v, int cpu_id);