# Obstacles in this project
## Linux Kernel Module Development
Because directly modifying the kernel source code is very risky (especially code related to system calls), I decided to use Linux Kernel Modules to implement malicious system calls. However, there are many difficulties during process of implementation without changing the kernel source code.
- __Lack of knowledge of Virtual Machines, Linux Kernel internals, and VFS__
- __Internal Compiler Error__
    - For some unknown reasons, on Ubuntu 16.04, it can sometimes trigger segment fault error inside the c compiler
- __Corruption caused by bugs in the module code__
    - Because this kernel module changes the behavior of some important and widely used system call functions,any bug in the code can cause huge corruption in the entire system. During the phase of development and testing, there were several times when a bug corrupted the kernel memory and caused the system to be not usable anymore. Although this is in a virtual environment, the time for setting up a new virtual machine again is not trivial. 
- __Protection mechanisms from the kernel__
    1. **_Difficulty of accessing system call table_**
        - Unlike older versions of Linux kernels, newer versions (>= 2.6) of Linux kernel does not export the system call table. As a result, the system call table is not directly accessible to the  dynamically loaded modules.
        - A way to bypass this problem is to manually look for the address of the system call table in the memory. In the directory `/boot/`, there is a file `System.map` which record the some addresses of functions and system data structures. The address of the system call table can be found in this file.
    2. **_System call table is not writable by default_**
        - As part of the protection mechanisms provided by the kernel, the page where the system call table is at is a read-only page. Any direct write to that page would be killed.
        - In order to implement the system hook module for this project, I needed to turn the page containing the system call table to writable. This step would require the functions `write_cr0` and `read_cr0` to change the protected bit of the control register. Then `pages_rw` to change the page from read-only to writable.

    3. **_Operations on files are highly not encouraged in kernel space_**
        - As explained by [this article](https://www.linuxjournal.com/article/8110), the original `open()` system call does checks on the passed arguments to ensure that they are from user space instead of the kernel space. In fact, opening, reading and writing files are not encouraged in kernel development because one of the design principle of Linux kernel is that the kernel should only provide mechanisms instead of policies. Opening, reading and writing files are seen as policies thus they are not encouraged in the development kernel modules. As a result, a lot of related functions (including helper functions used in the operation of open, read, and write) are not available to the kernel module.
        - `filp_open()` and `filp_close()` are used for creating and closing files in kernel space. For some unknown reasons (maybe due to stronger protections from recent versions of Linux kernel), these two functions are not usable on kernel versions >= 2.6 (always yield error when trying to create new files). As a result, I changed the environment system to Fedora 14 with Linux 2.6. Changing the entire environment required me to adjust the existing code to work with this version of Linux kernel. Unfortunately,some functions that were available previously becomes unaccessible.
