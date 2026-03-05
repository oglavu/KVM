#pragma once

#include <vector>
#include <string>
#include <unistd.h>

typedef struct {
    size_t memory_sz, page_sz;
    std::vector<std::string> guest_path;
    std::vector<std::string> file_path;
} args_t;

int read_args(int argc, char *argv[], args_t &myArgs);