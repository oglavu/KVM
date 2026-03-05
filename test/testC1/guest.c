#include "syscall.h"

void
__attribute__((noreturn))
__attribute__((section(".start")))
_start(void) {
	char c;

	// reading from shared file
	char filename[100];
	puts("Enter shared filename: ");
	gets(filename);
	puts(filename);

	int shared_fd = fopen(filename, "r+");
	if (shared_fd == 0) {
		puts("Cannot open shared file for reading.\n");
		goto done;
	}
	puts("Opened shared file for reading.\n");

	puts("Enter characters to replace (<old> <new>): ");
	char oc = getc();
	getc();
	char nc = getc();
	getc();

	putc(oc);
	putc(nc);
	
	int found = 0;
	while((c = fgetc(shared_fd)) != EOF) {
		putc(c);
		if (c == oc) {
			if (!found) {
				puts("Found missmatch.");
				gets(&c); // wait for approve
				found =1;
			}
			long pos = ftell(shared_fd);
			fseek(shared_fd, pos-1);
			if (EOF == fputc(nc, shared_fd)) {
				puts("fputc error\n");
				goto done;
			}
		}
	}

done:
	fclose(shared_fd);
	for (;;)
		asm("hlt");
}
