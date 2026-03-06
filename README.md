# Mini Hypervisor
This project implements a minimal userspace hypervisor built on top of the Linux Kernel-based Virtual Machine (KVM) interface for x86 architecture. The hypervisor creates and manages virtual machines, supports multiple vCPUs, configures paging and long mode, loads guest images, and handles basic guest–host interaction through I/O exits and custom system calls.

The project was developed as part of the Computer Architecture and Organization 2 (AOR2) course project and serves as an educational implementation demonstrating the core mechanisms required to run a guest program using hardware virtualization.

## Overview
The hypervisor is responsible for:
* Allocating guest physical memory, and setting page tables
* Initializing CPU state and entering 64-bit long mode
* Creating and running one or more vCPUs concurrently
* Loading guest images into memory
* Handling VM exits (I/O, shutdown, error)
* Providing a small guest file system interface via custom system calls

The guest code communicates with the hypervisor through I/O port exits, which are intercepted and handled by the host. Furthermore, system calls are implemented on top of I/O exits allowing the guest to perform simple operations such as console I/O and file access.

## Usage
The hypervisor is started from the command line by specifying one or more virtual machines and optional shared files.

```
mini_hypervisor.a \
  --vm image=<guest image>,cpus=<n>,mem=<m>,page=<p> \
  [{--vm image=<guest image>,cpus=<n>,mem=<m>,page=<p>}] \
  [-f filename{,filename}] 
```

Each `--vm` option defines a virtual machine configuration:
1. `guest_image` specifies a path to the guest image. Valid entries: any executable file
2. `cpus` specifies the number of vCPUs to run the given image: Valid entries: dependant on host CPU
3. `mem` specifies the total size of quest memory. Valid entries: 2, 4, 8 [MiB]
4. `page` specifies the page size. Valid entries: 4 [KiB], 2 [MiB]

Files passed with `-f` or `--file` are copied into the VM’s shared filesystem and can be accessed by guest programs. These files will appear in the guest-accessible shared directory (`drive/`)

## Build Process
The project uses a simple Makefile build system. All compiled binaries are available in `bin/`.

To build the hypervisor:
```
make hypervisor
```
It is possible to build individual test samples e.g.:
```
make testA1
``` 

## Prerequisites

The hypervisor requires:
* Linux with KVM support
* C++17 compiler
* Make build system
* linux-kvm headers

To verify KVM support:
```
ls /dev/kvm
```
Also, you may need to load the kernel modules:
```
sudo modprobe kvm
sudo modprobe kvm_intel   # or kvm_amd
```