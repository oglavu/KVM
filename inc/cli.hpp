#pragma once

#include <vector>
#include <string>

struct vm_args_t {
    std::string image;
    int cpus;
    size_t memory_sz;
    size_t page_sz;
};

struct args_t {
    std::vector<vm_args_t> vms;
    std::vector<std::string> files;
};

void print_help();

int read_args(int argc, char *argv[], args_t &myArgs);