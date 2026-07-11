/*
 * Copyright (C) 2016-2019 Ole André Vadla Ravnås <oleavr@nowsecure.com>
 *
 * Licence: wxWindows Library Licence, Version 3.1
 */

#ifndef __HOOX_CODE_SEGMENT_H__
#define __HOOX_CODE_SEGMENT_H__

#include "hooxmemory.h"

HX_BEGIN_DECLS

typedef struct _HooxCodeSegment HooxCodeSegment;

HOOX_API hx_boolean hoox_code_segment_is_supported (void);

HOOX_API HooxCodeSegment * hoox_code_segment_new (hx_size size,
    const HooxAddressSpec * spec);
HOOX_API void hoox_code_segment_free (HooxCodeSegment * segment);

HOOX_API hx_pointer hoox_code_segment_get_address (HooxCodeSegment * self);
HOOX_API hx_size hoox_code_segment_get_virtual_size (HooxCodeSegment * self);

HOOX_API void hoox_code_segment_realize (HooxCodeSegment * self);
HOOX_API void hoox_code_segment_map (HooxCodeSegment * self, hx_size source_offset,
    hx_size source_size, hx_pointer target_address);

HOOX_API hx_boolean hoox_code_segment_mark (hx_pointer code, hx_size size,
    HxError ** error);

HX_END_DECLS

#endif
