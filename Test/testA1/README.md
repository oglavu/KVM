# Test IN/OUT + Mem

App allocates a 4 KB page and asks user to modify it.

1. User is prompted to enter the number of bytes to be changed. Number entered is rounded to four digits with mandatory leading zeros, e.g. 0008 for 8 digits.

2. User then enters characters starting from the SOF consecutively and once N characters is entered a \n can be entered

3. User is promted to enter the number of bytes to be shown starting from the SOF in the same format as in (1)