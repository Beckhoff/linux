/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_POSIX_TIMERS_H
#define _LINUX_POSIX_TIMERS_H

#include <linux/sched/per_task.h>

DECLARE_PER_TASK(struct posix_cputimers, posix_cputimers);

#ifdef CONFIG_POSIX_CPU_TIMERS_TASK_WORK
DECLARE_PER_TASK(struct posix_cputimers_work, posix_cputimers_work);
#endif

#endif /* _LINUX_POSIX_TIMERS_H */
