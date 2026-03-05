#include "syscall.h"

void
__attribute__((noreturn))
__attribute__((section(".start")))
_start(void) {

	// reading from shared file
	char filename[100];
	puts("Enter shared filename: ");
	gets(filename);
	puts(filename);

	int fd = fopen(filename, "w+");
	if (fd == 0) {
		puts("Cannot open file for writing.\n");
		goto done;
	}
	puts("Opened file for writing.\n");

	puts("Enter up to 100 chars to be in file ");
	gets(filename);
	
	for (const char* p=filename; *p; p++) {
		fputc(*p, fd);
	}
	fclose(fd);

done:
	for (;;)
		asm("hlt");
}
