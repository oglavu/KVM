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
	inb(CIO_PORT, &ret);
	return (char)ret;
}

void puts(const char* s) {
	for (const char* p = s; *p; putc(*p++));
}

int atoi(const char *str) {
	int result = 0;
	int sign = 1;        // Default sign is positive

	if (*str == '-') {
        sign = -1;
        str++;
    } else if (*str == '+') {
        str++;
    }

    while (*str >= '0' && *str <= '9') {
        int digit = *str - '0';
        result = result * 10 + digit;
        str++;
    }

    return (int)(result * sign);
}

char page[4096];

void
__attribute__((noreturn))
__attribute__((section(".start")))
_start(void) {
	char *p;
	char c;
	char *s = "4096";

	puts("Unesi koliko menjas (n<4096): ");

	p = s;
	for (p = s; *p; ++p)
		*p=getc();
	getc(); // \n
	int n = atoi(s);

	puts("Menjaj: ");
	for (p = page; p < page+n; ++p) {
		*p = getc();
	}
	getc(); // \n

	puts("Unesi koliko gledas (n<4096): ");
	p = s;
	for (p = s; *p; ++p)
		*p=getc();
	getc(); // \n
	n = atoi(s);

	for (p = page; p<page+n; ++p) {
		putc(*p);
	}

	for (;;)
		asm("hlt");
}
