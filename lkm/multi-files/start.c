// start.c
// Illustration of multiple filed module

#include <linux/kernel.h>
#include <linux/module.h>

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Shuyang Liu");


int init_module(void)
{
	printk(KERN_INFO "Hello, world from kernel \n");
	return 0;
}