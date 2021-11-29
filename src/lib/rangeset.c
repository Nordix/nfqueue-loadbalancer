/*
  SPDX-License-Identifier: Apache-2.0
  Copyright (c) 2021 Nordix Foundation
*/

/*
  Recursive functions are used. The tree is balanced so depth<=log(n).
  The intended use is for 16-bit ports, which mean max 16 recursions.
 */

#include <rangeset.h>
#include <die.h>
#define _GNU_SOURCE				/* (for strndupa) */
#include <stdlib.h>
#include <stdio.h>
#include <stddef.h>
#include <limits.h>
#include <string.h>

#ifndef UNIT_TEST
#define NDEBUG
#endif
#include <assert.h>

#define Dx(x) x
#define D(x)

struct Node {
	unsigned first, last;
	struct Node* left;
	struct Node* right;
};

struct RangeSet {
	unsigned count;
	unsigned lowest;
	unsigned highest;
	struct Node* root;
	struct Node* added;
};

struct RangeSet* rangeSetCreate(void)
{
	struct RangeSet* t = calloc(1, sizeof(struct RangeSet));
	if (t == NULL)
		die("OOM");
	t->highest = UINT_MAX;
	return t;
}
struct RangeSet* rangeSetCreateLimited(unsigned lowest, unsigned highest)
{
	if (lowest >= highest)
		return NULL;
	struct RangeSet* t = calloc(1, sizeof(struct RangeSet));
	if (t == NULL)
		die("OOM");
	t->lowest = lowest;
	t->highest = highest;
	return t;
}
static void treeFree(struct Node* n)
{
	if (n == NULL)
		return;
	treeFree(n->left);
	treeFree(n->right);
	free(n);
}
void rangeSetDestroy(struct RangeSet* t)
{
	if (t == NULL)
		return;
	treeFree(t->root);
	treeFree(t->added);
	free(t);
}

static int isInTree(struct Node* n, unsigned value)
{
	if (n == NULL)
		return 0;
	if (value < n->first)
		return isInTree(n->left, value);
	if (value > n->last)
		return isInTree(n->right, value);
	return 1;
}

int rangeSetIn(struct RangeSet* t, unsigned value)
{
	return isInTree(t->root, value);
}

int rangeSetAdd(struct RangeSet* t, unsigned first, unsigned last)
{
	if (last < first)
		return -1;
	if (t->lowest > 0 && first < t->lowest)
		return -1;
	if (t->highest < UINT_MAX && last > t->highest)
		return -1;

	struct Node* n = calloc(1, sizeof(struct Node));
	if (n == NULL)
		die("OOM");
	n->first = first;
	n->last = last;
	n->left = t->added;
	t->added = n;
	t->count++;
	return 0;
}

static int rangeSetAddOneStr(struct RangeSet* t, char const* tok)
{
	unsigned first, last;
	if (strchr(tok, '-')) {
		if (sscanf(tok, "%u-%u", &first, &last) != 2)
			return -1;
		if (last < first)
			return -1;
	} else {
		if (sscanf(tok, "%u", &first) != 1)
			return -1;
		last = first;
	}
	if (rangeSetAdd(t, first, last) != 0)
		return -1;
	return 0;
}
int rangeSetAddStr(struct RangeSet* t, char const* _str)
{
	char* str = strndupa(_str, 1024);
	char* tok = strtok(str, ", ");
	while (tok != NULL) {
		if (rangeSetAddOneStr(t, tok) != 0)
			return -1;
		tok = strtok(NULL, ", ");
	}
	return 0;
}

int rangeSetAddArgv(struct RangeSet* t, char const* argv[])
{
	if (argv == NULL)
		return 0;
	while (*argv != NULL) {
		if (rangeSetAddOneStr(t, *argv) != 0)
			return -1;
		argv++;
	}
	return 0;
}

unsigned rangeSetSize(struct RangeSet* t)
{
	return t->count;
}

static void insertNode(struct Node* parent, struct Node* n)
{
	if (n->first < parent->first) {
		if (parent->left == NULL) {
			n->left = n->right = NULL;
			parent->left = n;
		} else
			insertNode(parent->left, n);
	} else {
		if (parent->right == NULL) {
			n->left = n->right = NULL;
			parent->right = n;
		} else
			insertNode(parent->right, n);
	}
}

// Used to collect all nodes in an array
static unsigned storeNodes(struct Node** pos, struct Node* n)
{
	if (n == NULL)
		return 0;
	unsigned cnt = storeNodes(pos, n->left);
	cnt += storeNodes(pos, n->right);
	pos += cnt;
	*pos = n;
	return cnt + 1;
}

// Used to qsort the node array
static int cmpNodes(const void* a, const void* b)
{
	struct Node* const* n1 = a;
	struct Node* const* n2 = b;
	if ((*n1)->first == (*n2)->first)
		return 0;
	if ((*n1)->first > (*n2)->first)
		return 1;
	return -1;
}

// Create a balanced BST tree from a sorted array
static struct Node* insertSorted(
	struct Node* parent, struct Node** nodes, unsigned len)
{
	if (len == 0)
		return NULL;
	unsigned h = len / 2;
	struct Node* n = nodes[h];
	if (parent != NULL)
		insertNode(parent, n);
	else {
		n->left = n->right = NULL;
	}
	D(printf("len=%u, h=%u, %u-%u\n", len, h, n->first, n->last));
	insertSorted(n, nodes, h);
	insertSorted(n, nodes + (h+1), len - (h+1));
	return n;
}

void rangeSetUpdate(struct RangeSet* t)
{
	if (t->count == 0)
		return;

	// https://medium.com/swlh/how-do-we-get-a-balanced-binary-tree-a25e72a9cd58
	// Create a Node array
	struct Node* nodes[t->count];
	unsigned cnt = storeNodes(nodes, t->root);
	D(printf("in root %u, count=%u\n", cnt, t->count));
	cnt += storeNodes(nodes + cnt, t->added);
	assert(cnt == t->count);
	t->added = NULL;
	
	// Sort it
	qsort(nodes, t->count, sizeof(struct Node*), cmpNodes);

	// Merge ranges
	unsigned i = 0;
	while ((i + 1) < t->count) {
		struct Node* n = nodes[i];
		struct Node* nextn = nodes[i+1];
		// UINT_MAX is special since we can't +1 on it!
		if (n->last == UINT_MAX || (n->last + 1) >= nextn->first) {
			// Merge!
			if (nextn->last > n->last)
				n->last = nextn->last;
			free(nextn);
			// Pack the array
			for (unsigned j = i + 1; (j + 1) < t->count; j++)
				nodes[j] = nodes[j+1];
			t->count--;
		} else {
			i++;
		}
	}

	D(for (unsigned i = 0; i < t->count; i++)
		  printf("  %u-%u\n", nodes[i]->first, nodes[i]->last));

	// Create a balanced BST tree from the sorted array
	t->root = insertSorted(t->root, nodes, t->count);
}

static char* printTree(struct Node* n, char* buf, char const* endp)
{
	if (n == NULL)
		return buf;
	char* nbuf = printTree(n->left, buf, endp);
	if (nbuf == NULL)
		return NULL;
	unsigned len = endp - nbuf;
	if (len < 3)
		return NULL;
	int cnt;
	if (n->first == n->last)
		cnt = snprintf(nbuf, len, "%u,", n->first);
	else
		cnt = snprintf(nbuf, len, "%u-%u,", n->first, n->last);
	return printTree(n->right, nbuf + cnt, endp);
}

int rangeSetString(struct RangeSet* t, char* str, unsigned len)
{
	if (t->root == NULL) {
		*str = 0;
		return 0;
	}
	if (printTree(t->root, str, str + len) == NULL)
		return 1;
	return 0;
}

// White-box unit-test functions
#ifdef UNIT_TEST
static unsigned treeDepth(struct Node* n)
{
	if (n == NULL)
		return 0;
	unsigned ldepth = treeDepth(n->left);
	unsigned rdepth = treeDepth(n->right);
	return (ldepth > rdepth ? ldepth : rdepth) + 1;
}
unsigned rangeTreeDepth(struct RangeSet* t)
{
	return treeDepth(t->root);
}
static void printIndentTree(struct Node* n, unsigned indent)
{
	if (n == NULL)
		return;
	printIndentTree(n->left, indent + 2);
	char pad[indent+1];
	memset(pad, ' ', indent);
	pad[indent] = 0;
	if (n->first == n->last)
		printf("%s%u\n", pad, n->first);
	else
		printf("%s%u-%u\n", pad, n->first, n->last);
	printIndentTree(n->right, indent + 2);
}


void rangeTreePrint(struct RangeSet* t)
{
	printIndentTree(t->root, 2);
}
#endif
