#include "syscall.h"

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
