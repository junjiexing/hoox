/*
 * hoox nano-glib: HxArray / HxPtrArray implementation.
 *
 * Licence: wxWindows Library Licence, Version 3.1
 */

#include "hxarray.h"
#include "hxmem.h"
#include "hxmessages.h"

#include <string.h>
#include <stdlib.h>

/* ---- HxArray ------------------------------------------------------------- */

typedef struct _HxRealArray HxRealArray;

struct _HxRealArray
{
  hx_uint8 * data;
  hx_uint len;

  hx_uint alloc;            /* capacity in elements */
  hx_uint elt_size;
  hx_uint zero_terminated : 1;
  hx_uint clear : 1;
  hx_int ref_count;
  HxDestroyNotify clear_func;
};

static void
hx_array_maybe_expand (HxRealArray * a,
                       hx_uint extra)
{
  hx_uint want = a->len + extra + (a->zero_terminated ? 1 : 0);

  if (want <= a->alloc)
    return;

  hx_uint new_alloc = (a->alloc != 0) ? a->alloc : 8;
  while (new_alloc < want)
    new_alloc *= 2;

  a->data = hx_realloc (a->data, (hx_size) new_alloc * a->elt_size);

  if (a->clear)
  {
    memset (a->data + (hx_size) a->alloc * a->elt_size, 0,
        (hx_size) (new_alloc - a->alloc) * a->elt_size);
  }

  a->alloc = new_alloc;
}

static void
hx_array_zero_terminate (HxRealArray * a)
{
  if (a->zero_terminated)
    memset (a->data + (hx_size) a->len * a->elt_size, 0, a->elt_size);
}

HxArray *
hx_array_sized_new (hx_boolean zero_terminated,
                   hx_boolean clear,
                   hx_uint element_size,
                   hx_uint reserved_size)
{
  HxRealArray * a = hx_new0 (HxRealArray, 1);

  a->elt_size = element_size;
  a->zero_terminated = zero_terminated ? 1 : 0;
  a->clear = clear ? 1 : 0;
  a->ref_count = 1;

  if (reserved_size != 0 || zero_terminated)
  {
    hx_array_maybe_expand (a, reserved_size);
    if (clear || zero_terminated)
      memset (a->data, 0, (hx_size) a->alloc * a->elt_size);
  }

  return (HxArray *) a;
}

HxArray *
hx_array_new (hx_boolean zero_terminated,
             hx_boolean clear,
             hx_uint element_size)
{
  return hx_array_sized_new (zero_terminated, clear, element_size, 0);
}

static void
hx_array_call_clear (HxRealArray * a,
                     hx_uint index_,
                     hx_uint length)
{
  hx_uint i;

  if (a->clear_func == NULL)
    return;

  for (i = 0; i != length; i++)
    a->clear_func (a->data + (hx_size) (index_ + i) * a->elt_size);
}

hx_char *
hx_array_free (HxArray * array,
              hx_boolean free_segment)
{
  HxRealArray * a = (HxRealArray *) array;
  hx_char * segment;

  if (array == NULL)
    return NULL;

  if (free_segment)
  {
    if (a->clear_func != NULL)
      hx_array_call_clear (a, 0, a->len);
    hx_free (a->data);
    segment = NULL;
  }
  else
  {
    segment = (hx_char *) a->data;
  }

  hx_free (a);

  return segment;
}

void
hx_array_unref (HxArray * array)
{
  HxRealArray * a = (HxRealArray *) array;

  if (--a->ref_count == 0)
    hx_array_free (array, TRUE);
}

HxArray *
hx_array_append_vals (HxArray * array,
                     hx_constpointer data,
                     hx_uint len)
{
  HxRealArray * a = (HxRealArray *) array;

  if (len == 0)
    return array;

  hx_array_maybe_expand (a, len);
  memcpy (a->data + (hx_size) a->len * a->elt_size, data,
      (hx_size) len * a->elt_size);
  a->len += len;
  hx_array_zero_terminate (a);

  return array;
}

HxArray *
hx_array_prepend_vals (HxArray * array,
                      hx_constpointer data,
                      hx_uint len)
{
  return hx_array_insert_vals (array, 0, data, len);
}

HxArray *
hx_array_insert_vals (HxArray * array,
                     hx_uint index_,
                     hx_constpointer data,
                     hx_uint len)
{
  HxRealArray * a = (HxRealArray *) array;

  if (len == 0)
    return array;

  hx_array_maybe_expand (a, len);

  if (index_ < a->len)
  {
    memmove (a->data + (hx_size) (index_ + len) * a->elt_size,
        a->data + (hx_size) index_ * a->elt_size,
        (hx_size) (a->len - index_) * a->elt_size);
  }

  memcpy (a->data + (hx_size) index_ * a->elt_size, data,
      (hx_size) len * a->elt_size);
  a->len += len;
  hx_array_zero_terminate (a);

  return array;
}

HxArray *
hx_array_set_size (HxArray * array,
                  hx_uint length)
{
  HxRealArray * a = (HxRealArray *) array;

  if (length > a->len)
  {
    hx_array_maybe_expand (a, length - a->len);
    if (a->clear)
    {
      memset (a->data + (hx_size) a->len * a->elt_size, 0,
          (hx_size) (length - a->len) * a->elt_size);
    }
  }
  else if (length < a->len)
  {
    if (a->clear_func != NULL)
      hx_array_call_clear (a, length, a->len - length);
  }

  a->len = length;
  hx_array_zero_terminate (a);

  return array;
}

/* hoox:test-only-begin */
HxArray *
hx_array_remove_index (HxArray * array,
                      hx_uint index_)
{
  HxRealArray * a = (HxRealArray *) array;

  hx_return_val_if_fail (index_ < a->len, array);

  if (a->clear_func != NULL)
    hx_array_call_clear (a, index_, 1);

  if (index_ != a->len - 1)
  {
    memmove (a->data + (hx_size) index_ * a->elt_size,
        a->data + (hx_size) (index_ + 1) * a->elt_size,
        (hx_size) (a->len - index_ - 1) * a->elt_size);
  }

  a->len--;
  hx_array_zero_terminate (a);

  return array;
}
/* hoox:test-only-end */

HxArray *
hx_array_remove_index_fast (HxArray * array,
                           hx_uint index_)
{
  HxRealArray * a = (HxRealArray *) array;

  hx_return_val_if_fail (index_ < a->len, array);

  if (a->clear_func != NULL)
    hx_array_call_clear (a, index_, 1);

  if (index_ != a->len - 1)
  {
    memcpy (a->data + (hx_size) index_ * a->elt_size,
        a->data + (hx_size) (a->len - 1) * a->elt_size, a->elt_size);
  }

  a->len--;
  hx_array_zero_terminate (a);

  return array;
}

/* ---- HxPtrArray ---------------------------------------------------------- */

typedef struct _HxRealPtrArray HxRealPtrArray;

struct _HxRealPtrArray
{
  hx_pointer * pdata;
  hx_uint len;

  hx_uint alloc;
  hx_int ref_count;
  HxDestroyNotify element_free_func;
};

static void
hx_ptr_array_maybe_expand (HxRealPtrArray * a,
                           hx_uint extra)
{
  hx_uint want = a->len + extra;
  hx_uint new_alloc;

  if (want <= a->alloc)
    return;

  new_alloc = (a->alloc != 0) ? a->alloc : 8;
  while (new_alloc < want)
    new_alloc *= 2;

  a->pdata = hx_realloc (a->pdata, (hx_size) new_alloc * sizeof (hx_pointer));
  a->alloc = new_alloc;
}

HxPtrArray *
hx_ptr_array_sized_new (hx_uint reserved_size)
{
  HxRealPtrArray * a = hx_new0 (HxRealPtrArray, 1);

  a->ref_count = 1;
  if (reserved_size != 0)
    hx_ptr_array_maybe_expand (a, reserved_size);

  return (HxPtrArray *) a;
}

HxPtrArray *
hx_ptr_array_new (void)
{
  return hx_ptr_array_sized_new (0);
}

/* hoox:test-only-begin */
HxPtrArray *
hx_ptr_array_new_with_free_func (HxDestroyNotify element_free_func)
{
  HxPtrArray * array = hx_ptr_array_new ();
  ((HxRealPtrArray *) array)->element_free_func = element_free_func;
  return array;
}
/* hoox:test-only-end */

HxPtrArray *
hx_ptr_array_new_full (hx_uint reserved_size,
                      HxDestroyNotify element_free_func)
{
  HxPtrArray * array = hx_ptr_array_sized_new (reserved_size);
  ((HxRealPtrArray *) array)->element_free_func = element_free_func;
  return array;
}

hx_pointer *
hx_ptr_array_free (HxPtrArray * array,
                  hx_boolean free_segment)
{
  HxRealPtrArray * a = (HxRealPtrArray *) array;
  hx_pointer * segment;

  if (array == NULL)
    return NULL;

  if (free_segment)
  {
    if (a->element_free_func != NULL)
    {
      hx_uint i;
      for (i = 0; i != a->len; i++)
      {
        if (a->pdata[i] != NULL)
          a->element_free_func (a->pdata[i]);
      }
    }
    hx_free (a->pdata);
    segment = NULL;
  }
  else
  {
    segment = a->pdata;
  }

  hx_free (a);

  return segment;
}

void
hx_ptr_array_unref (HxPtrArray * array)
{
  HxRealPtrArray * a = (HxRealPtrArray *) array;

  if (--a->ref_count == 0)
    hx_ptr_array_free (array, TRUE);
}

void
hx_ptr_array_add (HxPtrArray * array,
                 hx_pointer data)
{
  HxRealPtrArray * a = (HxRealPtrArray *) array;

  hx_ptr_array_maybe_expand (a, 1);
  a->pdata[a->len++] = data;
}

hx_pointer
hx_ptr_array_remove_index (HxPtrArray * array,
                          hx_uint index_)
{
  HxRealPtrArray * a = (HxRealPtrArray *) array;
  hx_pointer result;

  hx_return_val_if_fail (index_ < a->len, NULL);

  result = a->pdata[index_];

  if (index_ != a->len - 1)
  {
    memmove (a->pdata + index_, a->pdata + index_ + 1,
        (hx_size) (a->len - index_ - 1) * sizeof (hx_pointer));
  }
  a->len--;

  return result;
}

hx_pointer
hx_ptr_array_remove_index_fast (HxPtrArray * array,
                               hx_uint index_)
{
  HxRealPtrArray * a = (HxRealPtrArray *) array;
  hx_pointer result;

  hx_return_val_if_fail (index_ < a->len, NULL);

  result = a->pdata[index_];
  a->pdata[index_] = a->pdata[a->len - 1];
  a->len--;

  return result;
}

/* GLib's hx_ptr_array_sort passes pointers-to-elements to the comparator. */
static HxCompareFunc hx_ptr_sort_cmp;

static int
hx_ptr_sort_thunk (const void * a,
                   const void * b)
{
  return hx_ptr_sort_cmp (a, b);
}

void
hx_ptr_array_sort (HxPtrArray * array,
                  HxCompareFunc compare_func)
{
  hx_ptr_sort_cmp = compare_func;
  qsort (array->pdata, array->len, sizeof (hx_pointer), hx_ptr_sort_thunk);
}
