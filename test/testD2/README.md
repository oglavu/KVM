# Producer–Consumer test

This test demonstrates concurrent execution of multiple vCPUs using a producer–consumer pattern implemented through shared guest memory.

Two vCPUs execute the same program but perform different roles depending on their CPU ID:
    1. vCPU 0 (Producer)
Opens input.txt, reads characters, applies a Caesar cipher by shifting each ASCII character by +3, and places them into a shared ring buffer located in guest memory.
    2.vCPU 1 (Consumer)
Reads characters from the shared buffer, and writes the result to output.txt.

The buffer and its control variables are stored in *global guest memory*. This allows the producer and consumer to communicate without using hypervisor-mediated messaging.

Expected output is creation of output.txt file containing deciphered text from input.txt.