#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <linux/kvm.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <unistd.h>

#define GREEN_PREFIX "\x1b[32m"
#define RED_PREFIX "\x1b[31m"
#define NORMAL_PREFIX "\x1b[0m"

typedef struct {
    uint8_t memory_sz, page_sz;
    char guest_path[50];
} args_t;

int read_args(int argc, char* argv[], args_t* myArgs) {
    /* read args
       ./mini_hypervisor --memory 4 --page 2 --guest guest.img
       error codes:
       (1) invalid args number. Exactly 7 args required.
       (2) invalid format of the supplied options. Every option requires an argument.
       (3) trying to set the arg twice. Every arg should be set with shorter (-) or longer (--) format.
       (4) invalid arg value. Mem must be 2, 4, or 8, Page 2 or 4 and guest must be an exe file.
    */

    if (argc != 7)
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
            myArgs->memory_sz = val; 
            is_set.mem = 1;
        } else if (strcmp(argv[i], "-p") == 0 || strcmp(argv[i], "--page") == 0) {
            if (is_set.pg) return 3;
            int val = atoi(argv[i+1]);
            if (val != 2 && val != 4) return 4;
            myArgs->page_sz = val; 
            is_set.pg = 1;
        } else if (strcmp(argv[i], "-g") == 0 || strcmp(argv[i], "--guest") == 0) {
            if (is_set.guest) return 3;
            if (access(argv[i+1], X_OK) != 0) return 4;
            strcpy(myArgs->guest_path, argv[i+1]);
            is_set.guest = 1;
        } 
    }
    return 0;
}

int main(int argc, char* argv[]) {

    args_t myArgs = {0};
    int read_args_status = read_args(argc, argv, &myArgs);
    switch (read_args_status) {
        case 0: printf("%s[HOST]%s Args read successfully.\n", GREEN_PREFIX, NORMAL_PREFIX); break;
        case 1: printf("%s[HOST]%s Invalid arg format. Try:\nexe (--memory|-m) <m> (--page|-p) <p> (--guest|-g) <g>\nwhere <m> is either 2, 4 or 8 and <p> either 2 or 4, and <g> an executable file\n", RED_PREFIX, NORMAL_PREFIX); break;
        case 2: printf("%s[HOST]%s Invalid number of args. Try:\nexe (--memory|-m) <m> (--page|-p) <p> (--guest|-g) <g>\nwhere <m> is either 2, 4 or 8 and <p> either 2 or 4, and <g> an executable file\n", RED_PREFIX, NORMAL_PREFIX); break;
        case 3: printf("%s[HOST]%s Arg set twice\n", RED_PREFIX, NORMAL_PREFIX); break;
        case 4: printf("%s[HOST]%s Invalid arg value.\n", RED_PREFIX, NORMAL_PREFIX); break;
        default:printf("%s[HOST]%s Unexpected read args status.\n", RED_PREFIX, NORMAL_PREFIX); break;
    };
    return 0;
}
