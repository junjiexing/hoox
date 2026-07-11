/*
 * hoox nano-glib: HxArray and HxPtrArray.
 *
 * Public struct layout matches GLib (HxArray = {data,len}, HxPtrArray =
 * {pdata,len}) so hx_array_index / hx_ptr_array_index and direct field access in
 * extracted hoox code keep working.
 *
 * Licence: wxWindows Library Licence, Version 3.1
 */

#ifndef __HOOX_COMPAT_ARRAY_H__
#define __HOOX_COMPAT_ARRAY_H__

#include "hxdefs.h"

HX_BEGIN_DECLS

typedef struct _HxArray HxArray;
typedef struct _HxPtrArray HxPtrArray;

struct _HxArray
{
  hx_char * data;
  hx_uint len;
};

struct _HxPtrArray
{
  hx_pointer * pdata;
  hx_uint len;
};

/* ---- HxArray ------------------------------------------------------------- */

HxArray * hx_array_new (hx_boolean zero_terminated, hx_boolean clear,
    hx_uint element_size);
HxArray * hx_array_sized_new (hx_boolean zero_terminated, hx_boolean clear,
    hx_uint element_size, hx_uint reserved_size);
void hx_array_set_clear_func (HxArray * array, HxDestroyNotify clear_func);
hx_char * hx_array_free (HxArray * array, hx_boolean free_segment);
HxArray * hx_array_ref (HxArray * array);
void hx_array_unref (HxArray * array);
hx_uint hx_array_get_element_size (HxArray * array);

HxArray * hx_array_append_vals (HxArray * array, hx_constpointer data, hx_uint len);
HxArray * hx_array_prepend_vals (HxArray * array, hx_constpointer data, hx_uint len);
HxArray * hx_array_insert_vals (HxArray * array, hx_uint index_,
    hx_constpointer data, hx_uint len);
HxArray * hx_array_set_size (HxArray * array, hx_uint length);
HxArray * hx_array_remove_index (HxArray * array, hx_uint index_);
HxArray * hx_array_remove_index_fast (HxArray * array, hx_uint index_);
HxArray * hx_array_remove_range (HxArray * array, hx_uint index_, hx_uint length);
hx_pointer hx_array_steal (HxArray * array, hx_size * len);

#define hx_array_append_val(a, v) hx_array_append_vals ((a), &(v), 1)
#define hx_array_prepend_val(a, v) hx_array_prepend_vals ((a), &(v), 1)
#define hx_array_insert_val(a, i, v) hx_array_insert_vals ((a), (i), &(v), 1)
#define hx_array_index(a, type, i) (((type *) (void *) (a)->data)[(i)])

/* ---- HxPtrArray ---------------------------------------------------------- */

HxPtrArray * hx_ptr_array_new (void);
HxPtrArray * hx_ptr_array_sized_new (hx_uint reserved_size);
HxPtrArray * hx_ptr_array_new_with_free_func (HxDestroyNotify element_free_func);
HxPtrArray * hx_ptr_array_new_full (hx_uint reserved_size,
    HxDestroyNotify element_free_func);
void hx_ptr_array_set_free_func (HxPtrArray * array,
    HxDestroyNotify element_free_func);
hx_pointer * hx_ptr_array_free (HxPtrArray * array, hx_boolean free_segment);
HxPtrArray * hx_ptr_array_ref (HxPtrArray * array);
void hx_ptr_array_unref (HxPtrArray * array);

void hx_ptr_array_add (HxPtrArray * array, hx_pointer data);
void hx_ptr_array_set_size (HxPtrArray * array, hx_int length);
hx_boolean hx_ptr_array_remove (HxPtrArray * array, hx_pointer data);
hx_boolean hx_ptr_array_remove_fast (HxPtrArray * array, hx_pointer data);
hx_pointer hx_ptr_array_remove_index (HxPtrArray * array, hx_uint index_);
hx_pointer hx_ptr_array_remove_index_fast (HxPtrArray * array, hx_uint index_);
void hx_ptr_array_foreach (HxPtrArray * array, HxFunc func, hx_pointer user_data);
void hx_ptr_array_sort (HxPtrArray * array, HxCompareFunc compare_func);
hx_boolean hx_ptr_array_find (HxPtrArray * array, hx_constpointer needle,
    hx_uint * index_);
hx_pointer * hx_ptr_array_steal (HxPtrArray * array, hx_size * len);

#define hx_ptr_array_index(a, i) ((a)->pdata[(i)])

HX_END_DECLS

#endif
