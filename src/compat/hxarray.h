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
hx_char * hx_array_free (HxArray * array, hx_boolean free_segment);
void hx_array_unref (HxArray * array);

HxArray * hx_array_append_vals (HxArray * array, hx_constpointer data, hx_uint len);
HxArray * hx_array_prepend_vals (HxArray * array, hx_constpointer data, hx_uint len);
HxArray * hx_array_insert_vals (HxArray * array, hx_uint index_,
    hx_constpointer data, hx_uint len);
HxArray * hx_array_set_size (HxArray * array, hx_uint length);
/* hoox:test-only-begin */
HxArray * hx_array_remove_index (HxArray * array, hx_uint index_);
/* hoox:test-only-end */
HxArray * hx_array_remove_index_fast (HxArray * array, hx_uint index_);

#define hx_array_append_val(a, v) hx_array_append_vals ((a), &(v), 1)
#define hx_array_prepend_val(a, v) hx_array_prepend_vals ((a), &(v), 1)
#define hx_array_insert_val(a, i, v) hx_array_insert_vals ((a), (i), &(v), 1)
#define hx_array_index(a, type, i) (((type *) (void *) (a)->data)[(i)])

/* ---- HxPtrArray ---------------------------------------------------------- */

HxPtrArray * hx_ptr_array_new (void);
HxPtrArray * hx_ptr_array_sized_new (hx_uint reserved_size);
/* hoox:test-only-begin */
HxPtrArray * hx_ptr_array_new_with_free_func (HxDestroyNotify element_free_func);
/* hoox:test-only-end */
HxPtrArray * hx_ptr_array_new_full (hx_uint reserved_size,
    HxDestroyNotify element_free_func);
hx_pointer * hx_ptr_array_free (HxPtrArray * array, hx_boolean free_segment);
void hx_ptr_array_unref (HxPtrArray * array);

void hx_ptr_array_add (HxPtrArray * array, hx_pointer data);
hx_pointer hx_ptr_array_remove_index (HxPtrArray * array, hx_uint index_);
void hx_ptr_array_sort (HxPtrArray * array, HxCompareFunc compare_func);

#define hx_ptr_array_index(a, i) ((a)->pdata[(i)])

HX_END_DECLS

#endif
