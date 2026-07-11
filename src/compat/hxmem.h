/*
 * hoox nano-glib: memory allocation.
 *
 * hx_malloc/hx_free family + hx_slice_* (mapped to plain malloc/free) + hx_new
 * helpers + hx_memdup. Allocation failure aborts, matching GLib's hx_malloc
 * contract (callers never check for NULL).
 *
 * Licence: wxWindows Library Licence, Version 3.1
 */

#ifndef __HOOX_COMPAT_MEM_H__
#define __HOOX_COMPAT_MEM_H__

#include "hxdefs.h"

HX_BEGIN_DECLS

hx_pointer hx_malloc (hx_size n_bytes);
hx_pointer hx_malloc0 (hx_size n_bytes);
hx_pointer hx_realloc (hx_pointer mem, hx_size n_bytes);
void hx_free (hx_pointer mem);

hx_pointer hx_memdup (hx_constpointer mem, hx_uint byte_size);
hx_pointer hx_memdup2 (hx_constpointer mem, hx_size byte_size);

/* hx_slice_* : GLib's slab allocator is deprecated; map to malloc/free. */
hx_pointer hx_slice_alloc (hx_size block_size);
hx_pointer hx_slice_alloc0 (hx_size block_size);
hx_pointer hx_slice_copy (hx_size block_size, hx_constpointer mem_block);
void hx_slice_free1 (hx_size block_size, hx_pointer mem_block);

#define hx_newa(type, count) \
    ((type *) hx_alloca (sizeof (type) * (hx_size) (count)))

#define hx_new(type, count)   ((type *) hx_malloc (sizeof (type) * (hx_size) (count)))
#define hx_new0(type, count)  ((type *) hx_malloc0 (sizeof (type) * (hx_size) (count)))
#define hx_renew(type, mem, count) \
    ((type *) hx_realloc ((mem), sizeof (type) * (hx_size) (count)))

#define hx_slice_new(type)    ((type *) hx_slice_alloc (sizeof (type)))
#define hx_slice_new0(type)   ((type *) hx_slice_alloc0 (sizeof (type)))
#define hx_slice_dup(type, mem) \
    ((type *) hx_slice_copy (sizeof (type), (mem)))
#define hx_slice_free(type, mem) hx_slice_free1 (sizeof (type), (mem))

#define hx_clear_pointer(pp, destroy) \
    HX_STMT_START { \
      hx_pointer * _pp = (hx_pointer *) (pp); \
      hx_pointer _p = *_pp; \
      if (_p != NULL) \
      { \
        *_pp = NULL; \
        (destroy) (_p); \
      } \
    } HX_STMT_END

HX_END_DECLS

#endif
