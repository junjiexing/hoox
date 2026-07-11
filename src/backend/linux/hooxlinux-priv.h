/*
 * Copyright (C) 2022-2026 Ole André Vadla Ravnås <oleavr@nowsecure.com>
 *
 * Licence: wxWindows Library Licence, Version 3.1
 */

#ifndef __HOOX_LINUX_PRIV_H__
#define __HOOX_LINUX_PRIV_H__

#include "hxglib.h"

#include <limits.h>

#ifndef PATH_MAX
# define PATH_MAX 4096
#endif

HX_BEGIN_DECLS

typedef struct _HooxProcMapsIter HooxProcMapsIter;

/* Incremental, allocation-free reader over /proc/<pid>/maps. Each call to
 * _hoox_proc_maps_iter_next NUL-terminates one line in place and hands back a
 * pointer into the internal buffer; the line is invalidated on the next call. */
struct _HooxProcMapsIter
{
  hx_int fd;
  hx_char buffer[(2 * PATH_MAX) + 1];
  hx_char * read_cursor;
  hx_char * write_cursor;
};

HX_GNUC_INTERNAL void _hoox_proc_maps_iter_init_for_self (
    HooxProcMapsIter * iter);
HX_GNUC_INTERNAL void _hoox_proc_maps_iter_destroy (HooxProcMapsIter * iter);
HX_GNUC_INTERNAL hx_boolean _hoox_proc_maps_iter_next (HooxProcMapsIter * iter,
    const hx_char ** line);

HX_END_DECLS

#endif
