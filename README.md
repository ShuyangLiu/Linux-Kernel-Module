# Linux Kernel Loadable Module
## Files
- The source code and makefile are all in the `syscall/` directory

## Description 
- In this project, a linux kernel module, which replaces the original `open()`, `read()`, `write()` system call functions, is implemented. The module assumes there are two users on the machine, a regular user that serves as the victim and an user named `mallory` who attempts to get and modify information about victim's files that he does not have access to. To achieve this goal, he uses this loadable kernel module to change the behavior of `open()`, `read()`, and `write()` system calls. 
- Originally, the `open()` system call finds the (or create a new) file, allocate a new file descriptor, install the file descriptor with the file object, and return the file descriptor to the user. In this module, instead of allocating one file object and one file descrptor, it allocates two of them. One is the actual file that the user wants to use in the future (we call it the "actual" file) and another one is the file that is located in `mallory`'s home directory and has access permission bits set to 777 (we call it the "copy" file). The file descriptor pointing to the copy file is returned to the user. This gives user an illusion that the actual file she wants to open is opened. In the future, when she is doing operations on the file using this wrong file descriptor, she is actually writing or reading from the malicious copy file. Since `mallory` has access to the copy files, he can easily get or manipulate the content of those files. 

- `read()` and `write()` system calls are modified to work with `open()` system call to maintain the user's illusion. `read()` system call finds the copy file first. Then, since the copied file's file name is carefully constructed according to the actual file, the actual file can be found and read. After information is put into the buffer, it writes the buffer to the copied file. `write()` function directly writes the content of the buffer to both of the files. In this way,
  `mallory` can get the partial informations about the content of the files. 

## Environment
- Virtualbox 5.2.4
- Fedora 14 
- Linux Kernel Version 2.6.35.6

## Compile 
- To compile the source code, use `make` under the source code directory

## Load module into system
- To load the module into the system, use the following command:
```
# insmod sys_call_replace_open.ko uid=<uid>
```
where `uid` is the uid of the victim user

## Unload the module from the system
- To unload the module from the system, use the following command:
```
# rmmod sys_call_replace_open
```
This will remove the module from the system and recover the original functionality of `open()`, `read()`, and `write()` system calls

## Obstacles in this project
### Linux Kernel Module Development
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
