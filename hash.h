/**
 * Copyright (C) 1996-2009 Michael R. Elkins <me@mutt.org>
 *
 * This program is free software: you can redistribute it and/or modify it under
 * the terms of the GNU General Public License as published by the Free Software
 * Foundation, either version 2 of the License, or (at your option) any later
 * version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
 * details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef _MUTT_HASH_H
#define _MUTT_HASH_H 1

union hash_key {
  const char *strkey;
  unsigned int intkey;
};

struct hash_elem
{
  union hash_key key;
  void *data;
  struct hash_elem *next;
};

typedef struct
{
  int nelem, curnelem;
  bool strdup_keys : 1; /* if set, the key->strkey is strdup'ed */
  bool allow_dups  : 1; /* if set, duplicate keys are allowed */
  struct hash_elem **table;
  unsigned int (*gen_hash)(union hash_key, unsigned int);
  int (*cmp_key)(union hash_key, union hash_key);
} HASH;

/* flags for hash_create() */
#define MUTT_HASH_STRCASECMP  (1 << 0) /* use strcasecmp() to compare keys */
#define MUTT_HASH_STRDUP_KEYS (1 << 1) /* make a copy of the keys */
#define MUTT_HASH_ALLOW_DUPS  (1 << 2) /* allow duplicate keys to be inserted */

HASH *hash_create(int nelem, int flags);
HASH *int_hash_create(int nelem, int flags);

int hash_insert(HASH *table, const char *strkey, void *data);
int int_hash_insert(HASH *table, unsigned int intkey, void *data);
HASH *hash_resize(HASH *ptr, int nelem, int lower);

void *hash_find(const HASH *table, const char *strkey);
struct hash_elem *hash_find_elem(const HASH *table, const char *strkey);
void *int_hash_find(const HASH *table, unsigned int intkey);

struct hash_elem *hash_find_bucket(const HASH *table, const char *strkey);

void hash_delete(HASH *table, const char *strkey, const void *data,
                 void (*destroy)(void *));
void int_hash_delete(HASH *table, unsigned int intkey, const void *data,
                     void (*destroy)(void *));

void hash_destroy(HASH **ptr, void (*destroy)(void *));

struct hash_walk_state
{
  int index;
  struct hash_elem *last;
};

struct hash_elem *hash_walk(const HASH *table, struct hash_walk_state *state);

#endif /* _MUTT_HASH_H */
