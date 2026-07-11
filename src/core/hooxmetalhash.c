/* GLIB - Library of useful routines for C programming
 * Copyright (C) 1995-1997  Peter Mattis, Spencer Kimball and Josh MacDonald
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 */

/*
 * Modified by the GLib Team and others 1997-2000.  See the AUTHORS
 * file for a list of people on the GLib Team.  See the ChangeLog
 * files for a list of changes.  These files are distributed with
 * GLib at ftp://ftp.gtk.org/pub/gtk/.
 */

/*
 * MT safe
 */

#include "hooxmetalhash.h"

#include <string.h>
#include "hooxmemory-priv.h"

#define HASH_TABLE_MIN_SHIFT 3

#define UNUSED_HASH_VALUE 0
#define TOMBSTONE_HASH_VALUE 1
#define HASH_IS_UNUSED(h_) ((h_) == UNUSED_HASH_VALUE)
#define HASH_IS_TOMBSTONE(h_) ((h_) == TOMBSTONE_HASH_VALUE)
#define HASH_IS_REAL(h_) ((h_) >= 2)

/**
 * HooxMetalHashTable: (skip)
 */
struct _HooxMetalHashTable
{
  hx_int             size;
  hx_int             mod;
  hx_uint            mask;
  hx_int             nnodes;
  hx_int             noccupied;

  hx_pointer        *keys;
  hx_uint           *hashes;
  hx_pointer        *values;

  HxHashFunc        hash_func;
  HxEqualFunc       key_equal_func;
  hx_int             ref_count;
  HxDestroyNotify   key_destroy_func;
  HxDestroyNotify   value_destroy_func;
};

typedef struct
{
  HooxMetalHashTable  *hash_table;
  hx_pointer     dummy1;
  hx_pointer     dummy2;
  int          position;
  hx_boolean     dummy3;
  int          version;
} RealIter;

static const hx_int prime_mod [] =
{
  1,
  2,
  3,
  7,
  13,
  31,
  61,
  127,
  251,
  509,
  1021,
  2039,
  4093,
  8191,
  16381,
  32749,
  65521,
  131071,
  262139,
  524287,
  1048573,
  2097143,
  4194301,
  8388593,
  16777213,
  33554393,
  67108859,
  134217689,
  268435399,
  536870909,
  1073741789,
  2147483647
};

#define hoox_metal_new0(struct_type, n_structs) \
    (struct_type *) hoox_internal_calloc (n_structs, sizeof (struct_type))

static void
hoox_metal_hash_table_set_shift (HooxMetalHashTable *hash_table, hx_int shift)
{
  hx_int i;
  hx_uint mask = 0;

  hash_table->size = 1 << shift;
  hash_table->mod  = prime_mod [shift];

  for (i = 0; i < shift; i++)
    {
      mask <<= 1;
      mask |= 1;
    }

  hash_table->mask = mask;
}

static hx_int
hoox_metal_hash_table_find_closest_shift (hx_int n)
{
  hx_int i;

  for (i = 0; n; i++)
    n >>= 1;

  return i;
}

static void
hoox_metal_hash_table_set_shift_from_size (HooxMetalHashTable *hash_table, hx_int size)
{
  hx_int shift;

  shift = hoox_metal_hash_table_find_closest_shift (size);
  shift = MAX (shift, HASH_TABLE_MIN_SHIFT);

  hoox_metal_hash_table_set_shift (hash_table, shift);
}

static inline hx_uint
hoox_metal_hash_table_lookup_node (HooxMetalHashTable    *hash_table,
                          hx_constpointer  key,
                          hx_uint         *hash_return)
{
  hx_uint node_index;
  hx_uint node_hash;
  hx_uint hash_value;
  hx_uint first_tombstone = 0;
  hx_boolean have_tombstone = FALSE;
  hx_uint step = 0;

  hash_value = hash_table->hash_func (key);
  if (HX_UNLIKELY (!HASH_IS_REAL (hash_value)))
    hash_value = 2;

  *hash_return = hash_value;

  node_index = hash_value % hash_table->mod;
  node_hash = hash_table->hashes[node_index];

  while (!HASH_IS_UNUSED (node_hash))
    {
      if (node_hash == hash_value)
        {
          hx_pointer node_key = hash_table->keys[node_index];

          if (hash_table->key_equal_func)
            {
              if (hash_table->key_equal_func (node_key, key))
                return node_index;
            }
          else if (node_key == key)
            {
              return node_index;
            }
        }
      else if (HASH_IS_TOMBSTONE (node_hash) && !have_tombstone)
        {
          first_tombstone = node_index;
          have_tombstone = TRUE;
        }

      step++;
      node_index += step;
      node_index &= hash_table->mask;
      node_hash = hash_table->hashes[node_index];
    }

  if (have_tombstone)
    return first_tombstone;

  return node_index;
}

static void
hoox_metal_hash_table_remove_node (HooxMetalHashTable   *hash_table,
                          hx_int          i,
                          hx_boolean      notify)
{
  hx_pointer key;
  hx_pointer value;

  key = hash_table->keys[i];
  value = hash_table->values[i];

  hash_table->hashes[i] = TOMBSTONE_HASH_VALUE;

  hash_table->keys[i] = NULL;
  hash_table->values[i] = NULL;

  hash_table->nnodes--;

  if (notify && hash_table->key_destroy_func)
    hash_table->key_destroy_func (key);

  if (notify && hash_table->value_destroy_func)
    hash_table->value_destroy_func (value);

}

static void
hoox_metal_hash_table_remove_all_nodes (HooxMetalHashTable *hash_table,
                               hx_boolean    notify)
{
  int i;
  hx_pointer key;
  hx_pointer value;

  hash_table->nnodes = 0;
  hash_table->noccupied = 0;

  if (!notify ||
      (hash_table->key_destroy_func == NULL &&
       hash_table->value_destroy_func == NULL))
    {
      memset (hash_table->hashes, 0, hash_table->size * sizeof (hx_uint));
      memset (hash_table->keys, 0, hash_table->size * sizeof (hx_pointer));
      memset (hash_table->values, 0, hash_table->size * sizeof (hx_pointer));

      return;
    }

  for (i = 0; i < hash_table->size; i++)
    {
      if (HASH_IS_REAL (hash_table->hashes[i]))
        {
          key = hash_table->keys[i];
          value = hash_table->values[i];

          hash_table->hashes[i] = UNUSED_HASH_VALUE;
          hash_table->keys[i] = NULL;
          hash_table->values[i] = NULL;

          if (hash_table->key_destroy_func != NULL)
            hash_table->key_destroy_func (key);

          if (hash_table->value_destroy_func != NULL)
            hash_table->value_destroy_func (value);
        }
      else if (HASH_IS_TOMBSTONE (hash_table->hashes[i]))
        {
          hash_table->hashes[i] = UNUSED_HASH_VALUE;
        }
    }
}

static void
hoox_metal_hash_table_resize (HooxMetalHashTable *hash_table)
{
  hx_pointer *new_keys;
  hx_pointer *new_values;
  hx_uint *new_hashes;
  hx_int old_size;
  hx_int i;

  old_size = hash_table->size;
  hoox_metal_hash_table_set_shift_from_size (hash_table, hash_table->nnodes * 2);

  new_keys = hoox_metal_new0 (hx_pointer, hash_table->size);
  if (hash_table->keys == hash_table->values)
    new_values = new_keys;
  else
    new_values = hoox_metal_new0 (hx_pointer, hash_table->size);
  new_hashes = hoox_metal_new0 (hx_uint, hash_table->size);

  for (i = 0; i < old_size; i++)
    {
      hx_uint node_hash = hash_table->hashes[i];
      hx_uint hash_val;
      hx_uint step = 0;

      if (!HASH_IS_REAL (node_hash))
        continue;

      hash_val = node_hash % hash_table->mod;

      while (!HASH_IS_UNUSED (new_hashes[hash_val]))
        {
          step++;
          hash_val += step;
          hash_val &= hash_table->mask;
        }

      new_hashes[hash_val] = hash_table->hashes[i];
      new_keys[hash_val] = hash_table->keys[i];
      new_values[hash_val] = hash_table->values[i];
    }

  if (hash_table->keys != hash_table->values)
    hoox_internal_free (hash_table->values);

  hoox_internal_free (hash_table->keys);
  hoox_internal_free (hash_table->hashes);

  hash_table->keys = new_keys;
  hash_table->values = new_values;
  hash_table->hashes = new_hashes;

  hash_table->noccupied = hash_table->nnodes;
}

static inline void
hoox_metal_hash_table_maybe_resize (HooxMetalHashTable *hash_table)
{
  hx_int noccupied = hash_table->noccupied;
  hx_int size = hash_table->size;

  if ((size > hash_table->nnodes * 4 && size > 1 << HASH_TABLE_MIN_SHIFT) ||
      (size <= noccupied + (noccupied / 16)))
    hoox_metal_hash_table_resize (hash_table);
}

HooxMetalHashTable *
hoox_metal_hash_table_new (HxHashFunc  hash_func,
                  HxEqualFunc key_equal_func)
{
  return hoox_metal_hash_table_new_full (hash_func, key_equal_func, NULL, NULL);
}


HooxMetalHashTable *
hoox_metal_hash_table_new_full (HxHashFunc      hash_func,
                       HxEqualFunc     key_equal_func,
                       HxDestroyNotify key_destroy_func,
                       HxDestroyNotify value_destroy_func)
{
  HooxMetalHashTable *hash_table;

  hash_table = hoox_internal_malloc (sizeof (HooxMetalHashTable));
  hoox_metal_hash_table_set_shift (hash_table, HASH_TABLE_MIN_SHIFT);
  hash_table->nnodes             = 0;
  hash_table->noccupied          = 0;
  hash_table->hash_func          = hash_func ? hash_func : hx_direct_hash;
  hash_table->key_equal_func     = key_equal_func;
  hash_table->ref_count          = 1;
  hash_table->key_destroy_func   = key_destroy_func;
  hash_table->value_destroy_func = value_destroy_func;
  hash_table->keys               = hoox_metal_new0 (hx_pointer, hash_table->size);
  hash_table->values             = hash_table->keys;
  hash_table->hashes             = hoox_metal_new0 (hx_uint, hash_table->size);

  return hash_table;
}

void
hoox_metal_hash_table_iter_init (HooxMetalHashTableIter *iter,
                        HooxMetalHashTable     *hash_table)
{
  RealIter *ri = (RealIter *) iter;

  hx_return_if_fail (iter != NULL);
  hx_return_if_fail (hash_table != NULL);

  ri->hash_table = hash_table;
  ri->position = -1;
}

hx_boolean
hoox_metal_hash_table_iter_next (HooxMetalHashTableIter *iter,
                        hx_pointer       *key,
                        hx_pointer       *value)
{
  RealIter *ri = (RealIter *) iter;
  hx_int position;

  hx_return_val_if_fail (iter != NULL, FALSE);
  hx_return_val_if_fail (ri->position < ri->hash_table->size, FALSE);

  position = ri->position;

  do
    {
      position++;
      if (position >= ri->hash_table->size)
        {
          ri->position = position;
          return FALSE;
        }
    }
  while (!HASH_IS_REAL (ri->hash_table->hashes[position]));

  if (key != NULL)
    *key = ri->hash_table->keys[position];
  if (value != NULL)
    *value = ri->hash_table->values[position];

  ri->position = position;
  return TRUE;
}

HooxMetalHashTable *
hoox_metal_hash_table_iter_get_hash_table (HooxMetalHashTableIter *iter)
{
  hx_return_val_if_fail (iter != NULL, NULL);

  return ((RealIter *) iter)->hash_table;
}

static void
iter_remove_or_steal (RealIter *ri, hx_boolean notify)
{
  hx_return_if_fail (ri != NULL);
  hx_return_if_fail (ri->position >= 0);
  hx_return_if_fail (ri->position < ri->hash_table->size);

  hoox_metal_hash_table_remove_node (ri->hash_table, ri->position, notify);
}

void
hoox_metal_hash_table_iter_remove (HooxMetalHashTableIter *iter)
{
  iter_remove_or_steal ((RealIter *) iter, TRUE);
}

static hx_boolean
hoox_metal_hash_table_insert_node (HooxMetalHashTable *hash_table,
                          hx_uint       node_index,
                          hx_uint       key_hash,
                          hx_pointer    new_key,
                          hx_pointer    new_value,
                          hx_boolean    keep_new_key,
                          hx_boolean    reusing_key)
{
  hx_boolean already_exists;
  hx_uint old_hash;
  hx_pointer key_to_free = NULL;
  hx_pointer value_to_free = NULL;

  old_hash = hash_table->hashes[node_index];
  already_exists = HASH_IS_REAL (old_hash);

  if (already_exists)
    {
      value_to_free = hash_table->values[node_index];

      if (keep_new_key)
        {
          key_to_free = hash_table->keys[node_index];
          hash_table->keys[node_index] = new_key;
        }
      else
        key_to_free = new_key;
    }
  else
    {
      hash_table->hashes[node_index] = key_hash;
      hash_table->keys[node_index] = new_key;
    }

  if (HX_UNLIKELY (hash_table->keys == hash_table->values && hash_table->keys[node_index] != new_value))
    {
      hash_table->values = hoox_metal_new0 (hx_pointer, hash_table->size);
      memcpy (hash_table->values, hash_table->keys, hash_table->size * sizeof (hx_pointer));
    }

  hash_table->values[node_index] = new_value;

  if (!already_exists)
    {
      hash_table->nnodes++;

      if (HASH_IS_UNUSED (old_hash))
        {
          hash_table->noccupied++;
          hoox_metal_hash_table_maybe_resize (hash_table);
        }
    }

  if (already_exists)
    {
      if (hash_table->key_destroy_func && !reusing_key)
        (* hash_table->key_destroy_func) (key_to_free);
      if (hash_table->value_destroy_func)
        (* hash_table->value_destroy_func) (value_to_free);
    }

  return !already_exists;
}

void
hoox_metal_hash_table_iter_replace (HooxMetalHashTableIter *iter,
                           hx_pointer        value)
{
  RealIter *ri;
  hx_uint node_hash;
  hx_pointer key;

  ri = (RealIter *) iter;

  hx_return_if_fail (ri != NULL);
  hx_return_if_fail (ri->position >= 0);
  hx_return_if_fail (ri->position < ri->hash_table->size);

  node_hash = ri->hash_table->hashes[ri->position];
  key = ri->hash_table->keys[ri->position];

  hoox_metal_hash_table_insert_node (ri->hash_table, ri->position, node_hash, key, value, TRUE, TRUE);
}

void
hoox_metal_hash_table_iter_steal (HooxMetalHashTableIter *iter)
{
  iter_remove_or_steal ((RealIter *) iter, FALSE);
}


HooxMetalHashTable *
hoox_metal_hash_table_ref (HooxMetalHashTable *hash_table)
{
  hx_return_val_if_fail (hash_table != NULL, NULL);

  hx_atomic_int_inc (&hash_table->ref_count);

  return hash_table;
}

void
hoox_metal_hash_table_unref (HooxMetalHashTable *hash_table)
{
  hx_return_if_fail (hash_table != NULL);

  if (hx_atomic_int_dec_and_test (&hash_table->ref_count))
    {
      hoox_metal_hash_table_remove_all_nodes (hash_table, TRUE);
      if (hash_table->keys != hash_table->values)
        hoox_internal_free (hash_table->values);
      hoox_internal_free (hash_table->keys);
      hoox_internal_free (hash_table->hashes);
      hoox_internal_free (hash_table);
    }
}

void
hoox_metal_hash_table_destroy (HooxMetalHashTable *hash_table)
{
  hx_return_if_fail (hash_table != NULL);

  hoox_metal_hash_table_remove_all (hash_table);
  hoox_metal_hash_table_unref (hash_table);
}

hx_pointer
hoox_metal_hash_table_lookup (HooxMetalHashTable    *hash_table,
                     hx_constpointer  key)
{
  hx_uint node_index;
  hx_uint node_hash;

  hx_return_val_if_fail (hash_table != NULL, NULL);

  node_index = hoox_metal_hash_table_lookup_node (hash_table, key, &node_hash);

  return HASH_IS_REAL (hash_table->hashes[node_index])
    ? hash_table->values[node_index]
    : NULL;
}

hx_boolean
hoox_metal_hash_table_lookup_extended (HooxMetalHashTable    *hash_table,
                              hx_constpointer  lookup_key,
                              hx_pointer      *orig_key,
                              hx_pointer      *value)
{
  hx_uint node_index;
  hx_uint node_hash;

  hx_return_val_if_fail (hash_table != NULL, FALSE);

  node_index = hoox_metal_hash_table_lookup_node (hash_table, lookup_key, &node_hash);

  if (!HASH_IS_REAL (hash_table->hashes[node_index]))
    return FALSE;

  if (orig_key)
    *orig_key = hash_table->keys[node_index];

  if (value)
    *value = hash_table->values[node_index];

  return TRUE;
}

static hx_boolean
hoox_metal_hash_table_insert_internal (HooxMetalHashTable *hash_table,
                              hx_pointer    key,
                              hx_pointer    value,
                              hx_boolean    keep_new_key)
{
  hx_uint key_hash;
  hx_uint node_index;

  hx_return_val_if_fail (hash_table != NULL, FALSE);

  node_index = hoox_metal_hash_table_lookup_node (hash_table, key, &key_hash);

  return hoox_metal_hash_table_insert_node (hash_table, node_index, key_hash, key, value, keep_new_key, FALSE);
}

hx_boolean
hoox_metal_hash_table_insert (HooxMetalHashTable *hash_table,
                     hx_pointer    key,
                     hx_pointer    value)
{
  return hoox_metal_hash_table_insert_internal (hash_table, key, value, FALSE);
}

hx_boolean
hoox_metal_hash_table_replace (HooxMetalHashTable *hash_table,
                      hx_pointer    key,
                      hx_pointer    value)
{
  return hoox_metal_hash_table_insert_internal (hash_table, key, value, TRUE);
}

hx_boolean
hoox_metal_hash_table_add (HooxMetalHashTable *hash_table,
                  hx_pointer    key)
{
  return hoox_metal_hash_table_insert_internal (hash_table, key, key, TRUE);
}

hx_boolean
hoox_metal_hash_table_contains (HooxMetalHashTable    *hash_table,
                       hx_constpointer  key)
{
  hx_uint node_index;
  hx_uint node_hash;

  hx_return_val_if_fail (hash_table != NULL, FALSE);

  node_index = hoox_metal_hash_table_lookup_node (hash_table, key, &node_hash);

  return HASH_IS_REAL (hash_table->hashes[node_index]);
}

static hx_boolean
hoox_metal_hash_table_remove_internal (HooxMetalHashTable    *hash_table,
                              hx_constpointer  key,
                              hx_boolean       notify)
{
  hx_uint node_index;
  hx_uint node_hash;

  hx_return_val_if_fail (hash_table != NULL, FALSE);

  node_index = hoox_metal_hash_table_lookup_node (hash_table, key, &node_hash);

  if (!HASH_IS_REAL (hash_table->hashes[node_index]))
    return FALSE;

  hoox_metal_hash_table_remove_node (hash_table, node_index, notify);
  hoox_metal_hash_table_maybe_resize (hash_table);

  return TRUE;
}

hx_boolean
hoox_metal_hash_table_remove (HooxMetalHashTable    *hash_table,
                     hx_constpointer  key)
{
  return hoox_metal_hash_table_remove_internal (hash_table, key, TRUE);
}

hx_boolean
hoox_metal_hash_table_steal (HooxMetalHashTable    *hash_table,
                    hx_constpointer  key)
{
  return hoox_metal_hash_table_remove_internal (hash_table, key, FALSE);
}

void
hoox_metal_hash_table_remove_all (HooxMetalHashTable *hash_table)
{
  hx_return_if_fail (hash_table != NULL);

  hoox_metal_hash_table_remove_all_nodes (hash_table, TRUE);
  hoox_metal_hash_table_maybe_resize (hash_table);
}

void
hoox_metal_hash_table_steal_all (HooxMetalHashTable *hash_table)
{
  hx_return_if_fail (hash_table != NULL);

  hoox_metal_hash_table_remove_all_nodes (hash_table, FALSE);
  hoox_metal_hash_table_maybe_resize (hash_table);
}

static hx_uint
hoox_metal_hash_table_foreach_remove_or_steal (HooxMetalHashTable *hash_table,
                                      HxHRFunc     func,
                                      hx_pointer    user_data,
                                      hx_boolean    notify)
{
  hx_uint deleted = 0;
  hx_int i;

  for (i = 0; i < hash_table->size; i++)
    {
      hx_uint node_hash = hash_table->hashes[i];
      hx_pointer node_key = hash_table->keys[i];
      hx_pointer node_value = hash_table->values[i];

      if (HASH_IS_REAL (node_hash) &&
          (* func) (node_key, node_value, user_data))
        {
          hoox_metal_hash_table_remove_node (hash_table, i, notify);
          deleted++;
        }
    }

  hoox_metal_hash_table_maybe_resize (hash_table);

  return deleted;
}

hx_uint
hoox_metal_hash_table_foreach_remove (HooxMetalHashTable *hash_table,
                             HxHRFunc     func,
                             hx_pointer    user_data)
{
  hx_return_val_if_fail (hash_table != NULL, 0);
  hx_return_val_if_fail (func != NULL, 0);

  return hoox_metal_hash_table_foreach_remove_or_steal (hash_table, func, user_data, TRUE);
}

hx_uint
hoox_metal_hash_table_foreach_steal (HooxMetalHashTable *hash_table,
                            HxHRFunc     func,
                            hx_pointer    user_data)
{
  hx_return_val_if_fail (hash_table != NULL, 0);
  hx_return_val_if_fail (func != NULL, 0);

  return hoox_metal_hash_table_foreach_remove_or_steal (hash_table, func, user_data, FALSE);
}

void
hoox_metal_hash_table_foreach (HooxMetalHashTable *hash_table,
                      HxHFunc      func,
                      hx_pointer    user_data)
{
  hx_int i;

  hx_return_if_fail (hash_table != NULL);
  hx_return_if_fail (func != NULL);

  for (i = 0; i < hash_table->size; i++)
    {
      hx_uint node_hash = hash_table->hashes[i];
      hx_pointer node_key = hash_table->keys[i];
      hx_pointer node_value = hash_table->values[i];

      if (HASH_IS_REAL (node_hash))
        (* func) (node_key, node_value, user_data);
    }
}

hx_pointer
hoox_metal_hash_table_find (HooxMetalHashTable *hash_table,
                   HxHRFunc     predicate,
                   hx_pointer    user_data)
{
  hx_int i;
  hx_boolean match;

  hx_return_val_if_fail (hash_table != NULL, NULL);
  hx_return_val_if_fail (predicate != NULL, NULL);

  match = FALSE;

  for (i = 0; i < hash_table->size; i++)
    {
      hx_uint node_hash = hash_table->hashes[i];
      hx_pointer node_key = hash_table->keys[i];
      hx_pointer node_value = hash_table->values[i];

      if (HASH_IS_REAL (node_hash))
        match = predicate (node_key, node_value, user_data);

      if (match)
        return node_value;
    }

  return NULL;
}

hx_uint
hoox_metal_hash_table_size (HooxMetalHashTable *hash_table)
{
  hx_return_val_if_fail (hash_table != NULL, 0);

  return hash_table->nnodes;
}

