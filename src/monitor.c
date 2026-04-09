// SPDX-License-Identifier: GPL-2.0

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/workqueue.h>
#include <linux/kthread.h>
#include <linux/delay.h>

enum { MAX_SNAPSHOTS = 64 };

static int interval = 5;
module_param(interval, int, 0444);
MODULE_PARM_DESC(interval, "How often, in seconds, to poll the process list.");

static struct snapshot *snapshots;
static uint32_t snapshot_head;

static struct workqueue_struct *monitor_wq;
static struct task_struct *monitor_task;
static struct work_struct monitor_work;
static bool monitor_loop_early_exit;

struct snapshot {
	ktime_t timestamp;
	uint32_t process_count;
	uint32_t cpu_id;
};

#define snapshots_for_each(x)                \
	for (struct snapshot *x = snapshots; \
	     x < snapshots + (snapshot_head % MAX_SNAPSHOTS); ++x)

/**
 * __monitor_count_procs()	- Logs the current process count and
 *														updates the snapshots ringbuf.
 */
static void __monitor_count_procs(void)
{
	struct task_struct *task;
	uint32_t count = 0;

	rcu_read_lock();
	for_each_process(task) {
		count++;
	}
	rcu_read_unlock();

	snapshots[snapshot_head++ % MAX_SNAPSHOTS] =
		(struct snapshot){ .timestamp = ktime_get(),
				   .process_count = count,
				   .cpu_id = smp_processor_id() };

	pr_info(KBUILD_MODNAME ": %u processes\n", count);
}

/**
 * __monitor_work_handler() - Work item that counts processes.
 */
static void __monitor_work_handler(struct work_struct *work)
{
	(void)work;
	__monitor_count_procs();
}

/**
 * __monitor_loop() - Every %interval seconds, add a work item to the
 * workqueue.
 *
 * Signal %SIGINT calls __monitor_count_procs() (i.e. the work item).
 *
 * Return:
 * * 0 on success
 * * %-EINVAL on error.
 */
static int __monitor_loop(void *args)
{
	unsigned long msec_left;
	bool sigint_rcvd = false;
	bool sigterm_rcvd = false;

	(void)args;

	allow_signal(SIGINT);
	allow_signal(SIGTERM);

	INIT_WORK(&monitor_work, __monitor_work_handler);

	while (!kthread_should_stop()) {
		msec_left = msleep_interruptible(interval * 1000);

		if (msec_left != 0 && signal_pending(current)) {
			if (sigismember(&current->signal->shared_pending.signal,
					SIGINT)) {
				__monitor_count_procs();
				sigint_rcvd = true;
			}

			if (sigismember(&current->signal->shared_pending.signal,
					SIGTERM)) {
				pr_info(KBUILD_MODNAME ": SIGTERM received\n");

				sigterm_rcvd = true;
			}

			flush_signals(current);
		}

		if (sigterm_rcvd) {
			monitor_loop_early_exit = true;
			break;
		}

		if (sigint_rcvd) {
			sigint_rcvd = false;
			continue;
		}

		if (!monitor_wq) {
			pr_err(KBUILD_MODNAME ": invalid workqueue!\n");
			monitor_loop_early_exit = true;
			return -EINVAL;
		}

		bool not_already_queued = queue_work(monitor_wq, &monitor_work);

		if (!not_already_queued)
			pr_warn(KBUILD_MODNAME ": work already queued\n");
	}

	return 0;
}

static int __init monitor_init(void)
{
	if (interval < 1) {
		pr_warn(KBUILD_MODNAME
			": interval parameter is less than 1, setting to 1\n");

		interval = 1;
	}

	snapshots = kcalloc(MAX_SNAPSHOTS, sizeof(struct snapshot), GFP_KERNEL);
	if (!snapshots)
		return -ENOMEM;

	snapshot_head = 0;

	monitor_wq = alloc_workqueue("monitor_wq", WQ_PERCPU, 1);
	if (!monitor_wq) {
		kfree(snapshots);
		snapshots = NULL;

		pr_err(KBUILD_MODNAME ": failed to create workqueue!\n");
		return -EINVAL;
	}

	pr_info(KBUILD_MODNAME ": created workqueue\n");

	monitor_task = kthread_run(__monitor_loop, NULL, "monitor_task");
	if (IS_ERR(monitor_task)) {
		pr_err(KBUILD_MODNAME ": failed to start monitor task!\n");

		kfree(snapshots);

		if (monitor_wq) {
			flush_workqueue(monitor_wq);
			destroy_workqueue(monitor_wq);
			monitor_wq = NULL;
		}

		return PTR_ERR(monitor_task);
	}

	pr_info(KBUILD_MODNAME ": started monitor task\n");

	return 0;
}

static void __exit monitor_exit(void)
{
	if (monitor_task && !monitor_loop_early_exit) {
		(void)kthread_stop(monitor_task);
		monitor_task = NULL;
	}

	pr_info(KBUILD_MODNAME ": stopped monitor task\n");

	if (monitor_wq) {
		flush_workqueue(monitor_wq);
		destroy_workqueue(monitor_wq);
		monitor_wq = NULL;
	}

	pr_info(KBUILD_MODNAME ": stopped workqueue\n");

	if (snapshots) {
		snapshots_for_each(snap)
		{
			pr_info(KBUILD_MODNAME
				": snapshot entry > Timestamp %lld ms | %u processes | CPU #%u",
				ktime_to_ms(snap->timestamp),
				snap->process_count, snap->cpu_id);
		}

		kfree(snapshots);
		snapshots = NULL;
	}
}

module_init(monitor_init);
module_exit(monitor_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Rumen Mitov <rumenmitov@disroots.org>");
MODULE_DESCRIPTION(
	KBUILD_MODNAME
	" - A Linux kernel module that counts processes periodically.");
