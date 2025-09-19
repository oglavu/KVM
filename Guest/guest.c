#include <stdint.h>

#define EOF (int)-1

static const int FIO_PORT = 0x278;
static const int CIO_PORT = 0xE9;

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
	inb(CIO_PORT, &ret);
	return ret;
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
	return syscall(0, (long)filename, (long)mode);
}

int fputc(int b, int fd) {
	return syscall(2, (long)b, (long)fd);
}

int fgetc(int fd) {
	return syscall(3, (long)fd, 0);
}

int fclose(int fd) {
	return syscall(1, (long)fd, 0);
}

void
__attribute__((noreturn))
__attribute__((section(".start")))
_start(void) {
	const char *p;

	for (p = "Hello, world!\n"; *p; ++p)
		outb(0xE9, *p);

	// reading from shared file
	int fd = fopen("Makefile", "r");
	char c;
	while((c = fgetc(fd)) != EOF) {
		putc(c);
	}
	fclose(fd);

	// writing to proc-specific file
	int fd2 = fopen("custom.txt", "w");
	for (p = "Hello, world!\n"; *p; ++p)
		fputc(*p, fd2);
	fclose(fd2);

	for (;;)
		asm("hlt");
}
