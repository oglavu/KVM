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

uint8_t page[4096];

void
__attribute__((noreturn))
__attribute__((section(".start")))
_start(void) {

	char s[2];
	uint8_t *it;
	
	puts("Initial fill (XX XX ...)");

	it = page;
	while(1) {
		s[0] = getc();
		if (s[0] == '\n') break;
		s[1] = getc();
		if (s[1] == '\n') break;
		
		*it++ = hex_to_byte(s);
		if (getc() == '\n') break; // whitespace
	}

	puts("Access & Modify memory ( u | d | o XX | i XX | r XX )");

	char c; it = page;
	while((c = getc()) != '\n') {
		if (c == 'u') {
			byte_to_hex(*++it, s);
		} else if (c == 'd') {
			byte_to_hex(*--it, s);
		} else if (c == 'i') {
			getc(); // whitespace
			s[0] = getc();
			s[1] = getc();
			*it = hex_to_byte(s);
		} else if (c == 'o') {
			getc(); // whitespace
			s[0] = getc();
			s[1] = getc();
			uint8_t off = hex_to_byte(s);
			it += (off & 0x80) ? -(off & 0x7f) : (off & 0x7f);
			byte_to_hex(*it, s);
		} else if (c == 'r') {
			getc(); // whitespace
			s[0] = getc();
			s[1] = getc();
			uint8_t off = hex_to_byte(s);
			int sgn = (off & 0x80 ? -1 : 1);
			uint8_t cnt = off & 0x7f;
			uint8_t* start = (off & 0x80 ? it-cnt : it+1);
			for (uint8_t i = 0; i < cnt-1; ++i) {
				byte_to_hex(*(start + i), s);
				putc(s[0]);
				putc(s[1]);
				putc(' ');
			}
			byte_to_hex(*(start+cnt-1), s);
		}
		putc(s[0]);
		putc(s[1]);
		putc('\n');
		getc(); // newline
	}

	for (;;)
		asm("hlt");
}
