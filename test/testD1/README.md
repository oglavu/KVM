# Test vCPU stack isolation

This test behaves similarly to [testA1](../testA1/README.md), with the key difference being the modified array allocated on the stack rather than in static memory. Since each vCPU has its own stack, each vCPU can modify its own copy independently, allowing for distinct execution on different vCPUs.