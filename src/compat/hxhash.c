/*
 * hoox nano-glib: HxHashTable implementation (separate chaining).
 *
 * Licence: wxWindows Library Licence, Version 3.1
 */

#include "hxhash.h"
#include "hxmem.h"
#include "hxmessages.h"

#include <string.h>

typedef struct _HxNode HxNode;

struct _HxNode
{
  hx_pointer key;
  hx_pointer value;
  hx_uint hash;
  HxNode * next;
};

struct _HxHashTable
{
  HxNode ** buckets;
  hx_uint n_buckets;      /* power of two */
  hx_uint n_items;
  hx_int ref_count;
  int version;

  HxHashFunc hash_func;
  HxEqualFunc key_equal_func;
  HxDestroyNotify key_destroy_func;
  HxDestroyNotify value_destroy_func;
};

#define HX_HASH_INITIAL_BUCKETS 8

static hx_uint
hx_hash_key (HxHashTable * ht,
             hx_constpointer key)
{
  return (ht->hash_func != NULL)
      ? ht->hash_func (key)
      : (hx_uint) HX_POINTER_TO_SIZE (key);
}

static hx_boolean
hx_keys_equal (HxHashTable * ht,
               hx_constpointer a,
               hx_constpointer b)
{
  if (ht->key_equal_func != NULL)
    return ht->key_equal_func (a, b);
  return a == b;
}

HxHashTable *
hx_hash_table_new (HxHashFunc hash_func,
                  HxEqualFunc key_equal_func)
{
  return hx_hash_table_new_full (hash_func, key_equal_func, NULL, NULL);
}

HxHashTable *
hx_hash_table_new_full (HxHashFunc hash_func,
                       HxEqualFunc key_equal_func,
                       HxDestroyNotify key_destroy_func,
                       HxDestroyNotify value_destroy_func)
{
  HxHashTable * ht = hx_new0 (HxHashTable, 1);

  ht->n_buckets = HX_HASH_INITIAL_BUCKETS;
  ht->buckets = hx_new0 (HxNode *, ht->n_buckets);
  ht->ref_count = 1;
  ht->hash_func = hash_func;
  ht->key_equal_func = key_equal_func;
  ht->key_destroy_func = key_destroy_func;
  ht->value_destroy_func = value_destroy_func;

  return ht;
}

static void
hx_hash_resize (HxHashTable * ht)
{
  hx_uint new_n = ht->n_buckets * 2;
  HxNode ** new_buckets = hx_new0 (HxNode *, new_n);
  hx_uint i;

  for (i = 0; i != ht->n_buckets; i++)
  {
    HxNode * node = ht->buckets[i];
    while (node != NULL)
    {
      HxNode * next = node->next;
      hx_uint idx = node->hash & (new_n - 1);
      node->next = new_buckets[idx];
      new_buckets[idx] = node;
      node = next;
    }
  }

  hx_free (ht->buckets);
  ht->buckets = new_buckets;
  ht->n_buckets = new_n;
}

static HxNode *
hx_hash_find (HxHashTable * ht,
              hx_constpointer key,
              hx_uint hash,
              HxNode ** out_prev,
              hx_uint * out_index)
{
  hx_uint idx = hash & (ht->n_buckets - 1);
  HxNode * prev = NULL;
  HxNode * node = ht->buckets[idx];

  while (node != NULL)
  {
    if (node->hash == hash && hx_keys_equal (ht, node->key, key))
      break;
    prev = node;
    node = node->next;
  }

  if (out_prev != NULL)
    *out_prev = prev;
  if (out_index != NULL)
    *out_index = idx;

  return node;
}

static hx_boolean
hx_hash_insert (HxHashTable * ht,
                hx_pointer key,
                hx_pointer value,
                hx_boolean keep_new_key)
{
  hx_uint hash = hx_hash_key (ht, key);
  hx_uint idx;
  HxNode * node = hx_hash_find (ht, key, hash, NULL, &idx);

  if (node != NULL)
  {
    /* existing key */
    if (keep_new_key)
    {
      if (ht->key_destroy_func != NULL)
        ht->key_destroy_func (node->key);
      node->key = key;
    }
    else
    {
      if (ht->key_destroy_func != NULL && node->key != key)
        ht->key_destroy_func (key);
    }

    if (ht->value_destroy_func != NULL && node->value != value)
      ht->value_destroy_func (node->value);
    node->value = value;
    ht->version++;
    return FALSE;
  }

  if (ht->n_items >= ht->n_buckets * 3 / 4)
  {
    hx_hash_resize (ht);
    idx = hash & (ht->n_buckets - 1);
  }

  node = hx_new (HxNode, 1);
  node->key = key;
  node->value = value;
  node->hash = hash;
  node->next = ht->buckets[idx];
  ht->buckets[idx] = node;
  ht->n_items++;
  ht->version++;

  return TRUE;
}

hx_boolean
hx_hash_table_insert (HxHashTable * hash_table,
                     hx_pointer key,
                     hx_pointer value)
{
  return hx_hash_insert (hash_table, key, value, FALSE);
}

hx_boolean
hx_hash_table_add (HxHashTable * hash_table,
                  hx_pointer key)
{
  return hx_hash_insert (hash_table, key, key, TRUE);
}

hx_pointer
hx_hash_table_lookup (HxHashTable * hash_table,
                     hx_constpointer key)
{
  hx_uint hash = hx_hash_key (hash_table, key);
  HxNode * node = hx_hash_find (hash_table, key, hash, NULL, NULL);
  return (node != NULL) ? node->value : NULL;
}

hx_boolean
hx_hash_table_contains (HxHashTable * hash_table,
                       hx_constpointer key)
{
  hx_uint hash = hx_hash_key (hash_table, key);
  return hx_hash_find (hash_table, key, hash, NULL, NULL) != NULL;
}

static hx_boolean
hx_hash_remove_internal (HxHashTable * ht,
                         hx_constpointer key,
                         hx_boolean notify)
{
  hx_uint hash = hx_hash_key (ht, key);
  hx_uint idx;
  HxNode * prev;
  HxNode * node = hx_hash_find (ht, key, hash, &prev, &idx);

  if (node == NULL)
    return FALSE;

  if (prev != NULL)
    prev->next = node->next;
  else
    ht->buckets[idx] = node->next;

  if (notify)
  {
    if (ht->key_destroy_func != NULL)
      ht->key_destroy_func (node->key);
    if (ht->value_destroy_func != NULL)
      ht->value_destroy_func (node->value);
  }

  hx_free (node);
  ht->n_items--;
  ht->version++;

  return TRUE;
}

hx_boolean
hx_hash_table_remove (HxHashTable * hash_table,
                     hx_constpointer key)
{
  return hx_hash_remove_internal (hash_table, key, TRUE);
}

void
hx_hash_table_remove_all (HxHashTable * hash_table)
{
  hx_uint i;

  for (i = 0; i != hash_table->n_buckets; i++)
  {
    HxNode * node = hash_table->buckets[i];
    while (node != NULL)
    {
      HxNode * next = node->next;
      if (hash_table->key_destroy_func != NULL)
        hash_table->key_destroy_func (node->key);
      if (hash_table->value_destroy_func != NULL)
        hash_table->value_destroy_func (node->value);
      hx_free (node);
      node = next;
    }
    hash_table->buckets[i] = NULL;
  }

  hash_table->n_items = 0;
  hash_table->version++;
}

void
hx_hash_table_unref (HxHashTable * hash_table)
{
  if (--hash_table->ref_count == 0)
  {
    hx_hash_table_remove_all (hash_table);
    hx_free (hash_table->buckets);
    hx_free (hash_table);
  }
}

/* hoox:test-only-begin */
void
hx_hash_table_destroy (HxHashTable * hash_table)
{
  hx_hash_table_remove_all (hash_table);
  hx_hash_table_unref (hash_table);
}
/* hoox:test-only-end */

hx_uint
hx_hash_table_size (HxHashTable * hash_table)
{
  return hash_table->n_items;
}

/* ---- iterator ----------------------------------------------------------- */

void
hx_hash_table_iter_init (HxHashTableIter * iter,
                        HxHashTable * hash_table)
{
  iter->table = hash_table;
  iter->bucket = -1;
  iter->node = NULL;
  iter->prev = NULL;
  iter->version = hash_table->version;
}

hx_boolean
hx_hash_table_iter_next (HxHashTableIter * iter,
                        hx_pointer * key,
                        hx_pointer * value)
{
  HxHashTable * ht = iter->table;
  HxNode * node = iter->node;

  iter->prev = node;

  if (node != NULL)
    node = node->next;

  while (node == NULL)
  {
    iter->bucket++;
    if ((hx_uint) iter->bucket >= ht->n_buckets)
    {
      iter->node = NULL;
      return FALSE;
    }
    node = ht->buckets[iter->bucket];
    iter->prev = NULL;
  }

  iter->node = node;

  if (key != NULL)
    *key = node->key;
  if (value != NULL)
    *value = node->value;

  return TRUE;
}

void
hx_hash_table_iter_remove (HxHashTableIter * iter)
{
  HxHashTable * ht = iter->table;
  HxNode * node = iter->node;
  HxNode * prev = iter->prev;

  if (node == NULL)
    return;

  if (prev != NULL)
    prev->next = node->next;
  else
    ht->buckets[iter->bucket] = node->next;

  if (ht->key_destroy_func != NULL)
    ht->key_destroy_func (node->key);
  if (ht->value_destroy_func != NULL)
    ht->value_destroy_func (node->value);

  hx_free (node);
  ht->n_items--;

  /* Reposition so the next iter_next resumes correctly. */
  iter->node = prev;
  if (prev == NULL)
    iter->bucket--;
}

/* ---- hash / equal helpers ----------------------------------------------- */

hx_uint
hx_direct_hash (hx_constpointer v)
{
  return (hx_uint) HX_POINTER_TO_SIZE (v);
}

/* hoox:test-only-begin */
hx_boolean
hx_direct_equal (hx_constpointer a,
                hx_constpointer b)
{
  return a == b;
}
/* hoox:test-only-end */
