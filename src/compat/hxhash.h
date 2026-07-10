/*
 * hoox nano-glib: GHashTable.
 *
 * Separate-chaining hash table with GLib-compatible API, iterator, and the
 * common hash/equal helpers.
 *
 * Licence: wxWindows Library Licence, Version 3.1
 */

#ifndef __HOOX_COMPAT_HASH_H__
#define __HOOX_COMPAT_HASH_H__

#include "hxdefs.h"

G_BEGIN_DECLS

typedef struct _GHashTable GHashTable;

typedef struct _GHashTableIter GHashTableIter;

struct _GHashTableIter
{
  gpointer table;
  gssize bucket;
  gpointer node;
  gpointer prev;
  int version;
  int reserved;
};

GHashTable * g_hash_table_new (GHashFunc hash_func, GEqualFunc key_equal_func);
GHashTable * g_hash_table_new_full (GHashFunc hash_func,
    GEqualFunc key_equal_func, GDestroyNotify key_destroy_func,
    GDestroyNotify value_destroy_func);

GHashTable * g_hash_table_ref (GHashTable * hash_table);
void g_hash_table_unref (GHashTable * hash_table);
void g_hash_table_destroy (GHashTable * hash_table);

gboolean g_hash_table_insert (GHashTable * hash_table, gpointer key,
    gpointer value);
gboolean g_hash_table_replace (GHashTable * hash_table, gpointer key,
    gpointer value);
gboolean g_hash_table_add (GHashTable * hash_table, gpointer key);

gpointer g_hash_table_lookup (GHashTable * hash_table, gconstpointer key);
gboolean g_hash_table_lookup_extended (GHashTable * hash_table,
    gconstpointer lookup_key, gpointer * orig_key, gpointer * value);
gboolean g_hash_table_contains (GHashTable * hash_table, gconstpointer key);

gboolean g_hash_table_remove (GHashTable * hash_table, gconstpointer key);
gboolean g_hash_table_steal (GHashTable * hash_table, gconstpointer key);
void g_hash_table_remove_all (GHashTable * hash_table);

void g_hash_table_foreach (GHashTable * hash_table, GHFunc func,
    gpointer user_data);
guint g_hash_table_foreach_remove (GHashTable * hash_table, GHRFunc func,
    gpointer user_data);
gpointer g_hash_table_find (GHashTable * hash_table, GHRFunc predicate,
    gpointer user_data);
guint g_hash_table_size (GHashTable * hash_table);

void g_hash_table_iter_init (GHashTableIter * iter, GHashTable * hash_table);
gboolean g_hash_table_iter_next (GHashTableIter * iter, gpointer * key,
    gpointer * value);
void g_hash_table_iter_remove (GHashTableIter * iter);

/* hash / equal helpers */
guint g_direct_hash (gconstpointer v);
gboolean g_direct_equal (gconstpointer a, gconstpointer b);
guint g_int_hash (gconstpointer v);
gboolean g_int_equal (gconstpointer a, gconstpointer b);
guint g_int64_hash (gconstpointer v);
gboolean g_int64_equal (gconstpointer a, gconstpointer b);
guint g_str_hash (gconstpointer v);
gboolean g_str_equal (gconstpointer a, gconstpointer b);

G_END_DECLS

#endif
