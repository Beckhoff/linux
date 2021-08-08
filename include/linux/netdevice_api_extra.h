/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * INET		An implementation of the TCP/IP protocol suite for the LINUX
 *		operating system.  INET is implemented using the  BSD Socket
 *		interface as the means of communication with the user level.
 *
 *		Definitions for the Interfaces handler.
 *
 * Version:	@(#)dev.h	1.0.10	08/12/93
 *
 * Authors:	Ross Biro
 *		Fred N. van Kempen, <waltje@uWalt.NL.Mugnet.ORG>
 *		Corey Minyard <wf-rch!minyard@relay.EU.net>
 *		Donald J. Becker, <becker@cesdis.gsfc.nasa.gov>
 *		Alan Cox, <alan@lxorguk.ukuu.org.uk>
 *		Bjorn Ekwall. <bj0rn@blox.se>
 *              Pekka Riikonen <priikone@poseidon.pspt.fi>
 *
 *		Moved to /usr/include/linux for NET3
 */
#ifndef _LINUX_NETDEVICE_API_EXTRA_H
#define _LINUX_NETDEVICE_API_EXTRA_H

#include <linux/netdevice_api.h>

#include <linux/rculist.h>
#include <linux/skbuff_api.h>

#include <net/net_namespace.h>

/**
 *	napi_if_scheduled_mark_missed - if napi is running, set the
 *	NAPIF_STATE_MISSED
 *	@n: NAPI context
 *
 * If napi is running, set the NAPIF_STATE_MISSED, and return true if
 * NAPI is scheduled.
 **/
static inline bool napi_if_scheduled_mark_missed(struct napi_struct *n)
{
	unsigned long val, new;

	do {
		val = READ_ONCE(n->state);
		if (val & NAPIF_STATE_DISABLE)
			return true;

		if (!(val & NAPIF_STATE_SCHED))
			return false;

		new = val | NAPIF_STATE_MISSED;
	} while (cmpxchg(&n->state, val, new) != val);

	return true;
}

static inline bool netdev_phys_item_id_same(struct netdev_phys_item_id *a,
					    struct netdev_phys_item_id *b)
{
	return a->id_len == b->id_len &&
	       memcmp(a->id, b->id, a->id_len) == 0;
}

static inline struct netdev_queue *skb_get_tx_queue(const struct net_device *dev,
						    const struct sk_buff *skb)
{
	return netdev_get_tx_queue(dev, skb_get_queue_mapping(skb));
}

#define net_device_entry(lh)	list_entry(lh, struct net_device, dev_list)

static inline struct net_device *next_net_device_rcu(struct net_device *dev)
{
	struct list_head *lh;
	struct net *net;

	net = dev_net(dev);
	lh = rcu_dereference(list_next_rcu(&dev->dev_list));
	return lh == &net->dev_base_head ? NULL : net_device_entry(lh);
}

static inline struct net_device *first_net_device_rcu(struct net *net)
{
	struct list_head *lh = rcu_dereference(list_next_rcu(&net->dev_base_head));

	return lh == &net->dev_base_head ? NULL : net_device_entry(lh);
}

static inline struct net_device *next_net_device(struct net_device *dev)
{
	struct list_head *lh;
	struct net *net;

	net = dev_net(dev);
	lh = dev->dev_list.next;
	return lh == &net->dev_base_head ? NULL : net_device_entry(lh);
}

static inline struct net_device *first_net_device(struct net *net)
{
	return list_empty(&net->dev_base_head) ? NULL :
		net_device_entry(net->dev_base_head.next);
}

int __dev_direct_xmit(struct sk_buff *skb, u16 queue_id);

static inline int dev_direct_xmit(struct sk_buff *skb, u16 queue_id)
{
	int ret;

	ret = __dev_direct_xmit(skb, queue_id);
	if (!dev_xmit_complete(ret))
		kfree_skb(skb);
	return ret;
}
static inline void *skb_gro_header_slow(struct sk_buff *skb, unsigned int hlen,
					unsigned int offset)
{
	if (!pskb_may_pull(skb, hlen))
		return NULL;

	skb_gro_frag0_invalidate(skb);
	return skb->data + offset;
}

static inline void *skb_gro_network_header(struct sk_buff *skb)
{
	return (NAPI_GRO_CB(skb)->frag0 ?: skb->data) +
	       skb_network_offset(skb);
}

static inline void skb_gro_postpull_rcsum(struct sk_buff *skb,
					const void *start, unsigned int len)
{
	if (NAPI_GRO_CB(skb)->csum_valid)
		NAPI_GRO_CB(skb)->csum = csum_sub(NAPI_GRO_CB(skb)->csum,
						  csum_partial(start, len, 0));
}

/* GRO checksum functions. These are logical equivalents of the normal
 * checksum functions (in skbuff.h) except that they operate on the GRO
 * offsets and fields in sk_buff.
 */

__sum16 __skb_gro_checksum_complete(struct sk_buff *skb);

static inline bool skb_at_gro_remcsum_start(struct sk_buff *skb)
{
	return (NAPI_GRO_CB(skb)->gro_remcsum_start == skb_gro_offset(skb));
}

static inline bool __skb_gro_checksum_validate_needed(struct sk_buff *skb,
						      bool zero_okay,
						      __sum16 check)
{
	return ((skb->ip_summed != CHECKSUM_PARTIAL ||
		skb_checksum_start_offset(skb) <
		 skb_gro_offset(skb)) &&
		!skb_at_gro_remcsum_start(skb) &&
		NAPI_GRO_CB(skb)->csum_cnt == 0 &&
		(!zero_okay || check));
}

static inline __sum16 __skb_gro_checksum_validate_complete(struct sk_buff *skb,
							   __wsum psum)
{
	if (NAPI_GRO_CB(skb)->csum_valid &&
	    !csum_fold(csum_add(psum, NAPI_GRO_CB(skb)->csum)))
		return 0;

	NAPI_GRO_CB(skb)->csum = psum;

	return __skb_gro_checksum_complete(skb);
}

static inline void skb_gro_incr_csum_unnecessary(struct sk_buff *skb)
{
	if (NAPI_GRO_CB(skb)->csum_cnt > 0) {
		/* Consume a checksum from CHECKSUM_UNNECESSARY */
		NAPI_GRO_CB(skb)->csum_cnt--;
	} else {
		/* Update skb for CHECKSUM_UNNECESSARY and csum_level when we
		 * verified a new top level checksum or an encapsulated one
		 * during GRO. This saves work if we fallback to normal path.
		 */
		__skb_incr_checksum_unnecessary(skb);
	}
}

#define __skb_gro_checksum_validate(skb, proto, zero_okay, check,	\
				    compute_pseudo)			\
({									\
	__sum16 __ret = 0;						\
	if (__skb_gro_checksum_validate_needed(skb, zero_okay, check))	\
		__ret = __skb_gro_checksum_validate_complete(skb,	\
				compute_pseudo(skb, proto));		\
	if (!__ret)							\
		skb_gro_incr_csum_unnecessary(skb);			\
	__ret;								\
})

#define skb_gro_checksum_validate(skb, proto, compute_pseudo)		\
	__skb_gro_checksum_validate(skb, proto, false, 0, compute_pseudo)

#define skb_gro_checksum_validate_zero_check(skb, proto, check,		\
					     compute_pseudo)		\
	__skb_gro_checksum_validate(skb, proto, true, check, compute_pseudo)

#define skb_gro_checksum_simple_validate(skb)				\
	__skb_gro_checksum_validate(skb, 0, false, 0, null_compute_pseudo)

static inline bool __skb_gro_checksum_convert_check(struct sk_buff *skb)
{
	return (NAPI_GRO_CB(skb)->csum_cnt == 0 &&
		!NAPI_GRO_CB(skb)->csum_valid);
}

static inline void __skb_gro_checksum_convert(struct sk_buff *skb,
					      __wsum pseudo)
{
	NAPI_GRO_CB(skb)->csum = ~pseudo;
	NAPI_GRO_CB(skb)->csum_valid = 1;
}

#define skb_gro_checksum_try_convert(skb, proto, compute_pseudo)	\
do {									\
	if (__skb_gro_checksum_convert_check(skb))			\
		__skb_gro_checksum_convert(skb, 			\
					   compute_pseudo(skb, proto));	\
} while (0)

static inline void skb_gro_remcsum_init(struct gro_remcsum *grc)
{
	grc->offset = 0;
	grc->delta = 0;
}

static inline void *skb_gro_remcsum_process(struct sk_buff *skb, void *ptr,
					    unsigned int off, size_t hdrlen,
					    int start, int offset,
					    struct gro_remcsum *grc,
					    bool nopartial)
{
	__wsum delta;
	size_t plen = hdrlen + max_t(size_t, offset + sizeof(u16), start);

	BUG_ON(!NAPI_GRO_CB(skb)->csum_valid);

	if (!nopartial) {
		NAPI_GRO_CB(skb)->gro_remcsum_start = off + hdrlen + start;
		return ptr;
	}

	ptr = skb_gro_header_fast(skb, off);
	if (skb_gro_header_hard(skb, off + plen)) {
		ptr = skb_gro_header_slow(skb, off + plen, off);
		if (!ptr)
			return NULL;
	}

	delta = remcsum_adjust(ptr + hdrlen, NAPI_GRO_CB(skb)->csum,
			       start, offset);

	/* Adjust skb->csum since we changed the packet */
	NAPI_GRO_CB(skb)->csum = csum_add(NAPI_GRO_CB(skb)->csum, delta);

	grc->offset = off + hdrlen + offset;
	grc->delta = delta;

	return ptr;
}

static inline void skb_gro_remcsum_cleanup(struct sk_buff *skb,
					   struct gro_remcsum *grc)
{
	void *ptr;
	size_t plen = grc->offset + sizeof(u16);

	if (!grc->delta)
		return;

	ptr = skb_gro_header_fast(skb, grc->offset);
	if (skb_gro_header_hard(skb, grc->offset + sizeof(u16))) {
		ptr = skb_gro_header_slow(skb, plen, grc->offset);
		if (!ptr)
			return;
	}

	remcsum_unadjust((__sum16 *)ptr, grc->delta);
}

#ifdef CONFIG_XFRM_OFFLOAD
static inline void skb_gro_flush_final(struct sk_buff *skb, struct sk_buff *pp, int flush)
{
	if (PTR_ERR(pp) != -EINPROGRESS)
		NAPI_GRO_CB(skb)->flush |= flush;
}
static inline void skb_gro_flush_final_remcsum(struct sk_buff *skb,
					       struct sk_buff *pp,
					       int flush,
					       struct gro_remcsum *grc)
{
	if (PTR_ERR(pp) != -EINPROGRESS) {
		NAPI_GRO_CB(skb)->flush |= flush;
		skb_gro_remcsum_cleanup(skb, grc);
		skb->remcsum_offload = 0;
	}
}
#else
static inline void skb_gro_flush_final(struct sk_buff *skb, struct sk_buff *pp, int flush)
{
	NAPI_GRO_CB(skb)->flush |= flush;
}
static inline void skb_gro_flush_final_remcsum(struct sk_buff *skb,
					       struct sk_buff *pp,
					       int flush,
					       struct gro_remcsum *grc)
{
	NAPI_GRO_CB(skb)->flush |= flush;
	skb_gro_remcsum_cleanup(skb, grc);
	skb->remcsum_offload = 0;
}
#endif

/*
 *	netif_subqueue_stopped - test status of subqueue
 *	@dev: network device
 *	@skb: sub queue buffer pointer
 *
 * Check individual transmit queue of a device with multiple transmit queues.
 */
static inline bool netif_subqueue_stopped(const struct net_device *dev,
					  struct sk_buff *skb)
{
	return __netif_subqueue_stopped(dev, skb_get_queue_mapping(skb));
}

static inline void napi_free_frags(struct napi_struct *napi)
{
	kfree_skb(napi->skb);
	napi->skb = NULL;
}

int __dev_forward_skb(struct net_device *dev, struct sk_buff *skb);
int dev_forward_skb(struct net_device *dev, struct sk_buff *skb);
int dev_forward_skb_nomtu(struct net_device *dev, struct sk_buff *skb);
bool is_skb_forwardable(const struct net_device *dev,
			const struct sk_buff *skb);

static __always_inline bool __is_skb_forwardable(const struct net_device *dev,
						 const struct sk_buff *skb,
						 const bool check_mtu)
{
	const u32 vlan_hdr_len = 4; /* VLAN_HLEN */
	unsigned int len;

	if (!(dev->flags & IFF_UP))
		return false;

	if (!check_mtu)
		return true;

	len = dev->mtu + dev->hard_header_len + vlan_hdr_len;
	if (skb->len <= len)
		return true;

	/* if TSO is enabled, we don't care about the length as the packet
	 * could be forwarded without being segmented before
	 */
	if (skb_is_gso(skb))
		return true;

	return false;
}

static __always_inline int ____dev_forward_skb(struct net_device *dev,
					       struct sk_buff *skb,
					       const bool check_mtu)
{
	if (skb_orphan_frags(skb, GFP_ATOMIC) ||
	    unlikely(!__is_skb_forwardable(dev, skb, check_mtu))) {
		atomic_long_inc(&dev->rx_dropped);
		kfree_skb(skb);
		return NET_RX_DROP;
	}

	skb_scrub_packet(skb, !net_eq(dev_net(dev), dev_net(skb->dev)));
	skb->priority = 0;
	return 0;
}

static inline bool skb_gso_ok(struct sk_buff *skb, netdev_features_t features)
{
	return net_gso_ok(features, skb_shinfo(skb)->gso_type) &&
	       (!skb_has_frag_list(skb) || (features & NETIF_F_FRAGLIST));
}

static inline bool netif_needs_gso(struct sk_buff *skb,
				   netdev_features_t features)
{
	return skb_is_gso(skb) && (!skb_gso_ok(skb, features) ||
		unlikely((skb->ip_summed != CHECKSUM_PARTIAL) &&
			 (skb->ip_summed != CHECKSUM_UNNECESSARY)));
}

static inline void netif_set_gso_max_size(struct net_device *dev,
					  unsigned int size)
{
	dev->gso_max_size = size;
}

static inline void skb_gso_error_unwind(struct sk_buff *skb, __be16 protocol,
					int pulled_hlen, u16 mac_offset,
					int mac_len)
{
	skb->protocol = protocol;
	skb->encapsulation = 1;
	skb_push(skb, pulled_hlen);
	skb_reset_transport_header(skb);
	skb->mac_header = mac_offset;
	skb->network_header = skb->mac_header + mac_len;
	skb->mac_len = mac_len;
}

static inline const char *netdev_name(const struct net_device *dev)
{
	if (!dev->name[0] || strchr(dev->name, '%'))
		return "(unnamed net_device)";
	return dev->name;
}

/*
 * netdev_WARN() acts like dev_printk(), but with the key difference
 * of using a WARN/WARN_ON to get the message out, including the
 * file/line information and a backtrace.
 */
#define netdev_WARN(dev, format, args...)			\
	WARN(1, "netdevice: %s%s: " format, netdev_name(dev),	\
	     netdev_reg_state(dev), ##args)

#define netdev_WARN_ONCE(dev, format, args...)				\
	WARN_ONCE(1, "netdevice: %s%s: " format, netdev_name(dev),	\
		  netdev_reg_state(dev), ##args)

#define for_each_netdev_rcu(net, d)		\
		list_for_each_entry_rcu(d, &(net)->dev_base_head, dev_list)

/*
 * dev_addrs walker. Should be used only for read access. Call with
 * rcu_read_lock held.
 */
#define for_each_dev_addr(dev, ha) \
		list_for_each_entry_rcu(ha, &dev->dev_addrs.list, list)

#define for_each_netdev_continue_rcu(net, d)		\
	list_for_each_entry_continue_rcu(d, &(net)->dev_base_head, dev_list)

#define for_each_netdev_in_bond_rcu(bond, slave)	\
		for_each_netdev_rcu(&init_net, slave)	\
			if (netdev_master_upper_dev_get_rcu(slave) == (bond))

#define for_each_netdev(net, d)		\
		list_for_each_entry(d, &(net)->dev_base_head, dev_list)
#define for_each_netdev_reverse(net, d)	\
		list_for_each_entry_reverse(d, &(net)->dev_base_head, dev_list)
#define for_each_netdev_safe(net, d, n)	\
		list_for_each_entry_safe(d, n, &(net)->dev_base_head, dev_list)
#define for_each_netdev_continue(net, d)		\
		list_for_each_entry_continue(d, &(net)->dev_base_head, dev_list)
#define for_each_netdev_continue_reverse(net, d)		\
		list_for_each_entry_continue_reverse(d, &(net)->dev_base_head, \
						     dev_list)
#endif	/* _LINUX_NETDEVICE_API_EXTRA_H */
