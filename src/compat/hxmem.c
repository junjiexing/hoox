/*
 * hoox nano-glib: memory allocation implementation.
 *
 * Licence: wxWindows Library Licence, Version 3.1
 */

#include "hxmem.h"
#include "hxmessages.h"

#include <string.h>

static gpointer
hx_oom (void)
{
  g_error ("hoox: out of memory");
  return NULL;
}

gpointer
g_malloc (gsize n_bytes)
{
  gpointer mem;

  if (n_bytes == 0)
    return NULL;

  mem = malloc (n_bytes);
  if (G_UNLIKELY (mem == NULL))
    return hx_oom ();

  return mem;
}

gpointer
g_malloc0 (gsize n_bytes)
{
  gpointer mem;

  if (n_bytes == 0)
    return NULL;

  mem = calloc (1, n_bytes);
  if (G_UNLIKELY (mem == NULL))
    return hx_oom ();

  return mem;
}

gpointer
g_realloc (gpointer mem,
           gsize n_bytes)
{
  gpointer result;

  if (n_bytes == 0)
  {
    free (mem);
    return NULL;
  }

  result = realloc (mem, n_bytes);
  if (G_UNLIKELY (result == NULL))
    return hx_oom ();

  return result;
}

gpointer
g_try_malloc (gsize n_bytes)
{
  if (n_bytes == 0)
    return NULL;
  return malloc (n_bytes);
}

gpointer
g_try_malloc0 (gsize n_bytes)
{
  if (n_bytes == 0)
    return NULL;
  return calloc (1, n_bytes);
}

void
g_free (gpointer mem)
{
  free (mem);
}

gpointer
g_memdup (gconstpointer mem,
          guint byte_size)
{
  return g_memdup2 (mem, byte_size);
}

gpointer
g_memdup2 (gconstpointer mem,
           gsize byte_size)
{
  gpointer copy;

  if (mem == NULL || byte_size == 0)
    return NULL;

  copy = g_malloc (byte_size);
  memcpy (copy, mem, byte_size);

  return copy;
}

gpointer
g_slice_alloc (gsize block_size)
{
  return g_malloc (block_size);
}

gpointer
g_slice_alloc0 (gsize block_size)
{
  return g_malloc0 (block_size);
}

gpointer
g_slice_copy (gsize block_size,
              gconstpointer mem_block)
{
  return g_memdup2 (mem_block, block_size);
}

void
g_slice_free1 (gsize block_size,
               gpointer mem_block)
{
  (void) block_size;
  free (mem_block);
}
