#include <stdint.h>

#define EOL '\n'

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
	uint8_t ret;
	inb(CIO_PORT, (unsigned char*) &ret);
	return (char)ret;
}

void puts(const char* s) {
	for (const char* p = s; *p; putc(*p++));
}

int my_atoi(const char* str) {

    int sign = 1;
    if (*str == '-') {
        sign = -1;
        str++;
    } else if (*str == '+') {
        str++;
    }

    int result = 0;
    while (*str) {
        int digit = *str - '0';
        result = result * 10 + digit;
        str++;
    }

    return result * sign;
}

uint8_t hex_to_byte(const char *s) {
    uint8_t hi = ('0' <= s[0] && s[0] <= '9') ? s[0]-'0' : 10+s[0]-'a';
    uint8_t lo = ('0' <= s[1] && s[1] <= '9') ? s[1]-'0' : 10+s[1]-'a';
    return (uint8_t)((hi << 4) | lo);
}

void byte_to_hex(uint8_t val, char buf[2]) {
    const char *digits = "0123456789abcdef";
    buf[0] = digits[(val >> 4) & 0xF];
    buf[1] = digits[val & 0xF];
}

#define N 2   // number of pages
#define M 0x200000 // page size
uint8_t page[N][M];

void
__attribute__((noreturn))
__attribute__((section(".start")))
_start(void) {
	
	for (int i=0; i<N; ++i) {
		page[i][0] = i;
	}

	for (int i=0; i<N; ++i) {
		if (page[i][0] != i) {
			goto done;
		}
	}

	puts("Hello, world!!\n");

done:
	for (;;)
		asm("hlt");
}
