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

#ifndef __HOOX_METAL_HASH_H__
#define __HOOX_METAL_HASH_H__

#include "hooxdefs.h"

HX_BEGIN_DECLS

typedef struct _HooxMetalHashTable HooxMetalHashTable;
typedef struct _HooxMetalHashTableIter HooxMetalHashTableIter;

struct _HooxMetalHashTableIter
{
  hx_pointer dummy1;
  hx_pointer dummy2;
  hx_pointer dummy3;
  int dummy4;
  hx_boolean dummy5;
  hx_pointer dummy6;
};

HOOX_API HooxMetalHashTable * hoox_metal_hash_table_new (HxHashFunc hash_func,
    HxEqualFunc key_equal_func);
HOOX_API HooxMetalHashTable * hoox_metal_hash_table_new_full (HxHashFunc hash_func,
    HxEqualFunc key_equal_func, HxDestroyNotify key_destroy_func,
    HxDestroyNotify value_destroy_func);
HOOX_API hx_boolean hoox_metal_hash_table_insert (HooxMetalHashTable * hash_table,
    hx_pointer key, hx_pointer value);
HOOX_API void hoox_metal_hash_table_remove_all (HooxMetalHashTable * hash_table);
HOOX_API hx_pointer hoox_metal_hash_table_lookup (HooxMetalHashTable * hash_table,
    hx_constpointer key);


HOOX_API void hoox_metal_hash_table_unref (HooxMetalHashTable * hash_table);

HX_END_DECLS

#endif
