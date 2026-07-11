/*
 * hoox nano-glib: HxHashTable.
 *
 * Separate-chaining hash table with GLib-compatible API, iterator, and the
 * common hash/equal helpers.
 *
 * Licence: wxWindows Library Licence, Version 3.1
 */

#ifndef __HOOX_COMPAT_HASH_H__
#define __HOOX_COMPAT_HASH_H__

#include "hxdefs.h"

HX_BEGIN_DECLS

typedef struct _HxHashTable HxHashTable;

typedef struct _HxHashTableIter HxHashTableIter;

struct _HxHashTableIter
{
  hx_pointer table;
  hx_ssize bucket;
  hx_pointer node;
  hx_pointer prev;
  int version;
  int reserved;
};

HxHashTable * hx_hash_table_new (HxHashFunc hash_func, HxEqualFunc key_equal_func);
HxHashTable * hx_hash_table_new_full (HxHashFunc hash_func,
    HxEqualFunc key_equal_func, HxDestroyNotify key_destroy_func,
    HxDestroyNotify value_destroy_func);

HxHashTable * hx_hash_table_ref (HxHashTable * hash_table);
void hx_hash_table_unref (HxHashTable * hash_table);
void hx_hash_table_destroy (HxHashTable * hash_table);

hx_boolean hx_hash_table_insert (HxHashTable * hash_table, hx_pointer key,
    hx_pointer value);
hx_boolean hx_hash_table_replace (HxHashTable * hash_table, hx_pointer key,
    hx_pointer value);
hx_boolean hx_hash_table_add (HxHashTable * hash_table, hx_pointer key);

hx_pointer hx_hash_table_lookup (HxHashTable * hash_table, hx_constpointer key);
hx_boolean hx_hash_table_lookup_extended (HxHashTable * hash_table,
    hx_constpointer lookup_key, hx_pointer * orig_key, hx_pointer * value);
hx_boolean hx_hash_table_contains (HxHashTable * hash_table, hx_constpointer key);

hx_boolean hx_hash_table_remove (HxHashTable * hash_table, hx_constpointer key);
hx_boolean hx_hash_table_steal (HxHashTable * hash_table, hx_constpointer key);
void hx_hash_table_remove_all (HxHashTable * hash_table);

void hx_hash_table_foreach (HxHashTable * hash_table, HxHFunc func,
    hx_pointer user_data);
hx_uint hx_hash_table_foreach_remove (HxHashTable * hash_table, HxHRFunc func,
    hx_pointer user_data);
hx_pointer hx_hash_table_find (HxHashTable * hash_table, HxHRFunc predicate,
    hx_pointer user_data);
hx_uint hx_hash_table_size (HxHashTable * hash_table);

void hx_hash_table_iter_init (HxHashTableIter * iter, HxHashTable * hash_table);
hx_boolean hx_hash_table_iter_next (HxHashTableIter * iter, hx_pointer * key,
    hx_pointer * value);
void hx_hash_table_iter_remove (HxHashTableIter * iter);

/* hash / equal helpers */
hx_uint hx_direct_hash (hx_constpointer v);
hx_boolean hx_direct_equal (hx_constpointer a, hx_constpointer b);
hx_uint hx_int_hash (hx_constpointer v);
hx_boolean hx_int_equal (hx_constpointer a, hx_constpointer b);
hx_uint hx_int64_hash (hx_constpointer v);
hx_boolean hx_int64_equal (hx_constpointer a, hx_constpointer b);
hx_uint hx_str_hash (hx_constpointer v);
hx_boolean hx_str_equal (hx_constpointer a, hx_constpointer b);

HX_END_DECLS

#endif
