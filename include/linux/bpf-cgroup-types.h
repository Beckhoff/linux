/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _BPF_CGROUP_TYPES_H
#define _BPF_CGROUP_TYPES_H

#include <linux/bpf.h>
#include <linux/errno.h>
#include <linux/jump_label.h>
#include <linux/percpu.h>
#include <linux/percpu-refcount.h>
#include <linux/rbtree.h>
#include <uapi/linux/bpf.h>

struct sock;
struct sockaddr;
struct cgroup;
struct sk_buff;
struct bpf_map;
struct bpf_prog;
struct bpf_sock_ops_kern;
struct bpf_cgroup_storage;
struct ctl_table;
struct ctl_table_header;
struct task_struct;

#ifdef CONFIG_CGROUP_BPF

enum cgroup_bpf_attach_type {
	CGROUP_BPF_ATTACH_TYPE_INVALID = -1,
	CGROUP_INET_INGRESS = 0,
	CGROUP_INET_EGRESS,
	CGROUP_INET_SOCK_CREATE,
	CGROUP_SOCK_OPS,
	CGROUP_DEVICE,
	CGROUP_INET4_BIND,
	CGROUP_INET6_BIND,
	CGROUP_INET4_CONNECT,
	CGROUP_INET6_CONNECT,
	CGROUP_INET4_POST_BIND,
	CGROUP_INET6_POST_BIND,
	CGROUP_UDP4_SENDMSG,
	CGROUP_UDP6_SENDMSG,
	CGROUP_SYSCTL,
	CGROUP_UDP4_RECVMSG,
	CGROUP_UDP6_RECVMSG,
	CGROUP_GETSOCKOPT,
	CGROUP_SETSOCKOPT,
	CGROUP_INET4_GETPEERNAME,
	CGROUP_INET6_GETPEERNAME,
	CGROUP_INET4_GETSOCKNAME,
	CGROUP_INET6_GETSOCKNAME,
	CGROUP_INET_SOCK_RELEASE,
	MAX_CGROUP_BPF_ATTACH_TYPE
};

#define CGROUP_ATYPE(type) \
	case BPF_##type: return type

static inline enum cgroup_bpf_attach_type
to_cgroup_bpf_attach_type(enum bpf_attach_type attach_type)
{
	switch (attach_type) {
	CGROUP_ATYPE(CGROUP_INET_INGRESS);
	CGROUP_ATYPE(CGROUP_INET_EGRESS);
	CGROUP_ATYPE(CGROUP_INET_SOCK_CREATE);
	CGROUP_ATYPE(CGROUP_SOCK_OPS);
	CGROUP_ATYPE(CGROUP_DEVICE);
	CGROUP_ATYPE(CGROUP_INET4_BIND);
	CGROUP_ATYPE(CGROUP_INET6_BIND);
	CGROUP_ATYPE(CGROUP_INET4_CONNECT);
	CGROUP_ATYPE(CGROUP_INET6_CONNECT);
	CGROUP_ATYPE(CGROUP_INET4_POST_BIND);
	CGROUP_ATYPE(CGROUP_INET6_POST_BIND);
	CGROUP_ATYPE(CGROUP_UDP4_SENDMSG);
	CGROUP_ATYPE(CGROUP_UDP6_SENDMSG);
	CGROUP_ATYPE(CGROUP_SYSCTL);
	CGROUP_ATYPE(CGROUP_UDP4_RECVMSG);
	CGROUP_ATYPE(CGROUP_UDP6_RECVMSG);
	CGROUP_ATYPE(CGROUP_GETSOCKOPT);
	CGROUP_ATYPE(CGROUP_SETSOCKOPT);
	CGROUP_ATYPE(CGROUP_INET4_GETPEERNAME);
	CGROUP_ATYPE(CGROUP_INET6_GETPEERNAME);
	CGROUP_ATYPE(CGROUP_INET4_GETSOCKNAME);
	CGROUP_ATYPE(CGROUP_INET6_GETSOCKNAME);
	CGROUP_ATYPE(CGROUP_INET_SOCK_RELEASE);
	default:
		return CGROUP_BPF_ATTACH_TYPE_INVALID;
	}
}

#undef CGROUP_ATYPE

extern struct static_key_false cgroup_bpf_enabled_key[MAX_CGROUP_BPF_ATTACH_TYPE];
#define cgroup_bpf_enabled(atype) static_branch_unlikely(&cgroup_bpf_enabled_key[atype])

#define for_each_cgroup_storage_type(stype) \
	for (stype = 0; stype < MAX_BPF_CGROUP_STORAGE_TYPE; stype++)

struct bpf_cgroup_storage_map;

struct bpf_storage_buffer {
	struct rcu_head rcu;
	char data[];
};

struct bpf_cgroup_storage {
	union {
		struct bpf_storage_buffer *buf;
		void __percpu *percpu_buf;
	};
	struct bpf_cgroup_storage_map *map;
	struct bpf_cgroup_storage_key key;
	struct list_head list_map;
	struct list_head list_cg;
	struct rb_node node;
	struct rcu_head rcu;
};

struct bpf_cgroup_link {
	struct bpf_link link;
	struct cgroup *cgroup;
	enum bpf_attach_type type;
};

struct bpf_prog_list {
	struct list_head node;
	struct bpf_prog *prog;
	struct bpf_cgroup_link *link;
	struct bpf_cgroup_storage *storage[MAX_BPF_CGROUP_STORAGE_TYPE];
};

struct bpf_prog_array;

struct cgroup_bpf {
	/* array of effective progs in this cgroup */
	struct bpf_prog_array __rcu *effective[MAX_CGROUP_BPF_ATTACH_TYPE];

	/* attached progs to this cgroup and attach flags
	 * when flags == 0 or BPF_F_ALLOW_OVERRIDE the progs list will
	 * have either zero or one element
	 * when BPF_F_ALLOW_MULTI the list can have up to BPF_CGROUP_MAX_PROGS
	 */
	struct list_head progs[MAX_CGROUP_BPF_ATTACH_TYPE];
	u32 flags[MAX_CGROUP_BPF_ATTACH_TYPE];

	/* list of cgroup shared storages */
	struct list_head storages;

	/* temp storage for effective prog array used by prog_attach/detach */
	struct bpf_prog_array *inactive;

	/* reference counter used to detach bpf programs after cgroup removal */
	struct percpu_ref refcnt;

	/* cgroup_bpf is released using a work queue */
	struct work_struct release_work;
};

#else

struct cgroup_bpf {};

#endif /* CONFIG_CGROUP_BPF */

#endif /* _BPF_CGROUP_TYPES_H */
