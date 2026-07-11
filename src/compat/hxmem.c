/*
 * hoox nano-glib: memory allocation implementation.
 *
 * Licence: wxWindows Library Licence, Version 3.1
 */

#include "hxmem.h"
#include "hxmessages.h"

#include <string.h>

static hx_pointer
hx_oom (void)
{
  hx_error ("hoox: out of memory");
  return NULL;
}

hx_pointer
hx_malloc (hx_size n_bytes)
{
  hx_pointer mem;

  if (n_bytes == 0)
    return NULL;

  mem = malloc (n_bytes);
  if (HX_UNLIKELY (mem == NULL))
    return hx_oom ();

  return mem;
}

hx_pointer
hx_malloc0 (hx_size n_bytes)
{
  hx_pointer mem;

  if (n_bytes == 0)
    return NULL;

  mem = calloc (1, n_bytes);
  if (HX_UNLIKELY (mem == NULL))
    return hx_oom ();

  return mem;
}

hx_pointer
hx_realloc (hx_pointer mem,
           hx_size n_bytes)
{
  hx_pointer result;

  if (n_bytes == 0)
  {
    free (mem);
    return NULL;
  }

  result = realloc (mem, n_bytes);
  if (HX_UNLIKELY (result == NULL))
    return hx_oom ();

  return result;
}

void
hx_free (hx_pointer mem)
{
  free (mem);
}

hx_pointer
hx_memdup (hx_constpointer mem,
          hx_uint byte_size)
{
  return hx_memdup2 (mem, byte_size);
}

hx_pointer
hx_memdup2 (hx_constpointer mem,
           hx_size byte_size)
{
  hx_pointer copy;

  if (mem == NULL || byte_size == 0)
    return NULL;

  copy = hx_malloc (byte_size);
  memcpy (copy, mem, byte_size);

  return copy;
}

hx_pointer
hx_slice_alloc (hx_size block_size)
{
  return hx_malloc (block_size);
}

hx_pointer
hx_slice_alloc0 (hx_size block_size)
{
  return hx_malloc0 (block_size);
}

hx_pointer
hx_slice_copy (hx_size block_size,
              hx_constpointer mem_block)
{
  return hx_memdup2 (mem_block, block_size);
}

void
hx_slice_free1 (hx_size block_size,
               hx_pointer mem_block)
{
  (void) block_size;
  free (mem_block);
}
