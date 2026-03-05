
#include "cli.hpp"

#include <getopt.h>


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
