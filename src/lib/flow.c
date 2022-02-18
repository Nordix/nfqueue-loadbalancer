/*
  SPDX-License-Identifier: Apache-2.0
  Copyright (c) 2021-2022 Nordix Foundation
*/

#include <flow.h>
#include <die.h>
#include <rangeset.h>
#include <match.h>
#include <stdlib.h>
#include <string.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <limits.h>

#define D(x)
#define Dx(x) x

// Limits
#define MAX_NAME 1024
#define MAX_CIDRS 32

#define MALLOC(x) calloc(1, sizeof(*(x))); if (x == NULL) die("OOM")
#define CALLOC(n,x) calloc(n, sizeof(*(x))); if (x == NULL) die("OOM")
#define CNTINC(x) __atomic_add_fetch(&(x),1,__ATOMIC_RELAXED)

struct Cidr {
	struct in6_addr adr;
	uint64_t mask[2];
};

struct Flow {
	char* name;
	int priority;
	void* user_ref;
	unsigned short* protocols;	/* zero terminated */
	struct RangeSet* dports;
	struct RangeSet* sports;
	unsigned ndsts; struct Cidr* dsts;
	unsigned nsrcs; struct Cidr* srcs;
	struct Match* match;
	unsigned short udpencap;
	unsigned match_count;
};

struct FlowSet {
	unsigned count;
	struct Flow** flows;		/* null terminated */
	void (*lock_user_ref)(void* user_ref);
	int promiscuous_ping;
	pthread_rwlock_t lock;
};
#define RLOCK(set) if (pthread_rwlock_rdlock(&set->lock) != 0) \
		die("pthread_rwlock_rdlock")
#define WLOCK(set) if (pthread_rwlock_wrlock(&set->lock) != 0) \
		die("pthread_rwlock_wrlock")
#define UNLOCK(set) if (pthread_rwlock_unlock(&set->lock) != 0) \
		die("pthread_rwlock_unlock")

struct FlowSet* flowSetCreate(void (*lock_user_ref)(void* user_ref))
{
	struct FlowSet* set = MALLOC(set);
	set->flows = MALLOC(set->flows);
	if (pthread_rwlock_init(&set->lock, NULL) != 0)
		die("pthread_rwlock_init");
	set->lock_user_ref = lock_user_ref;
	return set;
}
void flowSetPromiscuousPing(struct FlowSet* set, int value)
{
	set->promiscuous_ping = value;
}
static void flowFree(struct Flow* f)
{
	if (f == NULL)
		return;
	free(f->protocols);
	free(f->dsts);
	free(f->srcs);
	rangeSetDestroy(f->dports);
	rangeSetDestroy(f->sports);
	matchDestroy(f->match);
	free(f->name);
	free(f);
}

void flowSetDelete(struct FlowSet* set)
{
	if (set == NULL)
		return;
	for (unsigned i = 0; i < set->count; i++)
		flowFree(set->flows[i]);
	free(set->flows);
	if (pthread_rwlock_destroy(&set->lock) != 0)
		die("pthread_rwlock_destroy");

	free(set);
}

unsigned flowSetSize(struct FlowSet* set)
{
	return set->count;
}

// For priority qsort
static int cmpFlows(const void* a, const void* b)
{
	struct Flow* const* f1 = a;
	struct Flow* const* f2 = b;
	return (*f2)->priority - (*f1)->priority;
}

static unsigned short* parseProtocol(char const* argv[])
{
	if (argv == NULL)
		return 0;
	// We accept repeats and any case
	unsigned protobits = 0; // 1-tcp, 2-udp, 4-sctp
	while (*argv != NULL) {
		if (strcasecmp("tcp", *argv) == 0)
			protobits |= 1;
		else if (strcasecmp("udp", *argv) == 0)
			protobits |= 2;
		else if (strcasecmp("sctp", *argv) == 0)
			protobits |= 4;
		else
			return NULL;
		argv++;
	}
	unsigned short* protocols = CALLOC(4,protocols);
	unsigned i = 0;
	if (protobits & 1)
		protocols[i++] = IPPROTO_TCP;
	if (protobits & 2)
		protocols[i++] = IPPROTO_UDP;
	if (protobits & 4)
		protocols[i++] = IPPROTO_SCTP;
	return protocols;
}

static inline void maskAdr(struct in6_addr* adr, uint64_t* mask)
{
	uint64_t* adrw = (uint64_t*)adr;
	adrw[0] &= mask[0];
	adrw[1] &= mask[1];
}

static struct Cidr* parseCidrs(
	char const* argv[], /*out*/unsigned* cnt)
{
	*cnt = 0;
	if (argv == NULL)
		return NULL;
	unsigned len = 0;
	for (char const** a = argv; *a != NULL; a++)
		len++;
	if (len == 0)
		return NULL;
	if (len > MAX_CIDRS)
		return NULL;
	struct Cidr* c = CALLOC(len,c);
	for (unsigned i = 0; i < len; i++) {
		char s[128];
		int isIpv4 = 0;
		if (strchr(argv[i], ':') == NULL) {
			// IPv4 address. Pre-pend "::ffff:"
			snprintf(s, sizeof(s), "::ffff:%s", argv[i]);
			isIpv4 = 1;
		} else {
			strncpy(s, argv[i], sizeof(s));
		}
		char* _mask = strchr(s, '/');
		if (_mask == NULL)
			goto bailout;
		*_mask++ = 0;
		unsigned mask;
		if (sscanf(_mask, "%u", &mask) != 1)
			goto bailout;
		if (isIpv4)
			mask += 96;
		if (mask > 128)
			goto bailout;
		if (inet_pton(AF_INET6, s, &(c[i].adr)) != 1)
			goto bailout;
		// Compute the bit-mask
		if (mask > 64) {
			c[i].mask[0] = UINT64_MAX;
			c[i].mask[1] = htobe64(UINT64_MAX << (128 - mask));
		} else {
			c[i].mask[0] = htobe64(UINT64_MAX << (64 - mask));
			c[i].mask[1] = 0;
		}
		// Apply the mask on the address
		maskAdr(&(c[i].adr), c[i].mask);
	}
	*cnt = len;
	return c;
bailout:
	free(c);
	return NULL;
}

// Add or replace a flow
char const* flowDefine(
	struct FlowSet* set,
	char const* name,
	int priority,
	void* user_ref,
	char const* protocols[],
	char const* dports,
	char const* sports,
	char const* dsts[],
	char const* srcs[],
	char const* match[],
	unsigned short udpencap)
{
#define BAILOUT(args...) {snprintf(err, sizeof(err), args); goto bailout;}
	static char err[128];
	*err = 0;					/* Clear previous error */
	if (name == NULL)
		return "No name";
	if (strlen(name) >= MAX_NAME)
		return "Name too long";

	struct Flow* f = MALLOC(f);

	f->priority = priority;
	f->user_ref = user_ref;
	f->udpencap = udpencap;
	if (protocols != NULL) {
		f->protocols = parseProtocol(protocols);
		if (f->protocols == NULL)
			BAILOUT("Invalid protocols");
	}
	if (dports != NULL) {
		D(printf("dports %s\n", dports));
		f->dports = rangeSetCreateLimited(1, USHRT_MAX);
		if (rangeSetAddStr(f->dports, dports) != 0)
			BAILOUT("Invalid dports");
		rangeSetUpdate(f->dports);
	}
	if (sports != NULL) {
		f->sports = rangeSetCreateLimited(1, USHRT_MAX);
		if (rangeSetAddStr(f->sports, sports) != 0)
			BAILOUT("Invalid dports");
		rangeSetUpdate(f->sports);
	}

	if (dsts != NULL) {
		f->dsts = parseCidrs(dsts, &f->ndsts);
		if (f->dsts == NULL)
			BAILOUT("Invalid dsts");
	}
	if (srcs != NULL) {
		f->srcs = parseCidrs(srcs, &f->nsrcs);
		if (f->srcs == NULL)
			BAILOUT("Invalid srcs");
	}

	if (match != NULL) {
		f->match = matchCreate();
		while (*match != NULL) {
			char const* merr = matchAdd(f->match, *match);
			if (merr != NULL)
				BAILOUT("Match [%s] %s", *match, merr);
			match++;
		}
	}

	f->name = strndup(name, MAX_NAME);
	if (f->name == NULL)
		die("OOM");
	
	WLOCK(set);

	// Insert the new flow or update an existing one.
	// This must be done while holding the write lock.
	struct Flow* updatedf = NULL;
	unsigned i;
	for (i = 0; i < set->count; i++) {
		if (strncmp(set->flows[i]->name, name, MAX_NAME) == 0) {
			// Update existing flow
			updatedf = set->flows[i];
			break;
		}
	}

	if (updatedf != NULL) {
		// Update
		set->flows[i] = f;
		flowFree(updatedf);
	} else {
		// New flow. Extend the flow array.
		set->flows[set->count++] = f; /* NULL termination overwritten */
		set->flows = realloc(set->flows, (set->count+1) * sizeof(struct Flow*));
		if (set->flows == NULL)
			die("OOM");
		set->flows[set->count] = NULL; /* Restore NULL termination */
	}

	// Sort on priority
	qsort(set->flows, set->count, sizeof(struct Flow*), cmpFlows);

	UNLOCK(set);
	return 0;
bailout:
	flowFree(f);
	return err;;
}

// Delete a flow
void* flowDelete(
	struct FlowSet* set, char const* name, /*out*/unsigned short* udpencap)
{
	void* user_ref = NULL;
	WLOCK(set);
	for (unsigned i = 0; i < set->count; i++) {
		if (strncmp(set->flows[i]->name, name, MAX_NAME) == 0) {
			user_ref = set->flows[i]->user_ref;
			if (udpencap != NULL)
				*udpencap = set->flows[i]->udpencap;
			flowFree(set->flows[i]);
			// Pack the array
			for (unsigned j = i; (j + 1) < set->count; j++)
				set->flows[j] = set->flows[j + 1];
			set->count--;
			// We must restore the null termination. (caused a segv)
			set->flows[set->count] = NULL;
			break;
		}
	}
	UNLOCK(set);
	return user_ref;
}

static int addrInCidr(struct Cidr* cidr, struct in6_addr adr)
{
	maskAdr(&adr, cidr->mask);
	return IN6_ARE_ADDR_EQUAL(&(cidr->adr), &adr);
}

static void* flowMatch(
	struct ctKey* key,
	struct Flow* f,
	int promiscuous_ping,
	unsigned l3proto, void const* data, unsigned len,
	unsigned short* udpencap)
{
	int found;

	if (f->dsts != NULL) {
		found = 0;
		for (unsigned i = 0; i < f->ndsts; i++) {
			if (addrInCidr(f->dsts + i, key->dst)) {
				found = 1;
				break;
			}
		}
		if (!found)
			return NULL;
	}
	if (f->srcs != NULL) {
		found = 0;
		for (unsigned i = 0; i < f->nsrcs; i++) {
			if (addrInCidr(f->srcs + i, key->src)) {
				found = 1;
				break;
			}
		}
		if (!found)
			return NULL;
	}
	if (promiscuous_ping) {
		// Ping will match any flow with an address match
		if (key->ports.proto == IPPROTO_ICMP || key->ports.proto == IPPROTO_ICMPV6)
			if (key->id != 0) {
				CNTINC(f->match_count);
				return f->user_ref;
			}
	}
	if (f->protocols != NULL) {
		found = 0;
		for (unsigned short* p = f->protocols; *p != 0; p++) {
			if (key->ports.proto == *p) {
				found = 1;
				break;
			}
		}
		if (!found)
			return NULL;
	}
	if (f->dports != NULL) {
		if (!rangeSetIn(f->dports, ntohs(key->ports.dst)))
			return NULL;
	}
	if (f->sports != NULL) {
		if (!rangeSetIn(f->sports, ntohs(key->ports.src)))
			return NULL;
	}
	if (f->match != NULL && data != NULL && len > 0) {
		if (!matchMatches(f->match, l3proto, key->ports.proto, data, len))
			return NULL;
	}

	// We have a match
	if (udpencap != NULL)
		*udpencap = f->udpencap;
	CNTINC(f->match_count);
	return f->user_ref;
}

// Lookup a key. Returns the "user_ref" if found, NULL if not.
void* flowLookup(
	struct FlowSet* set,
	struct ctKey* key,
	unsigned l3proto, void const* data, unsigned len, /* (for byte-match) */
	unsigned short* udpencap)
{
	RLOCK(set);
	struct Flow** fp = set->flows;
	while (*fp != NULL) {
		void* user_ref = flowMatch(
			key, *fp, set->promiscuous_ping, l3proto, data, len, udpencap);
		if (user_ref != NULL) {
			if (set->lock_user_ref != NULL)
				set->lock_user_ref(user_ref);
			UNLOCK(set);
			return user_ref;
		}
		fp++;
	}
	UNLOCK(set);
	return NULL;
}

static unsigned leadingones(uint64_t x)
{
	if (x == UINT64_MAX)
		return 64;
	x = be64toh(x);
	unsigned n = 0;
	while (x & 0x8000000000000000) {
		n++;
		x= x << 1;
	}
	return n;
}
static void printCidr(FILE* out, struct Cidr* c)
{
	char addr[INET6_ADDRSTRLEN];
	inet_ntop(AF_INET6, &c->adr, addr, INET6_ADDRSTRLEN);
	unsigned mask = leadingones(c->mask[0]) + leadingones(c->mask[1]);
	fprintf(out, "    \"%s/%u\"", addr, mask);
}
static void printCidrs(FILE* out, unsigned n, struct Cidr* c)
{
	while (n--) {
		printCidr(out, c++);
		if (n > 0)
			fprintf(out, ",\n");
	}
}

static void printPorts(FILE* out, struct RangeSet* ports)
{
	char buf[1024];
	if (rangeSetString(ports, buf, sizeof(buf)) < 0)
		return;
	char* tok = strtok(buf, ", ");
	while (tok != NULL) {
		fprintf(out, "    \"%s\"", tok);
		tok = strtok(NULL, ", ");
		if (tok != NULL)
			fprintf(out, ",\n");
	}
}

static char const* protostr(unsigned short p)
{
	switch (p) {
	case IPPROTO_TCP: return "tcp";
	case IPPROTO_UDP: return "udp";
	case IPPROTO_SCTP: return "sctp";
	default: return "";
	}
}
static void printProto(FILE* out, unsigned short const* p)
{
	fprintf(out, "  \"protocols\": [ \"%s\"", protostr(*p++));
	while (*p != 0) {
		fprintf(out, ", \"%s\"", protostr(*p++));
	}
	fprintf(out, " ]");
}

static void printMatch(FILE* out, struct Match* m)
{
	char buf[1024];
	if (matchString(m, buf, sizeof(buf)) < 0)
		return;
	char* tok = strtok(buf, ",");
	while (tok != NULL) {
		fprintf(out, "    \"%s\"", tok);
		tok = strtok(NULL, ", ");
		if (tok != NULL)
			fprintf(out, ",\n");
	}
}

static void printFlow(
	FILE* out, struct Flow* f,
	char const* (*user_ref2string)(void* user_ref))
{
	fprintf(out, "{\n");
	fprintf(out, "  \"name\": \"%s\",\n", f->name);
	fprintf(out, "  \"priority\": %u", f->priority);
	if (f->protocols != NULL) {
		fprintf(out, ",\n");
		printProto(out, f->protocols);
	}
	if (f->dsts != NULL) {
		fprintf(out, ",\n");
		fprintf(out, "  \"dests\": [\n");
		printCidrs(out, f->ndsts, f->dsts);
		fprintf(out, "\n  ]");
	}
	if (f->dports != NULL) {
		fprintf(out, ",\n");
		fprintf(out, "  \"dports\": [\n");
		printPorts(out, f->dports);
		fprintf(out, "\n  ]");
	}
	if (f->srcs != NULL) {
		fprintf(out, ",\n");
		fprintf(out, "  \"srcs\": [\n");
		printCidrs(out, f->nsrcs, f->srcs);
		fprintf(out, "\n  ]");
	}
	if (f->sports != NULL) {
		fprintf(out, ",\n");
		fprintf(out, "  \"sports\": [\n");
		printPorts(out, f->sports);
		fprintf(out, "\n  ]");
	}
	if (f->match != NULL) {
		fprintf(out, ",\n");
		fprintf(out, "  \"match\": [\n");
		printMatch(out, f->match);
		fprintf(out, "\n  ]");
	}
	if (f->udpencap > 0) {
		fprintf(out, ",\n");
		fprintf(out, "  \"udpencap\": %u", f->udpencap);
	}
	fprintf(out, ",\n  \"matches_count\": %u", f->match_count);
	if (user_ref2string != NULL) {
		fprintf(out, ",\n");
		fprintf(out, "  \"user_ref\": \"%s\"", user_ref2string(f->user_ref));
	}
	fprintf(out, "\n}");
}

void flowSetPrint(
	FILE* out, struct FlowSet* set, char const* name,
	char const* (*user_ref2string)(void* user_ref))
{
	RLOCK(set);
	if (name == NULL) {
		fprintf(out, "[");
		for (unsigned i = 0; i < set->count; i++) {
			printFlow(out, set->flows[i], user_ref2string);
			if ((i + 1) < set->count)
				fprintf(out, ",\n");
		}
		fprintf(out, "]\n");
	} else {
		for (unsigned i = 0; i < set->count; i++) {
			if (strcmp(name, set->flows[i]->name) == 0) {
				printFlow(out, set->flows[i], user_ref2string);
				break;
			}
		}

	}
	UNLOCK(set);
}

void flowSetPrintNames(FILE* out, struct FlowSet* set)
{
	RLOCK(set);
	if (set->count == 0) {
		UNLOCK(set);
		fprintf(out, "[]\n");
		return;
	}
	fprintf(out, "[\n");
	for (unsigned i = 0; i < set->count; i++) {
		if (i == 0)
			fprintf(out, "  \"%s\"", set->flows[i]->name);
		else
			fprintf(out, ",\n  \"%s\"", set->flows[i]->name);
	}
	fprintf(out, "\n]\n");
	UNLOCK(set);
}

// White-box testing
#ifdef UNIT_TEST
int flowSetIsSorted(struct FlowSet* set)
{
	for (unsigned i = 0; (i+1) < set->count; i++) {
		if (set->flows[i]->priority < set->flows[i+1]->priority)
			return 0;
	}
	return 1;
}
static int cidrEqual(struct Cidr* c1, struct Cidr* c2)
{
	if (c1->mask[0] != c2->mask[0])
		return 0;
	if (c1->mask[1] != c2->mask[1])
		return 0;
	if (!IN6_ARE_ADDR_EQUAL(&c1->adr, &c2->adr))
		return 0;
	return 1;
}
int flowSetEqual(struct FlowSet* f1, struct FlowSet* f2)
{
	if (f1->count != f2->count)
		return 0;
	if (f1->promiscuous_ping != f2->promiscuous_ping)
		return 0;

	unsigned i;
	char s1[1024];
	char s2[1024];
	for (i = 0; i < f1->count; i++) {
		struct Flow* fl1 = f1->flows[i];
		struct Flow* fl2 = f2->flows[i];
		if (strcmp(fl1->name, fl2->name) != 0)
			return 0;
		if (fl1->ndsts != fl2->ndsts)
			return 0;
		if (fl1->nsrcs != fl2->nsrcs)
			return 0;
		for (unsigned J = 0; J < fl1->ndsts; J++) {
			if (!cidrEqual(&fl1->dsts[J], &fl2->dsts[J]))
				return 0;
		}
		for (unsigned J = 0; J < fl1->nsrcs; J++) {
			if (!cidrEqual(&fl1->srcs[J], &fl2->srcs[J]))
				return 0;
		}
		rangeSetString(fl1->dports, s1, sizeof(s1)); 
		rangeSetString(fl2->dports, s2, sizeof(s2));
		if (strcmp(s1, s2) != 0)
			return 0;
		rangeSetString(fl1->sports, s1, sizeof(s1)); 
		rangeSetString(fl2->sports, s2, sizeof(s2));
		if (strcmp(s1, s2) != 0)
			return 0;
		matchString(fl1->match, s1, sizeof(s1));
		matchString(fl2->match, s2, sizeof(s2));
		if (strcmp(s1, s2) != 0)
			return 0;
	}
	return 1;
}
#endif
