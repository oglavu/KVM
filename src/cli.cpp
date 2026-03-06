#include "cli.hpp"

#include <getopt.h>
#include <unistd.h>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

// Error codes
// (1) invalid args number / missing required config
// (2) invalid format (bad --vm string, unknown key, missing '=')
// (3) trying to set the arg twice (duplicate key within one --vm, or --file twice)
// (4) invalid arg value (bad mem/page/cpus, missing file, not executable, etc.)

static bool is_option(const char* s) {
    return s && s[0] == '-';
}

static bool split_pair(const std::string& s, std::string& k, std::string& v) {
    const auto pos = s.find('=');
    if (pos == std::string::npos) return false;
    k = s.substr(0, pos);
    v = s.substr(pos + 1);
    return !(k.empty() || v.empty());
}

static std::vector<std::string> split(const std::string& s, char delim) {
    std::vector<std::string> out;
    std::string cur;
    for (char c : s) {
        if (c == delim) {
            out.push_back(cur);
            cur.clear();
        } else {
            cur.push_back(c);
        }
    }
    out.push_back(cur);
    return out;
}

static bool parse_int(const std::string& s, int& out) {
    char* end = nullptr;
    long v = std::strtol(s.c_str(), &end, 10);
    if (!end || *end != '\0') return false;
    out = static_cast<int>(v);
    return true;
}

static int parse_vm(const char* vm_c, vm_args_t& vm_args) {
    bool image_set = false, 
        cpus_set = false, 
        mem_set = false, 
        page_set = false;

    const std::string vm(vm_c ? vm_c : "");
    if (vm.empty()) return 2;

    for (const auto& token : split(vm, ',')) {
        if (token.empty()) return 2;

        std::string key, val;
        if (!split_pair(token, key, val)) return 2;

        if (key == "image") {
            if (image_set) return 3;
            if (access(val.c_str(), X_OK) != 0) return 4;
            vm_args.image = val;
            image_set = true;
        } else if (key == "cpus") {
            if (cpus_set) return 3;
            int n = 0;
            if (!parse_int(val, n)) return 4;
            if (n <= 0 || n > 256) return 4;    // TODO: Adjust max cpus based on host capabilities?
            vm_args.cpus = n;
            cpus_set = true;
        } else if (key == "mem") {
            if (mem_set) return 3;
            int m = 0;
            if (!parse_int(val, m)) return 4;
            if (m != 2 && m != 4 && m != 8) return 4;
            vm_args.memory_sz = static_cast<size_t>(m) << 20; // MiB
            mem_set = true;
        } else if (key == "page") {
            if (page_set) return 3;
            int p = 0;
            if (!parse_int(val, p)) return 4;

            if (p == 2)       vm_args.page_sz = static_cast<size_t>(p) << 20;
            else if (p == 4)  vm_args.page_sz = static_cast<size_t>(p) << 10;
            else return 4;

            page_set = true;
        } else {
            return 2; // unknown
        }
    }

    if (!image_set || !cpus_set || !mem_set || !page_set) return 1;

    return 0;
}

static int collect_files(int argc, char* argv[], int& idx, std::vector<std::string>& out) {
    int count = 0;
    while (idx < argc && !is_option(argv[idx])) {
        if (access(argv[idx], F_OK) != 0) return 4;
        out.emplace_back(argv[idx]);
        ++count;
        ++idx;
    }
    if (count == 0) return 1; // --file provided but no filenames
    return 0;
}

int read_args(int argc, char* argv[], args_t &myArgs) {
    if (argc < 3) return 1;

    bool file_set = false;

    static option longopts[] = {
        {"vm",      required_argument,  nullptr, 'v'},
        {"file",    no_argument,        nullptr, 'f'}, // parsed manually
        {nullptr,   0,                  nullptr, 0}
    };

    const char* optstring = "+f";

    opterr = 0;
    optind = 1;

    int c;
    int option_index = 0;
    while (-1 != (c = getopt_long(argc, argv, optstring, longopts, &option_index))) {

        if (c == 'v') { // --vm
            vm_args_t vm{};
            int e = parse_vm(optarg, vm);
            if (e != 0) return e;
            myArgs.vms.push_back(std::move(vm));
            continue;
        }

        if (c == 'f') { // -f / --file
            if (file_set) return 3;
            file_set = true;

            int idx = optind;
            int e = collect_files(argc, argv, idx, myArgs.files);
            if (e != 0) return e;
            optind = idx;
            continue;
        }

        return 2;
    }

    // If there are stray args left
    if (optind < argc) return 2;

    if (myArgs.vms.empty()) return 1;

    return 0;
}