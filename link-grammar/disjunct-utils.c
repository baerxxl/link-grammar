/*************************************************************************/
/* Copyright (c) 2004                                                    */
/* Daniel Sleator, David Temperley, and John Lafferty                    */
/* All rights reserved                                                   */
/*                                                                       */
/* Use of the link grammar parsing system is subject to the terms of the */
/* license set forth in the LICENSE file included with this software.    */
/* This license allows free redistribution and use in source and binary  */
/* forms, with or without modification, subject to certain conditions.   */
/*                                                                       */
/*************************************************************************/

#include <stdio.h>
#include <string.h>
#include "disjunct-utils.h"
#include "externs.h"
#include "string-set.h"
#include "structures.h"
#include "utilities.h"
#include "wordgraph.h"
#include "word-utils.h"

/* Disjunct utilities ... */

/**
 * free_disjuncts() -- free the list of disjuncts pointed to by c
 * (does not free any strings)
 */
void free_disjuncts(Disjunct *c)
{
	Disjunct *c1;
	for (;c != NULL; c = c1) {
		c1 = c->next;
		free_connectors(c->left);
		free_connectors(c->right);
		free(c->word);
		xfree((char *)c, sizeof(Disjunct));
	}
}

/**
 * Destructively catenates the two disjunct lists d1 followed by d2.
 * Doesn't change the contents of the disjuncts.
 * Traverses the first list, but not the second.
 */
Disjunct * catenate_disjuncts(Disjunct *d1, Disjunct *d2)
{
	Disjunct * dis = d1;

	if (d1 == NULL) return d2;
	if (d2 == NULL) return d1;
	while (dis->next != NULL) dis = dis->next;
	dis->next = d2;
	return d1;
}

/** Returns the number of disjuncts in the list pointed to by d */
unsigned int count_disjuncts(Disjunct * d)
{
	unsigned int count = 0;
	for (; d != NULL; d = d->next)
	{
		count++;
	}
	return count;
}

/* ============================================================= */

typedef struct disjunct_dup_table_s disjunct_dup_table;
struct disjunct_dup_table_s
{
	size_t dup_table_size;
	Disjunct ** dup_table;
};

/**
 * This is a hash function for disjuncts
 *
 * This is the old version that doesn't check for domination, just
 * equality.
 */
static inline unsigned int old_hash_disjunct(disjunct_dup_table *dt, Disjunct * d)
{
	Connector *e;
	unsigned int i;
	i = 0;
	for (e = d->left ; e != NULL; e = e->next) {
		i += string_hash(e->string);
	}
	for (e = d->right ; e != NULL; e = e->next) {
		i += string_hash(e->string);
	}
	i += string_hash(d->string);
	i += (i>>10);
	return (i & (dt->dup_table_size-1));
}

/**
 * Append a Gword list to a given Gword list (w/o duplicates).
 */
void gwordlist_append_list(const Gword ***to_word, const Gword **from_word)
{
	size_t to_word_arr_len = gwordlist_len(*to_word);

	for (const Gword **f = from_word; NULL != *f; f++)
	{
		size_t l;

		/* Note: Must use indexing because to_word may get realloc'ed. */
		for (l = 0; l < to_word_arr_len; l++)
			if (*f == (*to_word)[l]) break; /* Filter duplicates. */

		if (l == to_word_arr_len)
			gwordlist_append((Gword ***)to_word, (Gword *)*f);
	}
}

/**
 * The connectors must be exactly equal.  A similar function
 * is connectors_equal_AND(), but that ignores priorities,
 * this does not.
 */
static bool connectors_equal_prune(Connector *c1, Connector *c2)
{
	return string_set_cmp(c1->string, c2->string) && (c1->multi == c2->multi);
}

/** returns TRUE if the disjuncts are exactly the same */
static bool disjuncts_equal(Disjunct * d1, Disjunct * d2)
{
	Connector *e1, *e2;

	e1 = d1->left;
	e2 = d2->left;
	while ((e1 != NULL) && (e2 != NULL)) {
		if (!connectors_equal_prune(e1, e2)) return false;
		e1 = e1->next;
		e2 = e2->next;
	}
	if ((e1 != NULL) || (e2 != NULL)) return false;

	e1 = d1->right;
	e2 = d2->right;
	while ((e1 != NULL) && (e2 != NULL)) {
		if (!connectors_equal_prune(e1, e2)) return false;
		e1 = e1->next;
		e2 = e2->next;
	}
	if ((e1 != NULL) || (e2 != NULL)) return false;

	/* Save CPU time by comparing this last, since this will
	 * almost always be true. Rarely, the strings are not from
	 * the same string_set and hence the 2-step comparison. */
	if (d1->string == d2->string) return true;
	return (strcmp(d1->string, d2->string) == 0);
}

/**
 * Duplicate the given connector chain.
 * If the argument is NULL, return NULL.
 */
static Connector *connectors_dup(Connector *origc)
{
	Connector head;
	Connector *prevc = &head;
	Connector *newc = &head;
	Connector *t;

	for (t = origc; t != NULL;  t = t->next)
	{
		newc = connector_new();
		*newc = *t;

		prevc->next = newc;
		prevc = newc;
	}
	newc->next = NULL;

	return head.next;
}

/**
 * Duplicate the given disjunct chain.
 * If the argument is NULL, return NULL.
 */
Disjunct *disjuncts_dup(Disjunct *origd)
{
	Disjunct head;
	Disjunct *prevd = &head;
	Disjunct *newd = &head;
	Disjunct *t;

	for (t = origd; t != NULL; t = t->next)
	{
		newd = (Disjunct *)xalloc(sizeof(Disjunct));
		newd->string = t->string;
		newd->cost = t->cost;
		newd->left = connectors_dup(t->left);
		newd->right = connectors_dup(t->right);
		newd->word = NULL;
		gwordlist_append_list(&newd->word, t->word);

		prevd->next = newd;
		prevd = newd;
	}
	newd->next = NULL;

	return head.next;
}

static disjunct_dup_table * disjunct_dup_table_new(size_t sz)
{
	size_t i;
	disjunct_dup_table *dt;
	dt = (disjunct_dup_table *) xalloc(sizeof(disjunct_dup_table));

	dt->dup_table_size = sz;
	dt->dup_table = (Disjunct **) xalloc(sz * sizeof(Disjunct *));

	for (i=0; i<sz; i++) dt->dup_table[i] = NULL;

	return dt;
}

static void disjunct_dup_table_delete(disjunct_dup_table *dt)
{
	xfree(dt->dup_table, dt->dup_table_size * sizeof(Disjunct *));
	xfree(dt, sizeof(disjunct_dup_table));
}

/**
 * Takes the list of disjuncts pointed to by d, eliminates all
 * duplicates, and returns a pointer to a new list.
 * It frees the disjuncts that are eliminated.
 */
Disjunct * eliminate_duplicate_disjuncts(Disjunct * d)
{
	unsigned int i, h, count;
	Disjunct *dn, *dx;
	disjunct_dup_table *dt;

	count = 0;
	dt = disjunct_dup_table_new(next_power_of_two_up(2 * count_disjuncts(d)));

	while (d != NULL)
	{
		dn = d->next;
		h = old_hash_disjunct(dt, d);

		for (dx = dt->dup_table[h]; dx != NULL; dx = dx->next)
		{
			if (disjuncts_equal(dx, d)) break;
		}
		if (dx == NULL)
		{
			d->next = dt->dup_table[h];
			dt->dup_table[h] = d;
		}
		else
		{
			d->next = NULL;  /* to prevent it from freeing the whole list */
			if (d->cost < dx->cost) dx->cost = d->cost;
			gwordlist_append_list(&dx->word, d->word);
			free_disjuncts(d);
			count++;
		}
		d = dn;
	}

	/* d is already null */
	for (i=0; i < dt->dup_table_size; i++)
	{
		for (dn = dt->dup_table[i]; dn != NULL; dn = dx) {
			dx = dn->next;
			dn->next = d;
			d = dn;
		}
	}

	lgdebug(+5+(0==count)*1000, "Killed %u duplicates\n", count);

	disjunct_dup_table_delete(dt);
	return d;
}

/* ============================================================= */

/* Return the stringified disjunct.
 * Be sure to free the string upon return.
 */

static char * prt_con(Connector *c, char * p, char dir, size_t * bufsz)
{
	size_t n;

	if (NULL == c) return p;
	p = prt_con (c->next, p, dir, bufsz);

	if (c->multi)
	{
		n = snprintf(p, *bufsz, "@%s%c ", c->string, dir);
		*bufsz -= n;
	}
	else
	{
		n = snprintf(p, *bufsz, "%s%c ", c->string, dir);
		*bufsz -= n;
	}
	return p+n;
}

char * print_one_disjunct(Disjunct *dj)
{
	char buff[MAX_LINE];
	char * p = buff;
	size_t bufsz = MAX_LINE;

	p = prt_con(dj->left, p, '-', &bufsz);
	p = prt_con(dj->right, p, '+', &bufsz);
	buff[MAX_LINE-1] = 0;

	return strdup(buff);
}

/* ============================================================= */

/**
 * Record the wordgraph word to which the X-node belongs, in each of its
 * disjuncts.
 */
void word_record_in_disjunct(const Gword * gw, Disjunct * d)
{
	for (;d != NULL; d=d->next) {
		d->word = malloc(sizeof(*d->word)*2);
		d->word[0] = gw;
		d->word[1] = NULL;
	}
}
/* ========================= END OF FILE ============================== */
