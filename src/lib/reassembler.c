/*
  SPDX-License-Identifier: Apache-2.0
  Copyright (c) 2021 Nordix Foundation
*/

#include "reassembler.h"
#include <iputils.h>

#include <stddef.h>
#include <limits.h>
#include <netinet/ip.h>
#include <netinet/ip6.h>

#ifdef VERBOSE
#include <stdio.h>
#define Dx(x) x
#define D(x)
#else
#define Dx(x)
#define D(x)
#endif

/*
  https://datatracker.ietf.org/doc/html/rfc815

  The first part is used but not the second since we are not really
  reassemling the packet, just doing the book-keeping.
 */

static int handleFragment(
	struct Item* head, unsigned fragmentFirst, unsigned len, int morefragments);

#define INFINITY UINT_MAX
struct hole {
	unsigned first;
	unsigned last;
};
#define H(i) ((struct hole*)i->data)

static struct reassembler {
	struct ItemPool* holePool;
	struct ReassemblerStats stats;
} obj;

static void* raNew(void)
{
	D(printf("Called; raNew\n"));
	struct Item* hitem = itemAllocate(obj.holePool);
	if (hitem == NULL)
		return NULL;
	hitem->next = NULL;
	struct hole* h = H(hitem);
	h->first = 0;
	h->last = INFINITY;
	/*
	  The first item is a head-item. It's data is never used.
	 */
	return itemAllocateWithNext(obj.holePool, hitem);
}

static void raDestoy(void* r)
{
	D(printf("Called; raDestoy\n"));
	if (r == NULL)
		return;
	itemFree(r);
}

static int raHandleFragment(void* r, void const* data, unsigned len)
{
	Dx(printf("Called; raHandleFragment\n"));
	void const* endp = data + len;
	// Return 1 will fall-back to the default behavior
	if (r == NULL)
		return 1;
	switch (*((unsigned char const*)data) & 0xf0) {
	case 0x40: {
		// IPv4
		Dx(printf("Reassembler; IPv4 fragment\n"));
		struct iphdr* hdr = (struct iphdr*)data;
		if (!IN_BOUNDS(hdr, sizeof(*hdr), endp))
			return -1;
		if (len != ntohs(hdr->tot_len))
			return -1;
		unsigned payloadlen = len - hdr->ihl * sizeof(uint32_t);
		uint16_t frag_off = ntohs(hdr->frag_off);
		return handleFragment(
			r, (frag_off & IP_OFFMASK) * 8, payloadlen, frag_off & IP_MF);
	}
	case 0x60: {
		// IPv6
		Dx(printf("Reassembler; IPv6 fragment\n"));
		struct ip6_hdr* ip6hdr = (struct ip6_hdr*)data;
		uint8_t htype = ip6hdr->ip6_nxt;
		void const* hdr = data + sizeof(struct ip6_hdr);
		while (ipv6IsExtensionHeader(htype) && htype != IPPROTO_FRAGMENT) {
			struct ip6_ext const* xh = hdr;
			if (!IN_BOUNDS(xh, sizeof(*xh), endp))
				return -1;
			htype = xh->ip6e_nxt;
			if (xh->ip6e_len == 0)
				return -1;						/* Corrupt header */
			hdr = hdr + (xh->ip6e_len * 8);
		}
		if (htype == IPPROTO_FRAGMENT) {
			struct ip6_frag const* fh = hdr;
			if (!IN_BOUNDS(fh, sizeof(*fh), endp))
				return -1;
			uint16_t fragOffset = ntohs(fh->ip6f_offlg & IP6F_OFF_MASK);
			unsigned payloadlen = (endp - hdr) - sizeof(struct ip6_frag);
			return handleFragment(
				r, fragOffset, payloadlen, fh->ip6f_offlg & IP6F_MORE_FRAG);

		}
		break;
	}
	default:;
	}
	return 1;
}

struct FragReassembler* createReassembler(unsigned int size)
{
	Dx(printf("Called; createReassembler\n"));
	obj.holePool = itemPoolCreate(size, sizeof(struct hole), NULL);
	obj.stats.pool = itemPoolStats(obj.holePool);
	static struct FragReassembler ra = {raNew, raHandleFragment, raDestoy};
	return &ra;
}

struct ReassemblerStats const* getReassemblerStats(void)
{
	return &obj.stats;
}



/*
  From https://datatracker.ietf.org/doc/html/rfc815

   1. Select the next hole  descriptor  from  the  hole  descriptor
      list.  If there are no more entries, go to step eight.

   2. If fragment.first is greater than hole.last, go to step one.

   3. If fragment.last is less than hole.first, go to step one.

         - (If  either  step  two  or  step three is true, then the
           newly arrived fragment does not overlap with the hole in
           any way, so we need pay no  further  attention  to  this
           hole.  We return to the beginning of the algorithm where
           we select the next hole for examination.)


   4. Delete the current entry from the hole descriptor list.

         - (Since  neither  step  two  nor step three was true, the
           newly arrived fragment does interact with this  hole  in
           some  way.    Therefore,  the current descriptor will no
           longer be valid.  We will destroy it, and  in  the  next
           two  steps  we  will  determine  whether  or  not  it is
           necessary to create any new hole descriptors.)


   5. If fragment.first is greater than hole.first, then  create  a
      new  hole  descriptor "new_hole" with new_hole.first equal to
      hole.first, and new_hole.last equal to  fragment.first  minus
      one.

         - (If  the  test in step five is true, then the first part
           of the original hole is not filled by this fragment.  We
           create a new descriptor for this smaller hole.)


   6. If fragment.last is less  than  hole.last  and  fragment.more
      fragments   is  true,  then  create  a  new  hole  descriptor
      "new_hole", with new_hole.first equal to  fragment.last  plus
      one and new_hole.last equal to hole.last.

         - (This   test  is  the  mirror  of  step  five  with  one
           additional feature.  Initially, we did not know how long
           the reassembled datagram  would  be,  and  therefore  we
           created   a   hole   reaching  from  zero  to  infinity.
           Eventually, we will receive the  last  fragment  of  the
           datagram.    At  this  point, that hole descriptor which
           reaches from the last octet of the  buffer  to  infinity
           can  be discarded.  The fragment which contains the last
           fragment indicates this fact by a flag in  the  internet
           header called "more fragments".  The test of this bit in
           this  statement  prevents  us from creating a descriptor
           for the unneeded hole which describes the space from the
           end of the datagram to infinity.)


   7. Go to step one.

   8. If the hole descriptor list is now empty, the datagram is now
      complete.  Pass it on to the higher level protocol  processor
      for further handling.  Otherwise, return.

*/

/*
  Handle a fragment according to https://datatracker.ietf.org/doc/html/rfc815.
  Public to allow unit-test.
  return:
   0 - All fragments received
   1 - More fragments needed
 */
static int handleFragment(
	struct Item* head, unsigned fragmentFirst, unsigned len, int morefragments)
{
	D(printf("handleFragment: %u, len=%u, MF=%u\n", fragmentFirst, len, morefragments != 0));
	if (len == 0)
		return 1;
	unsigned fragmentLast = fragmentFirst + len -1;
	struct hole* hole;
	struct hole* new_hole;
	struct hole tmphole;
	struct Item* prev;
	struct Item* item = head;
	int itemDeleted = 0;
	for (;;) {
		// 1.
		if (itemDeleted) {
			// The current item is deleted. Unlink it
			D(printf("1. Unlink deleted item\n"));
			prev->next = item->next;
			item->next = NULL;
			itemFree(item);
			item = prev->next;
		} else {
			prev = item;
			item = item->next;
		}
		if (item == NULL)
			break;
		hole = H(item);
		// 2.
		if (fragmentFirst > hole->last)
			continue;
		// 3.
		if (fragmentLast < hole->first)
			continue;
		// 4.
		// We don't unlink the item yet since it can be re-used.
		// We must use the same hole in the next steps, so save it.
		D(printf("4. \n"));
		itemDeleted = 1;
		tmphole = *hole;
		hole = &tmphole;
		// 5.
		if (fragmentFirst > hole->first) {
			// Re-use the item
			D(printf("5. Re-use\n"));
			new_hole = H(item);
			new_hole->first = hole->first;
			new_hole->last = fragmentFirst - 1;
			itemDeleted = 0;
		}
		// 6.
		if (fragmentLast < hole->last && morefragments) {
			if (!itemDeleted) {
				// Item already re-used, create a new
				D(printf("6. New item\n"));
				struct Item* newItem = itemAllocate(obj.holePool);
				if (newItem == NULL) {
					// TODO
				}
				newItem->next = item->next;
				item->next = newItem;
				prev = newItem;
			} else {
				D(printf("6. Re-use\n"));
				itemDeleted = 0;
			}
			new_hole = H(item);
			new_hole->first = fragmentLast + 1;
			new_hole->last = hole->last;
		}
	}

	return head->next == NULL ? 0 : 1;
}
