/*
 * hoox nano-glib: GHashTable implementation (separate chaining).
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
  gpointer key;
  gpointer value;
  guint hash;
  HxNode * next;
};

struct _GHashTable
{
  HxNode ** buckets;
  guint n_buckets;      /* power of two */
  guint n_items;
  gint ref_count;
  int version;

  GHashFunc hash_func;
  GEqualFunc key_equal_func;
  GDestroyNotify key_destroy_func;
  GDestroyNotify value_destroy_func;
};

#define HX_HASH_INITIAL_BUCKETS 8

static guint
hx_hash_key (GHashTable * ht,
             gconstpointer key)
{
  return (ht->hash_func != NULL)
      ? ht->hash_func (key)
      : (guint) GPOINTER_TO_SIZE (key);
}

static gboolean
hx_keys_equal (GHashTable * ht,
               gconstpointer a,
               gconstpointer b)
{
  if (ht->key_equal_func != NULL)
    return ht->key_equal_func (a, b);
  return a == b;
}

GHashTable *
g_hash_table_new (GHashFunc hash_func,
                  GEqualFunc key_equal_func)
{
  return g_hash_table_new_full (hash_func, key_equal_func, NULL, NULL);
}

GHashTable *
g_hash_table_new_full (GHashFunc hash_func,
                       GEqualFunc key_equal_func,
                       GDestroyNotify key_destroy_func,
                       GDestroyNotify value_destroy_func)
{
  GHashTable * ht = g_new0 (GHashTable, 1);

  ht->n_buckets = HX_HASH_INITIAL_BUCKETS;
  ht->buckets = g_new0 (HxNode *, ht->n_buckets);
  ht->ref_count = 1;
  ht->hash_func = hash_func;
  ht->key_equal_func = key_equal_func;
  ht->key_destroy_func = key_destroy_func;
  ht->value_destroy_func = value_destroy_func;

  return ht;
}

static void
hx_hash_resize (GHashTable * ht)
{
  guint new_n = ht->n_buckets * 2;
  HxNode ** new_buckets = g_new0 (HxNode *, new_n);
  guint i;

  for (i = 0; i != ht->n_buckets; i++)
  {
    HxNode * node = ht->buckets[i];
    while (node != NULL)
    {
      HxNode * next = node->next;
      guint idx = node->hash & (new_n - 1);
      node->next = new_buckets[idx];
      new_buckets[idx] = node;
      node = next;
    }
  }

  g_free (ht->buckets);
  ht->buckets = new_buckets;
  ht->n_buckets = new_n;
}

static HxNode *
hx_hash_find (GHashTable * ht,
              gconstpointer key,
              guint hash,
              HxNode ** out_prev,
              guint * out_index)
{
  guint idx = hash & (ht->n_buckets - 1);
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

static gboolean
hx_hash_insert (GHashTable * ht,
                gpointer key,
                gpointer value,
                gboolean keep_new_key)
{
  guint hash = hx_hash_key (ht, key);
  guint idx;
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

  node = g_new (HxNode, 1);
  node->key = key;
  node->value = value;
  node->hash = hash;
  node->next = ht->buckets[idx];
  ht->buckets[idx] = node;
  ht->n_items++;
  ht->version++;

  return TRUE;
}

gboolean
g_hash_table_insert (GHashTable * hash_table,
                     gpointer key,
                     gpointer value)
{
  return hx_hash_insert (hash_table, key, value, FALSE);
}

gboolean
g_hash_table_replace (GHashTable * hash_table,
                      gpointer key,
                      gpointer value)
{
  return hx_hash_insert (hash_table, key, value, TRUE);
}

gboolean
g_hash_table_add (GHashTable * hash_table,
                  gpointer key)
{
  return hx_hash_insert (hash_table, key, key, TRUE);
}

gpointer
g_hash_table_lookup (GHashTable * hash_table,
                     gconstpointer key)
{
  guint hash = hx_hash_key (hash_table, key);
  HxNode * node = hx_hash_find (hash_table, key, hash, NULL, NULL);
  return (node != NULL) ? node->value : NULL;
}

gboolean
g_hash_table_lookup_extended (GHashTable * hash_table,
                              gconstpointer lookup_key,
                              gpointer * orig_key,
                              gpointer * value)
{
  guint hash = hx_hash_key (hash_table, lookup_key);
  HxNode * node = hx_hash_find (hash_table, lookup_key, hash, NULL, NULL);

  if (node == NULL)
    return FALSE;

  if (orig_key != NULL)
    *orig_key = node->key;
  if (value != NULL)
    *value = node->value;

  return TRUE;
}

gboolean
g_hash_table_contains (GHashTable * hash_table,
                       gconstpointer key)
{
  guint hash = hx_hash_key (hash_table, key);
  return hx_hash_find (hash_table, key, hash, NULL, NULL) != NULL;
}

static gboolean
hx_hash_remove_internal (GHashTable * ht,
                         gconstpointer key,
                         gboolean notify)
{
  guint hash = hx_hash_key (ht, key);
  guint idx;
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

  g_free (node);
  ht->n_items--;
  ht->version++;

  return TRUE;
}

gboolean
g_hash_table_remove (GHashTable * hash_table,
                     gconstpointer key)
{
  return hx_hash_remove_internal (hash_table, key, TRUE);
}

gboolean
g_hash_table_steal (GHashTable * hash_table,
                    gconstpointer key)
{
  return hx_hash_remove_internal (hash_table, key, FALSE);
}

void
g_hash_table_remove_all (GHashTable * hash_table)
{
  guint i;

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
      g_free (node);
      node = next;
    }
    hash_table->buckets[i] = NULL;
  }

  hash_table->n_items = 0;
  hash_table->version++;
}

GHashTable *
g_hash_table_ref (GHashTable * hash_table)
{
  hash_table->ref_count++;
  return hash_table;
}

void
g_hash_table_unref (GHashTable * hash_table)
{
  if (--hash_table->ref_count == 0)
  {
    g_hash_table_remove_all (hash_table);
    g_free (hash_table->buckets);
    g_free (hash_table);
  }
}

void
g_hash_table_destroy (GHashTable * hash_table)
{
  g_hash_table_remove_all (hash_table);
  g_hash_table_unref (hash_table);
}

void
g_hash_table_foreach (GHashTable * hash_table,
                      GHFunc func,
                      gpointer user_data)
{
  guint i;

  for (i = 0; i != hash_table->n_buckets; i++)
  {
    HxNode * node = hash_table->buckets[i];
    while (node != NULL)
    {
      HxNode * next = node->next;
      func (node->key, node->value, user_data);
      node = next;
    }
  }
}

guint
g_hash_table_foreach_remove (GHashTable * hash_table,
                             GHRFunc func,
                             gpointer user_data)
{
  guint i;
  guint removed = 0;

  for (i = 0; i != hash_table->n_buckets; i++)
  {
    HxNode * prev = NULL;
    HxNode * node = hash_table->buckets[i];

    while (node != NULL)
    {
      HxNode * next = node->next;

      if (func (node->key, node->value, user_data))
      {
        if (prev != NULL)
          prev->next = next;
        else
          hash_table->buckets[i] = next;

        if (hash_table->key_destroy_func != NULL)
          hash_table->key_destroy_func (node->key);
        if (hash_table->value_destroy_func != NULL)
          hash_table->value_destroy_func (node->value);
        g_free (node);
        hash_table->n_items--;
        removed++;
      }
      else
      {
        prev = node;
      }

      node = next;
    }
  }

  if (removed != 0)
    hash_table->version++;

  return removed;
}

gpointer
g_hash_table_find (GHashTable * hash_table,
                   GHRFunc predicate,
                   gpointer user_data)
{
  guint i;

  for (i = 0; i != hash_table->n_buckets; i++)
  {
    HxNode * node = hash_table->buckets[i];
    while (node != NULL)
    {
      if (predicate (node->key, node->value, user_data))
        return node->value;
      node = node->next;
    }
  }

  return NULL;
}

guint
g_hash_table_size (GHashTable * hash_table)
{
  return hash_table->n_items;
}

/* ---- iterator ----------------------------------------------------------- */

void
g_hash_table_iter_init (GHashTableIter * iter,
                        GHashTable * hash_table)
{
  iter->table = hash_table;
  iter->bucket = -1;
  iter->node = NULL;
  iter->prev = NULL;
  iter->version = hash_table->version;
}

gboolean
g_hash_table_iter_next (GHashTableIter * iter,
                        gpointer * key,
                        gpointer * value)
{
  GHashTable * ht = iter->table;
  HxNode * node = iter->node;

  iter->prev = node;

  if (node != NULL)
    node = node->next;

  while (node == NULL)
  {
    iter->bucket++;
    if ((guint) iter->bucket >= ht->n_buckets)
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
g_hash_table_iter_remove (GHashTableIter * iter)
{
  GHashTable * ht = iter->table;
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

  g_free (node);
  ht->n_items--;

  /* Reposition so the next iter_next resumes correctly. */
  iter->node = prev;
  if (prev == NULL)
    iter->bucket--;
}

/* ---- hash / equal helpers ----------------------------------------------- */

guint
g_direct_hash (gconstpointer v)
{
  return (guint) GPOINTER_TO_SIZE (v);
}

gboolean
g_direct_equal (gconstpointer a,
                gconstpointer b)
{
  return a == b;
}

guint
g_int_hash (gconstpointer v)
{
  return (guint) *(const gint *) v;
}

gboolean
g_int_equal (gconstpointer a,
             gconstpointer b)
{
  return *(const gint *) a == *(const gint *) b;
}

guint
g_int64_hash (gconstpointer v)
{
  guint64 k = *(const guint64 *) v;
  return (guint) (k ^ (k >> 32));
}

gboolean
g_int64_equal (gconstpointer a,
               gconstpointer b)
{
  return *(const guint64 *) a == *(const guint64 *) b;
}

guint
g_str_hash (gconstpointer v)
{
  const signed char * p;
  guint32 h = 5381;

  for (p = v; *p != '\0'; p++)
    h = (h << 5) + h + (guint32) *p;

  return h;
}

gboolean
g_str_equal (gconstpointer a,
             gconstpointer b)
{
  return strcmp (a, b) == 0;
}
