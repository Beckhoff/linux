/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef _IP_API_GRO_H
#define _IP_API_GRO_H

#include <net/ip.h>

#include <linux/netdevice_api.h>

static inline __wsum inet_gro_compute_pseudo(struct sk_buff *skb, int proto)
{
	const struct iphdr *iph = skb_gro_network_header(skb);

	return csum_tcpudp_nofold(iph->saddr, iph->daddr,
				  skb_gro_len(skb), proto, 0);
}

#endif	/* _IP_API_GRO_H */
