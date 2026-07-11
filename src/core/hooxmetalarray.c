/*
 * Copyright (C) 2017 Ole André Vadla Ravnås <oleavr@nowsecure.com>
 *
 * Licence: wxWindows Library Licence, Version 3.1
 */

#include "hooxmetalarray.h"

#include "hooxmemory.h"

#include <string.h>

static hx_uint hoox_round_up_to_page_size (hx_uint size);

void
hoox_metal_array_init (HooxMetalArray * array,
                      hx_uint element_size)
{
  array->data = hoox_alloc_n_pages (1, HOOX_PAGE_RW);
  array->length = 0;
  array->capacity = hoox_query_page_size () / element_size;

  array->element_size = element_size;
}

void
hoox_metal_array_free (HooxMetalArray * array)
{
  array->element_size = 0;

  array->capacity = 0;
  array->length = 0;
  hoox_free_pages (array->data);
  array->data = NULL;
}

hx_pointer
hoox_metal_array_element_at (HooxMetalArray * self,
                            hx_uint index_)
{
  return ((hx_uint8 *) self->data) + (index_ * self->element_size);
}

void
hoox_metal_array_remove_at (HooxMetalArray * self,
                           hx_uint index_)
{
  if (index_ != self->length - 1)
  {
    memmove (hoox_metal_array_element_at (self, index_),
        hoox_metal_array_element_at (self, index_ + 1),
        (self->length - index_ - 1) * self->element_size);
  }
  self->length--;
}

void
hoox_metal_array_remove_all (HooxMetalArray * self)
{
  self->length = 0;
}

hx_pointer
hoox_metal_array_append (HooxMetalArray * self)
{
  hoox_metal_array_ensure_capacity (self, self->length + 1);

  return hoox_metal_array_element_at (self, self->length++);
}

void
hoox_metal_array_ensure_capacity (HooxMetalArray * self,
                                 hx_uint capacity)
{
  hx_uint size_in_bytes, page_size, size_in_pages;
  hx_pointer new_data;

  if (self->capacity >= capacity)
    return;

  size_in_bytes = capacity * self->element_size;
  page_size = hoox_query_page_size ();
  size_in_pages = size_in_bytes / page_size;
  if (size_in_bytes % page_size != 0)
    size_in_pages++;

  new_data = hoox_alloc_n_pages (size_in_pages, HOOX_PAGE_RW);
  memcpy (new_data, self->data, self->length * self->element_size);

  hoox_free_pages (self->data);
  self->data = new_data;
  self->capacity = (size_in_pages * page_size) / self->element_size;
}

static hx_uint
hoox_round_up_to_page_size (hx_uint size)
{
  hx_uint page_mask = hoox_query_page_size () - 1;

  return (size + page_mask) & ~page_mask;
}
