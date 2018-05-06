// stop.c
// Illustration of multiple filed module

#include <linux/kernel.h>
#include <linux/module.h>

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Shuyang Liu");


void cleanup_module() 
{
	printk(KERN_INFO "Goodbye from kernel\n");
}