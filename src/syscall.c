
#include "syscall.h"

// keep ports hidden so guest is imposed only the valid interface
static const uint16_t FIO_PORT = 0x278;
static const uint16_t CIO_PORT = 0xE9;

static void outb(uint16_t port, uint8_t value) {
	asm("outb %0,%1" : /* empty */ : "a" (value), "Nd" (port) : "memory");
}

static void inb(uint16_t port, uint8_t* value) {
	asm("inb %1,%0" : "=a" (*value) : "Nd" (port) : "memory");
}

void putc(int c) {
	outb(CIO_PORT, c);
}

char getc() {
	char ret;
	inb(CIO_PORT, (unsigned char*) &ret);
	return ret;
}

void puts(const char* s) {
	for (const char* p = s; *p; putc(*p++));
}

void gets(char* s) {
	int c;
    while ((c = getc()) != EOF && c != '\n') {
        *s++ = (char)c;
    }
    *s = '\0';
}

static inline long syscall(long num, long arg1, long arg2) {
    register long rax asm("rax") = num;
    register long rbx asm("rbx") = arg1;
    register long rcx asm("rcx") = arg2;
    asm volatile("outb %%al, %3"
                 : "+a"(rax)
                 : "b"(rbx), "c"(rcx), "Nd" (FIO_PORT)
                 : "memory");
    return rax;
}

int fopen(const char *filename, const char *mode) {
	return syscall(SYS_FOPEN, (long)filename, (long)mode);
}

int fputc(int b, int fd) {
	return syscall(SYS_FPUTC, (long)b, (long)fd);
}

int fgetc(int fd) {
	return syscall(SYS_FGETC, (long)fd, 0);
}

int fclose(int fd) {
	return syscall(SYS_FCLOSE, (long)fd, 0);
}

long ftell(int fd) {
	return syscall(SYS_FTELL, (long)fd, 0);
}

int fseek(int fd, long offset) {
	return syscall(SYS_FSEEK, (long)fd, offset);
}