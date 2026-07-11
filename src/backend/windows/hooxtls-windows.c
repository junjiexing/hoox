/*
* Copyright (C) 2015-2023 Ole André Vadla Ravnås <oleavr@nowsecure.com>
* Copyright (C) 2015 Eloi Vanderbeken <eloi.vanderbeken@synacktiv.com>
*
* Licence: wxWindows Library Licence, Version 3.1
*/

#include "hooxtls.h"

#include "hooxprocess.h"
#include "hooxspinlock.h"

#ifndef WIN32_LEAN_AND_MEAN
# define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <intrin.h>

#if defined (HAVE_I386)

# define MAX_TMP_TLS_KEY 200

typedef struct _HooxTmpTlsKey HooxTmpTlsKey;

struct _HooxTmpTlsKey
{
  HooxThreadId tid;
  HooxTlsKey key;
  hx_pointer value;
};

static hx_pointer hoox_tls_key_get_tmp_value (HooxTlsKey key);
static void hoox_tls_key_set_tmp_value (HooxTlsKey key, hx_pointer value);
static void hoox_tls_key_del_tmp_value (HooxTlsKey key);

static HooxTmpTlsKey hoox_tls_tmp_keys[MAX_TMP_TLS_KEY];
static HooxSpinlock hoox_tls_tmp_keys_lock = HOOX_SPINLOCK_INIT;

#endif

HooxTlsKey
hoox_tls_key_new (void)
{
  DWORD res;

  res = TlsAlloc ();
  hx_assert (res != TLS_OUT_OF_INDEXES);

  return res;
}

void
hoox_tls_key_free (HooxTlsKey key)
{
  TlsFree ((DWORD) key);
}

void
_hoox_tls_init (void)
{
#if defined (HAVE_I386)
  memset (hoox_tls_tmp_keys, 0, sizeof (hoox_tls_tmp_keys));
#endif
}

void
_hoox_tls_realize (void)
{
}

void
_hoox_tls_deinit (void)
{
}

#if defined (HAVE_I386)

static hx_pointer
hoox_tls_key_get_tmp_value (HooxTlsKey key)
{
  HooxThreadId tid;
  hx_pointer value;
  hx_uint i;

  tid = hoox_process_get_current_thread_id ();
  value = NULL;

  hoox_spinlock_acquire (&hoox_tls_tmp_keys_lock);

  for (i = 0; i != MAX_TMP_TLS_KEY; i++)
  {
    if (hoox_tls_tmp_keys[i].tid == tid && hoox_tls_tmp_keys[i].key == key)
    {
      value = hoox_tls_tmp_keys[i].value;
      break;
    }
  }

  hoox_spinlock_release (&hoox_tls_tmp_keys_lock);

  return value;
}

static void
hoox_tls_key_set_tmp_value (HooxTlsKey key,
                           hx_pointer value)
{
  HooxThreadId tid;
  hx_uint i;

  tid = hoox_process_get_current_thread_id ();

  hoox_spinlock_acquire (&hoox_tls_tmp_keys_lock);

  for (i = 0; i != MAX_TMP_TLS_KEY; i++)
  {
    if (hoox_tls_tmp_keys[i].tid == 0)
    {
      hoox_tls_tmp_keys[i].tid = tid;
      hoox_tls_tmp_keys[i].key = key;
      hoox_tls_tmp_keys[i].value = value;
      break;
    }
  }
  hx_assert (i < MAX_TMP_TLS_KEY);

  hoox_spinlock_release (&hoox_tls_tmp_keys_lock);
}

static void
hoox_tls_key_del_tmp_value (HooxTlsKey key)
{
  HooxThreadId tid;
  hx_uint i;

  tid = hoox_process_get_current_thread_id ();

  hoox_spinlock_acquire (&hoox_tls_tmp_keys_lock);

  for (i = 0; i != MAX_TMP_TLS_KEY; i++)
  {
    if (hoox_tls_tmp_keys[i].tid == tid && hoox_tls_tmp_keys[i].key == key)
    {
      memset (&hoox_tls_tmp_keys[i], 0, sizeof (hoox_tls_tmp_keys[i]));
      break;
    }
  }
  hx_assert (i < MAX_TMP_TLS_KEY);

  hoox_spinlock_release (&hoox_tls_tmp_keys_lock);
}

# ifndef _MSC_VER
#  pragma GCC diagnostic push
#  pragma GCC diagnostic ignored "-Warray-bounds"
# endif

# if HX_SIZEOF_VOID_P == 4

hx_pointer
hoox_tls_key_get_value (HooxTlsKey key)
{
  if (key < 64)
  {
    return (hx_pointer) __readfsdword (3600 + key * sizeof (hx_pointer));
  }
  else if (key < 1088)
  {
    hx_pointer * tls_expansion_slots;

    tls_expansion_slots = (hx_pointer *) __readfsdword (3988);
    if (tls_expansion_slots != NULL)
      return tls_expansion_slots[key - 64];

    return hoox_tls_key_get_tmp_value (key);
  }

  return NULL;
}

void
hoox_tls_key_set_value (HooxTlsKey key,
                       hx_pointer value)
{
  if (key < 64)
  {
    __writefsdword (3600 + key * sizeof (hx_pointer), (DWORD) value);
  }
  else if (key < 1088)
  {
    hx_pointer * tls_expansion_slots;

    tls_expansion_slots = (hx_pointer *) __readfsdword (3988);
    if (tls_expansion_slots != NULL)
    {
      tls_expansion_slots[key - 64] = value;
    }
    else
    {
      hoox_tls_key_set_tmp_value (key, value);
      TlsSetValue (key, value);
      hoox_tls_key_del_tmp_value (key);
    }
  }
}

# elif HX_SIZEOF_VOID_P == 8

hx_pointer
hoox_tls_key_get_value (HooxTlsKey key)
{
  if (key < 64)
  {
    return (hx_pointer) __readgsqword ((DWORD) (0x1480 + key * sizeof (hx_pointer)));
  }
  else if (key < 1088)
  {
    hx_pointer * tls_expansion_slots;

    tls_expansion_slots = (hx_pointer) __readgsqword (0x1780);
    if (tls_expansion_slots != NULL)
      return tls_expansion_slots[key - 64];

    return hoox_tls_key_get_tmp_value (key);
  }
  return NULL;
}

void
hoox_tls_key_set_value (HooxTlsKey key,
                       hx_pointer value)
{
  if (key < 64)
  {
    __writegsqword ((DWORD) (0x1480 + key * sizeof (hx_pointer)), (hx_uint64) value);
  }
  else if (key < 1088)
  {
    hx_pointer * tls_expansion_slots;

    tls_expansion_slots = (hx_pointer) __readgsqword (0x1780);
    if (tls_expansion_slots != NULL)
    {
      tls_expansion_slots[key - 64] = value;
    }
    else
    {
      hoox_tls_key_set_tmp_value (key, value);
      TlsSetValue ((DWORD) key, value);
      hoox_tls_key_del_tmp_value (key);
    }
  }
}

# else
#  error Unknown architecture
# endif

# ifndef _MSC_VER
#  pragma GCC diagnostic pop
# endif

#else

hx_pointer
hoox_tls_key_get_value (HooxTlsKey key)
{
  return TlsGetValue (key);
}

void
hoox_tls_key_set_value (HooxTlsKey key,
                       hx_pointer value)
{
  TlsSetValue (key, value);
}

#endif
