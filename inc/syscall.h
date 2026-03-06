
#include <stdint.h>

#define SYS_FOPEN 0
#define SYS_FCLOSE 1
#define SYS_FPUTC 2
#define SYS_FGETC 3
#define SYS_FTELL 4
#define SYS_FSEEK 5

#define SYS_CPUID 6

#ifdef GUEST_BUILD

#define EOF (int)-1

// util
int get_cpuid();

// std::io system calls
void putc(int c);

char getc();

void puts(const char* s);

void gets(char* s);

// std::fio system calls
int fopen(const char *filename, const char *mode);

int fputc(int b, int fd);

int fgetc(int fd);

int fclose(int fd);

long ftell(int fd);

int fseek(int fd, long offset);

#endif