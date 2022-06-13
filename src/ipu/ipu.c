
#define _GNU_SOURCE
#include <cmd.h>
#include <die.h>

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <arpa/inet.h>
#include <assert.h>

static int cmdMakeip(int argc, char* argv[])
{
	char const* opt_cidr = NULL;
	char const* opt_net = "0";
	char const* opt_host = "0";
	char const* opt_subnet = NULL;
	char const* opt_ipv6prefix = NULL;
	struct Option options[] = {
		{"help", NULL, 0,
		 "makeip [options]\n"
		 "  Print an address as a combination of a base cidr in the form\n"
		 "  192.168.128.0/17/24 plus net and host numbers"
		},
		{"cidr", &opt_cidr, 1, "CIDR in double-slash notation"},
		{"net", &opt_net, 0, "Net number. Will go into the first slot in the CIDR"},
		{"host", &opt_host, 0, "Host number. Will go into the second slot in the CIDR"},
		{"subnet", &opt_subnet, 0, "Print subnet. Values=1,2"},
		{"ipv6prefix", &opt_ipv6prefix, 0, "Print as ipv6 with the passed prefix"},
		{0, 0, 0, 0}
	};
	(void)parseOptionsOrDie(argc, argv, options);

	if (strlen(opt_cidr) > INET6_ADDRSTRLEN + 6)
		die("Too long cidr [%s]\n", opt_cidr);
	char* cidr = strdupa(opt_cidr);
	char* cp = strchr(cidr, '/');
	if (cp == 0)
		die("Invalid cidr [%s]\n", opt_cidr);
	int s1 = strtol(cp+1, NULL, 0);
	*cp = 0;					/* null terminate the address */
	cp = strchr(cp + 1, '/');
	int s2;
	if (cp == 0) {
		s2 = s1;
	} else {
		s2 = strtol(cp+1, NULL, 0);
	}

	int bits = strchr(cidr, ':') != NULL ? 128 : 32;
	int host = atoi(opt_host);
	int net = atoi(opt_net);
	unsigned max;
	char strbuf[INET6_ADDRSTRLEN+1];

	if (bits == 128 && s1 < 97)
		die("Can only handle /97 subnets and larger\n");
	if (bits == 128 && opt_ipv6prefix != NULL)
		die("Can't combine ipv6prefix and IPv6 addresses\n");
	if (s1 > s2 || s2 > bits || s1 <= 0)
		die("Invalid cidr [%s]\n", opt_cidr);
	max = 1 << (s2 - s1);
	if (net >= max)
		die("Net too large [%d]\n", net);
	max = 1 << (bits - s2);
	if (host >= max)
		die("Host too large [%d]\n", host);

	uint32_t mask = ~((1 << (bits - s1)) - 1);

	if (bits == 32) {
		// IPv4
		struct in_addr addr;
		if (inet_pton(AF_INET, cidr, &addr) != 1)
			die("Invalid address [%s]\n", cidr);

		uint32_t a = ntohl(addr.s_addr);
		a = (a & mask) + (net << (bits - s2)) + host;

		addr.s_addr = htonl(a);
		if (opt_ipv6prefix != NULL) {
			struct in6_addr a6;
			if (inet_pton(AF_INET6, opt_ipv6prefix, &a6) != 1)
				die("Invalid IPv6 prefix [%s]\n", opt_ipv6prefix);
			a6.s6_addr32[3] = addr.s_addr;
			inet_ntop(AF_INET6, &a6, strbuf, sizeof(strbuf));
			if (opt_subnet != NULL) {
				s1 += 96;
				s2 += 96;
				printf("%s/%d\n", strbuf, atoi(opt_subnet) == 1 ? s1 : s2);
			} else {
				printf("%s\n", strbuf);
			}
		} else {
			inet_ntop(AF_INET, &addr, strbuf, sizeof(strbuf));
			if (opt_subnet != NULL) {
				printf("%s/%d\n", strbuf, atoi(opt_subnet) == 1 ? s1 : s2);
			} else {
				printf("%s\n", strbuf);
			}
		}
	} else {
		// IPv6
		struct in6_addr addr;
		if (inet_pton(AF_INET6, cidr, &addr) != 1)
			die("Invalid address [%s]\n", cidr);

		uint32_t a = ntohl(addr.s6_addr32[3]);
		a = (a & mask) + (net << (bits - s2)) + host;
		addr.s6_addr32[3] = htonl(a);
		inet_ntop(AF_INET6, &addr, strbuf, sizeof(strbuf));
		if (opt_subnet != NULL) {
			printf("%s/%d\n", strbuf, atoi(opt_subnet) == 1 ? s1 : s2);
		} else {
			printf("%s\n", strbuf);
		}
	}
	return 0;
}

// Clear all bits above the prefix
static void mask_ipv6(struct in6_addr* a, unsigned prefix)
{
	assert(prefix <= 128);
	unsigned bits_to_clear = 128 - prefix;
	unsigned i = 3;
	while (bits_to_clear >= 32) {
		a->s6_addr32[i] = 0;
		bits_to_clear -= 32;
		i--;
	}
	if (bits_to_clear == 0)
		return;
	uint32_t mask = htonl(~((1 << bits_to_clear) - 1));
	a->s6_addr32[i] &= mask;
}

static int cmdContains(int argc, char* argv[])
{
	char const* opt_cidr = NULL;
	struct Option options[] = {
		{"help", NULL, 0,
		 "contains --cidr= address\n"
		 "  Returns OK if the passed address is within the CIDR"
		},
		{"cidr", &opt_cidr, 1, "CIDR"},
		{0, 0, 0, 0}
	};
	int n = parseOptionsOrDie(argc, argv, options);
	argc -= n; argv += n;
	if (argc < 1)
		die("No address\n");
	
	char* cidr = strdupa(opt_cidr);
	char* cp = strchr(cidr, '/');
	if (cp == NULL)
		die("Invalid cidr\n");
	*cp++ = 0;
	int s = atoi(cp);
	if (s == 0)
		return 0;				/* anything goes */

	if (strchr(cidr, ':') != NULL) {
		// IPv6
		if (strchr(*argv, ':') == NULL)
			return 1;			/* Different families */
		if (s < 0 || s > 128)
			die("Invalid mask\n");
		struct in6_addr a;
		if (inet_pton(AF_INET6, *argv, &a) != 1)
				die("Invalid address [%s]\n", *argv);
		struct in6_addr c;
		if (inet_pton(AF_INET6, cidr, &c) != 1)
				die("Invalid cidr [%s]\n", cidr);
		mask_ipv6(&a, s);
		mask_ipv6(&c, s);
		if (!IN6_ARE_ADDR_EQUAL(&a, &c))
			return 1;
	} else {
		// IPv4
		if (strchr(*argv, ':') != NULL)
			return 1;			/* Different families */
		if (s < 0 || s > 32)
			die("Invalid mask\n");
		struct in_addr addr;
		if (inet_pton(AF_INET, *argv, &addr) != 1)
			die("Invalid address [%s]\n", *argv);
		struct in_addr c;
		if (inet_pton(AF_INET, cidr, &c) != 1)
			die("Invalid cidr [%s]\n", opt_cidr);
		uint32_t mask = htonl(~((1 << (32 - s)) - 1));
		if ((addr.s_addr & mask) != (c.s_addr & mask))
			return 1;

	}
	return 0;
}

#ifndef VERSION
#define VERSION unknown
#endif
#define xstr(s) str(s)
#define str(s) #s

static int cmdVersion(int argc, char **argv)
{
	struct Option options[] = {
		{"help", NULL, 0,
		 "version\n"
		 "  Print version"},
		{0, 0, 0, 0}
	};
	parseOptionsOrDie(argc, argv, options);
	printf("nfqlb-%s\n", xstr(VERSION));
	return 0;
}

__attribute__ ((__constructor__)) static void addCommands(void) {
	addCmd("makeip", cmdMakeip);
	addCmd("contains", cmdContains);
	addCmd("version", cmdVersion);
}
