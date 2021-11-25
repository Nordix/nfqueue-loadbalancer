/*
  SPDX-License-Identifier: Apache-2.0
  Copyright (c) 2021 Nordix Foundation
*/

#include <rangeset.h>
#include <die.h>
#include <itempool.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stddef.h>

#ifndef UNIT_TEST
#define NDEBUG
#endif
#include <assert.h>

#define Dx(x) x
#define D(x)

/*
  We assume that rangeSetString() will be used with a 1024 char string.
  I every other number is used approx 1024/4 will fit in that string.
 */
#define DEFAULT_MAX 256

struct Node {
	unsigned first, last;
	struct Node* left;
	struct Node* right;
};

struct RangeSet {
	unsigned max;
	unsigned count;
	struct ItemPool* pool;
	struct Node* root;
	struct Node* added;
};

struct RangeSet* rangeSetCreate(unsigned max)
{
	struct RangeSet* t = calloc(1, sizeof(struct RangeSet));
	if (t == NULL)
		die("OOM");
	if (max == 0)
		t->max = DEFAULT_MAX;
	else
		t->max = max;
	t->pool = itemPoolCreate(t->max, sizeof(struct Node), NULL);
	if (t->pool == NULL)
		die("OOM");
	return t;
}

void rangeSetDestroy(struct RangeSet* t)
{
	if (t == NULL)
		return;
	itemPoolDestroy(t->pool, NULL);
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
	struct Item* item = itemAllocate(t->pool);
	if (item == NULL)
		return -1;
	struct Node* n = (struct Node*)item->data;
	n->first = first;
	n->last = last;
	n->left = t->added;
	n->right = NULL;
	t->added = n;
	t->count++;
	return 0;
}

int rangeSetAddStr(struct RangeSet* t, char const* _str)
{
	char* str = strndup(_str, 1024);
	char* tok = strtok(str, ", ");
	while (tok != NULL) {
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
		tok = strtok(NULL, ", ");
	}
	return 0;
}

unsigned rangeSetSize(struct RangeSet* t)
{
	return t->count;
}

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
		if (nodes[i]->last >= (nodes[i+1]->first - 1)) {
			// Merge!
			if (nodes[i+1]->last > nodes[i]->last)
				nodes[i]->last = nodes[i+1]->last;
			itemFree(ITEM_OF(nodes[i+1]));
			// Pack the array
			for (unsigned j = i + 1; (j + 1) < t->count; j++)
				nodes[j] = nodes[j+1];
			t->count--;
		} else {
			i++;
		}
	}

	assert(t->count ==
		   (itemPoolStats(t->pool)->size - itemPoolStats(t->pool)->nFree));
	D(for (unsigned i = 0; i < t->count; i++)
		  printf("  %u-%u\n", nodes[i]->first, nodes[i]->last));

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
	if (printTree(t->root, str, str + len) == NULL)
		return 1;
	return 0;
}

#ifdef UNIT_TEST
unsigned treeDepth(struct Node* n)
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
#endif
