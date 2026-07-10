/*
 * hoox nano-glib: GArray / GPtrArray implementation.
 *
 * Licence: wxWindows Library Licence, Version 3.1
 */

#include "hxarray.h"
#include "hxmem.h"
#include "hxmessages.h"

#include <string.h>

/* ---- GArray ------------------------------------------------------------- */

typedef struct _GRealArray GRealArray;

struct _GRealArray
{
  guint8 * data;
  guint len;

  guint alloc;            /* capacity in elements */
  guint elt_size;
  guint zero_terminated : 1;
  guint clear : 1;
  gint ref_count;
  GDestroyNotify clear_func;
};

static void
hx_array_maybe_expand (GRealArray * a,
                       guint extra)
{
  guint want = a->len + extra + (a->zero_terminated ? 1 : 0);

  if (want <= a->alloc)
    return;

  guint new_alloc = (a->alloc != 0) ? a->alloc : 8;
  while (new_alloc < want)
    new_alloc *= 2;

  a->data = g_realloc (a->data, (gsize) new_alloc * a->elt_size);

  if (a->clear)
  {
    memset (a->data + (gsize) a->alloc * a->elt_size, 0,
        (gsize) (new_alloc - a->alloc) * a->elt_size);
  }

  a->alloc = new_alloc;
}

static void
hx_array_zero_terminate (GRealArray * a)
{
  if (a->zero_terminated)
    memset (a->data + (gsize) a->len * a->elt_size, 0, a->elt_size);
}

GArray *
g_array_sized_new (gboolean zero_terminated,
                   gboolean clear,
                   guint element_size,
                   guint reserved_size)
{
  GRealArray * a = g_new0 (GRealArray, 1);

  a->elt_size = element_size;
  a->zero_terminated = zero_terminated ? 1 : 0;
  a->clear = clear ? 1 : 0;
  a->ref_count = 1;

  if (reserved_size != 0 || zero_terminated)
  {
    hx_array_maybe_expand (a, reserved_size);
    if (clear || zero_terminated)
      memset (a->data, 0, (gsize) a->alloc * a->elt_size);
  }

  return (GArray *) a;
}

GArray *
g_array_new (gboolean zero_terminated,
             gboolean clear,
             guint element_size)
{
  return g_array_sized_new (zero_terminated, clear, element_size, 0);
}

void
g_array_set_clear_func (GArray * array,
                        GDestroyNotify clear_func)
{
  ((GRealArray *) array)->clear_func = clear_func;
}

guint
g_array_get_element_size (GArray * array)
{
  return ((GRealArray *) array)->elt_size;
}

static void
hx_array_call_clear (GRealArray * a,
                     guint index_,
                     guint length)
{
  guint i;

  if (a->clear_func == NULL)
    return;

  for (i = 0; i != length; i++)
    a->clear_func (a->data + (gsize) (index_ + i) * a->elt_size);
}

gchar *
g_array_free (GArray * array,
              gboolean free_segment)
{
  GRealArray * a = (GRealArray *) array;
  gchar * segment;

  if (array == NULL)
    return NULL;

  if (free_segment)
  {
    if (a->clear_func != NULL)
      hx_array_call_clear (a, 0, a->len);
    g_free (a->data);
    segment = NULL;
  }
  else
  {
    segment = (gchar *) a->data;
  }

  g_free (a);

  return segment;
}

GArray *
g_array_ref (GArray * array)
{
  ((GRealArray *) array)->ref_count++;
  return array;
}

void
g_array_unref (GArray * array)
{
  GRealArray * a = (GRealArray *) array;

  if (--a->ref_count == 0)
    g_array_free (array, TRUE);
}

GArray *
g_array_append_vals (GArray * array,
                     gconstpointer data,
                     guint len)
{
  GRealArray * a = (GRealArray *) array;

  if (len == 0)
    return array;

  hx_array_maybe_expand (a, len);
  memcpy (a->data + (gsize) a->len * a->elt_size, data,
      (gsize) len * a->elt_size);
  a->len += len;
  hx_array_zero_terminate (a);

  return array;
}

GArray *
g_array_prepend_vals (GArray * array,
                      gconstpointer data,
                      guint len)
{
  return g_array_insert_vals (array, 0, data, len);
}

GArray *
g_array_insert_vals (GArray * array,
                     guint index_,
                     gconstpointer data,
                     guint len)
{
  GRealArray * a = (GRealArray *) array;

  if (len == 0)
    return array;

  hx_array_maybe_expand (a, len);

  if (index_ < a->len)
  {
    memmove (a->data + (gsize) (index_ + len) * a->elt_size,
        a->data + (gsize) index_ * a->elt_size,
        (gsize) (a->len - index_) * a->elt_size);
  }

  memcpy (a->data + (gsize) index_ * a->elt_size, data,
      (gsize) len * a->elt_size);
  a->len += len;
  hx_array_zero_terminate (a);

  return array;
}

GArray *
g_array_set_size (GArray * array,
                  guint length)
{
  GRealArray * a = (GRealArray *) array;

  if (length > a->len)
  {
    hx_array_maybe_expand (a, length - a->len);
    if (a->clear)
    {
      memset (a->data + (gsize) a->len * a->elt_size, 0,
          (gsize) (length - a->len) * a->elt_size);
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

GArray *
g_array_remove_index (GArray * array,
                      guint index_)
{
  GRealArray * a = (GRealArray *) array;

  g_return_val_if_fail (index_ < a->len, array);

  if (a->clear_func != NULL)
    hx_array_call_clear (a, index_, 1);

  if (index_ != a->len - 1)
  {
    memmove (a->data + (gsize) index_ * a->elt_size,
        a->data + (gsize) (index_ + 1) * a->elt_size,
        (gsize) (a->len - index_ - 1) * a->elt_size);
  }

  a->len--;
  hx_array_zero_terminate (a);

  return array;
}

GArray *
g_array_remove_index_fast (GArray * array,
                           guint index_)
{
  GRealArray * a = (GRealArray *) array;

  g_return_val_if_fail (index_ < a->len, array);

  if (a->clear_func != NULL)
    hx_array_call_clear (a, index_, 1);

  if (index_ != a->len - 1)
  {
    memcpy (a->data + (gsize) index_ * a->elt_size,
        a->data + (gsize) (a->len - 1) * a->elt_size, a->elt_size);
  }

  a->len--;
  hx_array_zero_terminate (a);

  return array;
}

GArray *
g_array_remove_range (GArray * array,
                      guint index_,
                      guint length)
{
  GRealArray * a = (GRealArray *) array;

  g_return_val_if_fail (index_ + length <= a->len, array);

  if (a->clear_func != NULL)
    hx_array_call_clear (a, index_, length);

  if (index_ + length != a->len)
  {
    memmove (a->data + (gsize) index_ * a->elt_size,
        a->data + (gsize) (index_ + length) * a->elt_size,
        (gsize) (a->len - (index_ + length)) * a->elt_size);
  }

  a->len -= length;
  hx_array_zero_terminate (a);

  return array;
}

gpointer
g_array_steal (GArray * array,
               gsize * len)
{
  GRealArray * a = (GRealArray *) array;
  gpointer segment = a->data;

  if (len != NULL)
    *len = a->len;

  a->data = NULL;
  a->len = 0;
  a->alloc = 0;

  return segment;
}

/* ---- GPtrArray ---------------------------------------------------------- */

typedef struct _GRealPtrArray GRealPtrArray;

struct _GRealPtrArray
{
  gpointer * pdata;
  guint len;

  guint alloc;
  gint ref_count;
  GDestroyNotify element_free_func;
};

static void
hx_ptr_array_maybe_expand (GRealPtrArray * a,
                           guint extra)
{
  guint want = a->len + extra;
  guint new_alloc;

  if (want <= a->alloc)
    return;

  new_alloc = (a->alloc != 0) ? a->alloc : 8;
  while (new_alloc < want)
    new_alloc *= 2;

  a->pdata = g_realloc (a->pdata, (gsize) new_alloc * sizeof (gpointer));
  a->alloc = new_alloc;
}

GPtrArray *
g_ptr_array_sized_new (guint reserved_size)
{
  GRealPtrArray * a = g_new0 (GRealPtrArray, 1);

  a->ref_count = 1;
  if (reserved_size != 0)
    hx_ptr_array_maybe_expand (a, reserved_size);

  return (GPtrArray *) a;
}

GPtrArray *
g_ptr_array_new (void)
{
  return g_ptr_array_sized_new (0);
}

GPtrArray *
g_ptr_array_new_with_free_func (GDestroyNotify element_free_func)
{
  GPtrArray * array = g_ptr_array_new ();
  ((GRealPtrArray *) array)->element_free_func = element_free_func;
  return array;
}

GPtrArray *
g_ptr_array_new_full (guint reserved_size,
                      GDestroyNotify element_free_func)
{
  GPtrArray * array = g_ptr_array_sized_new (reserved_size);
  ((GRealPtrArray *) array)->element_free_func = element_free_func;
  return array;
}

void
g_ptr_array_set_free_func (GPtrArray * array,
                           GDestroyNotify element_free_func)
{
  ((GRealPtrArray *) array)->element_free_func = element_free_func;
}

gpointer *
g_ptr_array_free (GPtrArray * array,
                  gboolean free_segment)
{
  GRealPtrArray * a = (GRealPtrArray *) array;
  gpointer * segment;

  if (array == NULL)
    return NULL;

  if (free_segment)
  {
    if (a->element_free_func != NULL)
    {
      guint i;
      for (i = 0; i != a->len; i++)
      {
        if (a->pdata[i] != NULL)
          a->element_free_func (a->pdata[i]);
      }
    }
    g_free (a->pdata);
    segment = NULL;
  }
  else
  {
    segment = a->pdata;
  }

  g_free (a);

  return segment;
}

GPtrArray *
g_ptr_array_ref (GPtrArray * array)
{
  ((GRealPtrArray *) array)->ref_count++;
  return array;
}

void
g_ptr_array_unref (GPtrArray * array)
{
  GRealPtrArray * a = (GRealPtrArray *) array;

  if (--a->ref_count == 0)
    g_ptr_array_free (array, TRUE);
}

void
g_ptr_array_add (GPtrArray * array,
                 gpointer data)
{
  GRealPtrArray * a = (GRealPtrArray *) array;

  hx_ptr_array_maybe_expand (a, 1);
  a->pdata[a->len++] = data;
}

void
g_ptr_array_set_size (GPtrArray * array,
                      gint length)
{
  GRealPtrArray * a = (GRealPtrArray *) array;
  guint new_len = (guint) length;

  if (new_len > a->len)
  {
    hx_ptr_array_maybe_expand (a, new_len - a->len);
    memset (a->pdata + a->len, 0,
        (gsize) (new_len - a->len) * sizeof (gpointer));
  }
  else if (new_len < a->len)
  {
    if (a->element_free_func != NULL)
    {
      guint i;
      for (i = new_len; i != a->len; i++)
      {
        if (a->pdata[i] != NULL)
          a->element_free_func (a->pdata[i]);
      }
    }
  }

  a->len = new_len;
}

gpointer
g_ptr_array_remove_index (GPtrArray * array,
                          guint index_)
{
  GRealPtrArray * a = (GRealPtrArray *) array;
  gpointer result;

  g_return_val_if_fail (index_ < a->len, NULL);

  result = a->pdata[index_];

  if (index_ != a->len - 1)
  {
    memmove (a->pdata + index_, a->pdata + index_ + 1,
        (gsize) (a->len - index_ - 1) * sizeof (gpointer));
  }
  a->len--;

  return result;
}

gpointer
g_ptr_array_remove_index_fast (GPtrArray * array,
                               guint index_)
{
  GRealPtrArray * a = (GRealPtrArray *) array;
  gpointer result;

  g_return_val_if_fail (index_ < a->len, NULL);

  result = a->pdata[index_];
  a->pdata[index_] = a->pdata[a->len - 1];
  a->len--;

  return result;
}

gboolean
g_ptr_array_remove (GPtrArray * array,
                    gpointer data)
{
  guint i;

  for (i = 0; i != array->len; i++)
  {
    if (array->pdata[i] == data)
    {
      g_ptr_array_remove_index (array, i);
      return TRUE;
    }
  }

  return FALSE;
}

gboolean
g_ptr_array_remove_fast (GPtrArray * array,
                         gpointer data)
{
  guint i;

  for (i = 0; i != array->len; i++)
  {
    if (array->pdata[i] == data)
    {
      g_ptr_array_remove_index_fast (array, i);
      return TRUE;
    }
  }

  return FALSE;
}

void
g_ptr_array_foreach (GPtrArray * array,
                     GFunc func,
                     gpointer user_data)
{
  guint i;

  for (i = 0; i != array->len; i++)
    func (array->pdata[i], user_data);
}

gboolean
g_ptr_array_find (GPtrArray * array,
                  gconstpointer needle,
                  guint * index_)
{
  guint i;

  for (i = 0; i != array->len; i++)
  {
    if (array->pdata[i] == needle)
    {
      if (index_ != NULL)
        *index_ = i;
      return TRUE;
    }
  }

  return FALSE;
}

gpointer *
g_ptr_array_steal (GPtrArray * array,
                   gsize * len)
{
  GRealPtrArray * a = (GRealPtrArray *) array;
  gpointer * segment = a->pdata;

  if (len != NULL)
    *len = a->len;

  a->pdata = NULL;
  a->len = 0;
  a->alloc = 0;

  return segment;
}
