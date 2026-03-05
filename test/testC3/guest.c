#include "syscall.h"

void
__attribute__((noreturn))
__attribute__((section(".start")))
_start(void) {
	
	char src_filename[100], dst_filename[100];
	char c;

	puts("Enter copy src file: ");
	gets(src_filename);
	int src_fd = fopen(src_filename, "r");
	if (src_fd == 0) {
		puts("Cannot open src file for reading.\n");
		goto done;
	}
	puts("Enter copy dst file: ");
	gets(dst_filename);
	int dst_fd = fopen(dst_filename, "w+");
	if (src_fd == 0) {
		puts("Cannot open dst file for writing.\n");
		goto done;
	}
	
	while( (c = fgetc(src_fd)) != EOF) {
		fputc(c, dst_fd);
	}

	puts("Finished file copy.");

done:
	for (;;)
		asm("hlt");
}
