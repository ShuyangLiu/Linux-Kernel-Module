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

