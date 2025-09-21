# Test IN/OUT + Mem

App allocates a 4 KB page and let user access & modify it.

1. Host is prompted to enter the initial state of Guest's page. Starting from SOF Host provided data is filled in form of bytes. For instance, if Host enters: 23 41 33\n, Guest's memory layout will be as follows: mem[0]=0x23; mem[1]=0x41; mem[2]=0x33

2. Host can permorm the following operations on Guest's memory:
    u [up] - increment the iterator and print corresponding memory value
    d [down] - decrement the iterator and print corresponding memory value
    i XX [insert] - set 0xXX on the iterator's address
    o XX [offset] - perform relative jump in the array. *Note*: XX is in first complement.
    r XX [range] - print XX consecutive memory addresses excluding iterator. *Note*: XX is in first complement. 

3. Host can exit anytime by entering newline instead of command