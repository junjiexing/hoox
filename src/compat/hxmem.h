/*
 * hoox nano-glib: memory allocation.
 *
 * g_malloc/g_free family + g_slice_* (mapped to plain malloc/free) + g_new
 * helpers + g_memdup. Allocation failure aborts, matching GLib's g_malloc
 * contract (callers never check for NULL).
 *
 * Licence: wxWindows Library Licence, Version 3.1
 */

#ifndef __HOOX_COMPAT_MEM_H__
#define __HOOX_COMPAT_MEM_H__

#include "hxdefs.h"

G_BEGIN_DECLS

gpointer g_malloc (gsize n_bytes);
gpointer g_malloc0 (gsize n_bytes);
gpointer g_realloc (gpointer mem, gsize n_bytes);
gpointer g_try_malloc (gsize n_bytes);
gpointer g_try_malloc0 (gsize n_bytes);
void g_free (gpointer mem);

gpointer g_memdup (gconstpointer mem, guint byte_size);
gpointer g_memdup2 (gconstpointer mem, gsize byte_size);

/* g_slice_* : GLib's slab allocator is deprecated; map to malloc/free. */
gpointer g_slice_alloc (gsize block_size);
gpointer g_slice_alloc0 (gsize block_size);
gpointer g_slice_copy (gsize block_size, gconstpointer mem_block);
void g_slice_free1 (gsize block_size, gpointer mem_block);

#define g_newa(type, count) \
    ((type *) g_alloca (sizeof (type) * (gsize) (count)))

#define g_new(type, count)   ((type *) g_malloc (sizeof (type) * (gsize) (count)))
#define g_new0(type, count)  ((type *) g_malloc0 (sizeof (type) * (gsize) (count)))
#define g_renew(type, mem, count) \
    ((type *) g_realloc ((mem), sizeof (type) * (gsize) (count)))

#define g_slice_new(type)    ((type *) g_slice_alloc (sizeof (type)))
#define g_slice_new0(type)   ((type *) g_slice_alloc0 (sizeof (type)))
#define g_slice_dup(type, mem) \
    ((type *) g_slice_copy (sizeof (type), (mem)))
#define g_slice_free(type, mem) g_slice_free1 (sizeof (type), (mem))

#define g_clear_pointer(pp, destroy) \
    G_STMT_START { \
      gpointer * _pp = (gpointer *) (pp); \
      gpointer _p = *_pp; \
      if (_p != NULL) \
      { \
        *_pp = NULL; \
        (destroy) (_p); \
      } \
    } G_STMT_END

G_END_DECLS

#endif
