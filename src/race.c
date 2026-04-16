// SPDX-License-Identifier: GPL-2.0
#include "linux/kthread.h"
#include <linux/module.h>

enum { COUNTER_LIMIT = 64, NUM_THREADS = 2 };

static unsigned long counter;
static struct task_struct *threads[NUM_THREADS];

struct thread_data {
	uint32_t id;
	struct completion done;
};

static struct thread_data args[NUM_THREADS];

/**
 * __increment_counter()	- Thread worker to increment counter.
 *
 * @arg:	struct thread_data* for the current thread.
 *
 * Return:
 * * 0 on success.
 */
static int __increment_counter(void *arg)
{
	struct thread_data *tdata = (struct thread_data *)arg;

	while (counter < COUNTER_LIMIT) {
		if (kthread_should_stop())
			break;

		counter++;

		pr_info(KBUILD_MODNAME
			": thread #%d > counter updated to %lu\n",
			tdata->id, counter);
	}

	complete(&tdata->done);
	return 0;
}

static int __init race_init(void)
{
	counter = 0;

	for (int i = 0; i < NUM_THREADS; ++i) {
		args[i].id = i;
		init_completion(&args[i].done);

		threads[i] = kthread_run(__increment_counter, &args[i],
					 "increment_counter");

		if (IS_ERR(threads[i])) {
			pr_err(KBUILD_MODNAME ": failed to start thread #%d!\n",
			       i);

			return -ENOMEM;
		}
	}

	return 0;
}

static void __exit race_exit(void)
{
	for (int i = 0; i < NUM_THREADS; ++i) {
		if (threads[i] && !completion_done(&args[i].done))
			kthread_stop(threads[i]);
	}
}

module_init(race_init);
module_exit(race_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Rumen Mitov <rumenmitov@disroots.org>");
MODULE_DESCRIPTION(
	KBUILD_MODNAME
	" - A Linux kernel module that simulates a race condition in the kernel.");
