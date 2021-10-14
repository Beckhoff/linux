/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef _LINUX_SKBUFF_TYPES_HEAD_H
#define _LINUX_SKBUFF_TYPES_HEAD_H

#include <linux/spinlock_types.h>

struct sk_buff;

struct sk_buff_head {
	/* These two members must be first. */
	struct sk_buff	*next;
	struct sk_buff	*prev;

	__u32		qlen;
	spinlock_t	lock;
};

#endif	/* _LINUX_SKBUFF_TYPES_HEAD_H */
