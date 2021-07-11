/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_MM_API_KASAN_H
#define _LINUX_MM_API_KASAN_H

#ifndef __ASSEMBLY__

#include <linux/mm_types.h>

#if defined(CONFIG_KASAN_SW_TAGS) || defined(CONFIG_KASAN_HW_TAGS)

/*
 * KASAN per-page tags are stored xor'ed with 0xff. This allows to avoid
 * setting tags for all pages to native kernel tag value 0xff, as the default
 * value 0x00 maps to 0xff.
 */

static inline u8 page_kasan_tag(const struct page *page)
{
	u8 tag = 0xff;

	if (kasan_enabled()) {
		tag = (page->flags >> KASAN_TAG_PGSHIFT) & KASAN_TAG_MASK;
		tag ^= 0xff;
	}

	return tag;
}

static inline void page_kasan_tag_set(struct page *page, u8 tag)
{
	if (kasan_enabled()) {
		tag ^= 0xff;
		page->flags &= ~(KASAN_TAG_MASK << KASAN_TAG_PGSHIFT);
		page->flags |= (tag & KASAN_TAG_MASK) << KASAN_TAG_PGSHIFT;
	}
}

static inline void page_kasan_tag_reset(struct page *page)
{
	if (kasan_enabled())
		page_kasan_tag_set(page, 0xff);
}

#else /* CONFIG_KASAN_SW_TAGS || CONFIG_KASAN_HW_TAGS */

static inline u8 page_kasan_tag(const struct page *page)
{
	return 0xff;
}

static inline void page_kasan_tag_set(struct page *page, u8 tag) { }
static inline void page_kasan_tag_reset(struct page *page) { }

#endif /* CONFIG_KASAN_SW_TAGS || CONFIG_KASAN_HW_TAGS */

#endif /* !__ASSEMBLY__ */

#endif /* _LINUX_MM_API_KASAN_H */
