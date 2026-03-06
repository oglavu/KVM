#include <syscall.h>

#define BUF_SIZE 32

volatile char buffer[BUF_SIZE];
volatile int head = 0;
volatile int tail = 0;
volatile int count = 0;
volatile int done = 0;

static void producer(void) {
    int f = fopen("input.txt", "r");
    if (f < 0) {
        puts("Producer: failed to open input.txt\n");
        return;
    }

    int c;
    while ((c = fgetc(f)) != EOF) {

        /* wait while buffer full */
        while (count == BUF_SIZE) { }

        buffer[head] = (char)c;
        head = (head + 1) % BUF_SIZE;
        count++;
    }

    fclose(f);
    done = 1;
}

static void consumer(void) {
    int f = fopen("output.txt", "w");
    if (f < 0) {
        puts("Consumer: failed to open output.txt\n");
        return;
    }

    while (1) {

        while (count == 0) {
            if (done) {
                fclose(f);
                return;
            }
        }

        char c = buffer[tail];
        tail = (tail + 1) % BUF_SIZE;
        count--;

        // Caesar cipher
        c = c + 3;

        fputc(c, f);
    }
}

void
__attribute__((noreturn))
__attribute__((section(".start")))
_start(void) {

    puts("Guest started\n");
    getc();
    int cpu = get_cpuid();

    if (cpu == 0) {
        puts("Producer running on CPU0\n");
        producer();
    } else if (cpu == 1) {
        puts("Consumer running on CPU1\n");
        consumer();
    }

    for (;;)
        asm("hlt");
}