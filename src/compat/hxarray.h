/*
 * hoox nano-glib: GArray and GPtrArray.
 *
 * Public struct layout matches GLib (GArray = {data,len}, GPtrArray =
 * {pdata,len}) so g_array_index / g_ptr_array_index and direct field access in
 * extracted gum code keep working.
 *
 * Licence: wxWindows Library Licence, Version 3.1
 */

#ifndef __HOOX_COMPAT_ARRAY_H__
#define __HOOX_COMPAT_ARRAY_H__

#include "hxdefs.h"

G_BEGIN_DECLS

typedef struct _GArray GArray;
typedef struct _GPtrArray GPtrArray;

struct _GArray
{
  gchar * data;
  guint len;
};

struct _GPtrArray
{
  gpointer * pdata;
  guint len;
};

/* ---- GArray ------------------------------------------------------------- */

GArray * g_array_new (gboolean zero_terminated, gboolean clear,
    guint element_size);
GArray * g_array_sized_new (gboolean zero_terminated, gboolean clear,
    guint element_size, guint reserved_size);
void g_array_set_clear_func (GArray * array, GDestroyNotify clear_func);
gchar * g_array_free (GArray * array, gboolean free_segment);
GArray * g_array_ref (GArray * array);
void g_array_unref (GArray * array);
guint g_array_get_element_size (GArray * array);

GArray * g_array_append_vals (GArray * array, gconstpointer data, guint len);
GArray * g_array_prepend_vals (GArray * array, gconstpointer data, guint len);
GArray * g_array_insert_vals (GArray * array, guint index_,
    gconstpointer data, guint len);
GArray * g_array_set_size (GArray * array, guint length);
GArray * g_array_remove_index (GArray * array, guint index_);
GArray * g_array_remove_index_fast (GArray * array, guint index_);
GArray * g_array_remove_range (GArray * array, guint index_, guint length);
gpointer g_array_steal (GArray * array, gsize * len);

#define g_array_append_val(a, v) g_array_append_vals ((a), &(v), 1)
#define g_array_prepend_val(a, v) g_array_prepend_vals ((a), &(v), 1)
#define g_array_insert_val(a, i, v) g_array_insert_vals ((a), (i), &(v), 1)
#define g_array_index(a, type, i) (((type *) (void *) (a)->data)[(i)])

/* ---- GPtrArray ---------------------------------------------------------- */

GPtrArray * g_ptr_array_new (void);
GPtrArray * g_ptr_array_sized_new (guint reserved_size);
GPtrArray * g_ptr_array_new_with_free_func (GDestroyNotify element_free_func);
GPtrArray * g_ptr_array_new_full (guint reserved_size,
    GDestroyNotify element_free_func);
void g_ptr_array_set_free_func (GPtrArray * array,
    GDestroyNotify element_free_func);
gpointer * g_ptr_array_free (GPtrArray * array, gboolean free_segment);
GPtrArray * g_ptr_array_ref (GPtrArray * array);
void g_ptr_array_unref (GPtrArray * array);

void g_ptr_array_add (GPtrArray * array, gpointer data);
void g_ptr_array_set_size (GPtrArray * array, gint length);
gboolean g_ptr_array_remove (GPtrArray * array, gpointer data);
gboolean g_ptr_array_remove_fast (GPtrArray * array, gpointer data);
gpointer g_ptr_array_remove_index (GPtrArray * array, guint index_);
gpointer g_ptr_array_remove_index_fast (GPtrArray * array, guint index_);
void g_ptr_array_foreach (GPtrArray * array, GFunc func, gpointer user_data);
gboolean g_ptr_array_find (GPtrArray * array, gconstpointer needle,
    guint * index_);
gpointer * g_ptr_array_steal (GPtrArray * array, gsize * len);

#define g_ptr_array_index(a, i) ((a)->pdata[(i)])

G_END_DECLS

#endif
