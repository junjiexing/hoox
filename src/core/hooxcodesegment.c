/*
 * Copyright (C) 2016-2022 Ole André Vadla Ravnås <oleavr@nowsecure.com>
 *
 * Licence: wxWindows Library Licence, Version 3.1
 */

#include "hooxcodesegment.h"

/**
 * HooxCodeSegment: (skip)
 */

#if !(defined (HAVE_DARWIN) && defined (HAVE_JAILBREAK))

hx_boolean
hoox_code_segment_is_supported (void)
{
  return FALSE;
}

HooxCodeSegment *
hoox_code_segment_new (hx_size size,
                      const HooxAddressSpec * spec)
{
  return NULL;
}

void
hoox_code_segment_free (HooxCodeSegment * segment)
{
}

hx_pointer
hoox_code_segment_get_address (HooxCodeSegment * self)
{
  return NULL;
}

hx_size
hoox_code_segment_get_virtual_size (HooxCodeSegment * self)
{
  return 0;
}

void
hoox_code_segment_realize (HooxCodeSegment * self)
{
}

void
hoox_code_segment_map (HooxCodeSegment * self,
                      hx_size source_offset,
                      hx_size source_size,
                      hx_pointer target_address)
{
}

hx_boolean
hoox_code_segment_mark (hx_pointer code,
                       hx_size size,
                       HxError ** error)
{
  hx_set_error (error, HOOX_ERROR, HOOX_ERROR_NOT_SUPPORTED, "Not supported");
  return FALSE;
}

#endif
