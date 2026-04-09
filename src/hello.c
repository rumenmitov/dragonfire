// SPDX-License-Identifier: GPL-2.0
#include <linux/module.h>
#include <linux/vmalloc.h>

static void *kbuf;
static void *vbuf;

static int __init hello_init(void)
{
	pr_info("Hello, world!\n");

	kbuf = kmalloc(1024, GFP_KERNEL);
	if (!kbuf)
		return -ENOMEM;

	vbuf = vmalloc(1024);
	if (!vbuf) {
		kfree(kbuf);
		kbuf = NULL;

		return -ENOMEM;
	}

	pr_info(KBUILD_MODNAME ": kbuf address = %px\n", kbuf);
	pr_info(KBUILD_MODNAME ": bbuf address = %px\n", vbuf);

	return 0;
}

static void __exit hello_exit(void)
{
	kfree(kbuf);
	kbuf = NULL;

	vfree(vbuf);
	vbuf = NULL;

	pr_info("Goodbye, world!\n");
}

module_init(hello_init);
module_exit(hello_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Rumen Mitov <rumenmitov@disroots.org>");
MODULE_DESCRIPTION(KBUILD_MODNAME " - A Linux kernel module.");
