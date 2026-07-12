/*
 * Copyright (C) 2016-2024 Ole André Vadla Ravnås <oleavr@nowsecure.com>
 *
 * hoox — Darwin (macOS) code-segment backend.
 *
 * Ported from frida-gum's gumcodesegment-darwin.c. Implements the ad-hoc
 * code-signing + mach_vm_remap mechanism used to place executable code /
 * trampolines on Apple platforms where code pages cannot be made W+X.
 *
 * On macOS the mechanism is: allocate RW pages, let the caller write code into
 * them, then map() overlays them onto the target via mach_vm_remap() with the
 * source pages flipped to RX (hoox_code_segment_try_remap_locally). The
 * temp-file + code-signature + fcntl(F_ADDFILESIGS) + mmap realize path exists
 * for iOS/tvOS (where realize()/map() take the signed-mapping route); it is
 * compiled here for parity but only exercised when HAVE_IOS / HAVE_TVOS is
 * defined. The jailbreak/substrate remapping path is kept behind those same
 * guards and is never built on macOS.
 *
 * Licence: wxWindows Library Licence, Version 3.1
 */

#include "hooxcodesegment.h"

#include "hooxmemory.h"

#include <CommonCrypto/CommonDigest.h>
#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <mach/mach.h>
#include "hooxdarwin.h"
#include <mach-o/loader.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/sysctl.h>
#include <unistd.h>

#define HOOX_CS_MAGIC_EMBEDDED_SIGNATURE 0xfade0cc0
#define HOOX_CS_MAGIC_CODE_DIRECTORY 0xfade0c02
#define HOOX_CS_MAGIC_REQUIREMENTS 0xfade0c01

#define HOOX_CS_HASH_SHA1 1
#define HOOX_CS_HASH_SHA1_SIZE 20

#define HOOX_OFFSET_NONE -1

/* Mach-O structures differ by pointer width; mirror gumdarwin.h's typedefs. */
#if HX_SIZEOF_VOID_P == 4
# define HOOX_LC_SEGMENT LC_SEGMENT
typedef struct mach_header hoox_mach_header_t;
typedef struct segment_command hoox_segment_command_t;
typedef struct section hoox_section_t;
#else
# define HOOX_LC_SEGMENT LC_SEGMENT_64
typedef struct mach_header_64 hoox_mach_header_t;
typedef struct segment_command_64 hoox_segment_command_t;
typedef struct section_64 hoox_section_t;
#endif

typedef struct _HooxCodeLayout HooxCodeLayout;
typedef struct _HooxCsSuperBlob HooxCsSuperBlob;
typedef struct _HooxCsBlobIndex HooxCsBlobIndex;
typedef struct _HooxCsDirectory HooxCsDirectory;
typedef struct _HooxCsRequirements HooxCsRequirements;
typedef hx_uint HooxSandboxFilterType;

struct _HooxCodeSegment
{
  hx_pointer data;
  hx_size size;
  hx_size virtual_size;
  hx_boolean owns_data;

  hx_int fd;
};

struct _HooxCodeLayout
{
  hx_size header_file_size;

  hx_size text_file_offset;
  hx_size text_file_size;
  hx_size text_size;

  hx_size code_signature_file_offset;
  hx_size code_signature_file_size;
  hx_size code_signature_page_size;
  hx_size code_signature_size;
  hx_size code_signature_hash_count;
  hx_size code_signature_hash_size;
};

struct _HooxCsBlobIndex
{
  hx_uint32 type;
  hx_uint32 offset;
};

struct _HooxCsSuperBlob
{
  hx_uint32 magic;
  hx_uint32 length;
  hx_uint32 count;
  HooxCsBlobIndex index[];
};

struct _HooxCsDirectory
{
  hx_uint32 magic;
  hx_uint32 length;
  hx_uint32 version;
  hx_uint32 flags;
  hx_uint32 hash_offset;
  hx_uint32 ident_offset;
  hx_uint32 num_special_slots;
  hx_uint32 num_code_slots;
  hx_uint32 code_limit;
  hx_uint8 hash_size;
  hx_uint8 hash_type;
  hx_uint8 reserved_1;
  hx_uint8 page_size;
  hx_uint32 reserved_2;
};

struct _HooxCsRequirements
{
  hx_uint32 magic;
  hx_uint32 length;
  hx_uint32 count;
};

enum _HooxSandboxFilterType
{
  HOOX_SANDBOX_FILTER_PATH = 1,
};

static HooxCodeSegment * hoox_code_segment_new_full (hx_pointer data,
    hx_size size, hx_size virtual_size, hx_boolean owns_data);

HX_GNUC_UNUSED static HooxCodeSegment * hoox_code_segment_new_static (
    hx_pointer data, hx_size size);
HX_GNUC_UNUSED static hx_boolean hoox_code_segment_is_realize_supported (void);
HX_GNUC_UNUSED static hx_boolean hoox_code_segment_try_realize (
    HooxCodeSegment * self);
HX_GNUC_UNUSED static hx_boolean hoox_code_segment_try_map (
    HooxCodeSegment * self, hx_size source_offset, hx_size source_size,
    hx_pointer target_address);
static hx_boolean hoox_code_segment_try_remap_locally (HooxCodeSegment * self,
    hx_size source_offset, hx_size source_size, hx_pointer target_address);
#if defined (HAVE_IOS) || defined (HAVE_TVOS)
HX_GNUC_UNUSED static hx_boolean hoox_code_segment_try_remap_using_substrated (
    HooxCodeSegment * self, hx_size source_offset, hx_size source_size,
    hx_pointer target_address);
#endif

static void hoox_code_segment_compute_layout (HooxCodeSegment * self,
    HooxCodeLayout * layout);

static void hoox_put_mach_headers (const hx_char * dylib_path,
    const HooxCodeLayout * layout, hx_pointer output, hx_size * output_size);
static void hoox_put_code_signature (hx_constpointer header,
    hx_constpointer text, const HooxCodeLayout * layout, hx_pointer output);

static hx_int hoox_file_open_tmp (const hx_char * tmpl, hx_char ** name_used);
static hx_char * hoox_build_filename (const hx_char * dir,
    const hx_char * name);
static void hoox_file_write_all (hx_int fd, hx_ssize offset,
    hx_constpointer data, hx_size size);
static hx_boolean hoox_file_check_sandbox_allows (const hx_char * path,
    const hx_char * operation);

static hx_boolean hoox_darwin_check_xnu_version (hx_uint major, hx_uint minor,
    hx_uint micro);

#if defined (HAVE_IOS) || defined (HAVE_TVOS)
static mach_port_t hoox_try_get_substrated_port (void);
static void hoox_deallocate_substrated_port (void);

kern_return_t bootstrap_look_up (mach_port_t bp, const char * service_name,
    mach_port_t * sp);
#endif

hx_boolean
hoox_code_segment_is_supported (void)
{
#if (defined (HAVE_DARWIN) && defined (HAVE_ARM64)) || \
    defined (HAVE_IOS) || defined (HAVE_TVOS)
  /* Not going to work on newer kernels, such as on iOS >= 15.6.1. */
  return !hoox_darwin_check_xnu_version (8020, 142, 0);
#else
  return FALSE;
#endif
}

HooxCodeSegment *
hoox_code_segment_new (hx_size size,
                       const HooxAddressSpec * spec)
{
  hx_size page_size, size_in_pages, virtual_size;
  hx_pointer data;

  page_size = hoox_query_page_size ();
  size_in_pages = size / page_size;
  if (size % page_size != 0)
    size_in_pages++;
  virtual_size = size_in_pages * page_size;

  if (spec == NULL)
  {
    data = hoox_alloc_n_pages ((hx_uint) size_in_pages, HOOX_PAGE_RW);
  }
  else
  {
    data = hoox_try_alloc_n_pages_near ((hx_uint) size_in_pages, HOOX_PAGE_RW,
        spec);
    if (data == NULL)
      return NULL;
  }

  return hoox_code_segment_new_full (data, size, virtual_size, TRUE);
}

HX_GNUC_UNUSED static HooxCodeSegment *
hoox_code_segment_new_static (hx_pointer data,
                              hx_size size)
{
  return hoox_code_segment_new_full (data, size, size, FALSE);
}

static HooxCodeSegment *
hoox_code_segment_new_full (hx_pointer data,
                            hx_size size,
                            hx_size virtual_size,
                            hx_boolean owns_data)
{
  HooxCodeSegment * segment;

  segment = calloc (1, sizeof (HooxCodeSegment));

  segment->data = data;
  segment->size = size;
  segment->virtual_size = virtual_size;
  segment->owns_data = owns_data;

  segment->fd = -1;

  return segment;
}

void
hoox_code_segment_free (HooxCodeSegment * segment)
{
  if (segment == NULL)
    return;

  if (segment->fd != -1)
    close (segment->fd);

  if (segment->owns_data)
    hoox_free_pages (segment->data);

  free (segment);
}

hx_pointer
hoox_code_segment_get_address (HooxCodeSegment * self)
{
  return self->data;
}

hx_size
hoox_code_segment_get_virtual_size (HooxCodeSegment * self)
{
  return self->virtual_size;
}

void
hoox_code_segment_realize (HooxCodeSegment * self)
{
#if defined (HAVE_IOS) || defined (HAVE_TVOS)
  if (hoox_code_segment_is_realize_supported ())
  {
    hoox_code_segment_try_realize (self);
  }
#else
  (void) self;
#endif
}

static hx_boolean
hoox_code_segment_is_realize_supported (void)
{
#if defined (HAVE_IOS) || defined (HAVE_TVOS)
  static hx_size realize_supported = 0;

  if (g_once_init_enter (&realize_supported))
  {
    hx_boolean supported = FALSE;
    hx_pointer scratch_page;
    HooxCodeSegment * segment;

    if (g_file_test ("/usr/libexec/corelliumd", G_FILE_TEST_EXISTS))
      goto not_necessary;

    segment = hoox_code_segment_new (1, NULL);
    scratch_page = hoox_code_segment_get_address (segment);
    supported = hoox_code_segment_try_realize (segment);
    if (supported)
      supported = hoox_code_segment_try_map (segment, 0, 1, scratch_page);
    hoox_code_segment_free (segment);

not_necessary:
    g_once_init_leave (&realize_supported, supported + 1);
  }

  return realize_supported - 1;
#else
  return FALSE;
#endif
}

void
hoox_code_segment_map (HooxCodeSegment * self,
                       hx_size source_offset,
                       hx_size source_size,
                       hx_pointer target_address)
{
  HX_GNUC_UNUSED hx_boolean mapped_successfully;

#if defined (HAVE_IOS) || defined (HAVE_TVOS)
  if (self->fd != -1)
  {
    mapped_successfully = hoox_code_segment_try_map (self, source_offset,
        source_size, target_address);
  }
  else
  {
    mapped_successfully = hoox_code_segment_try_remap_using_substrated (self,
        source_offset, source_size, target_address);
    if (!mapped_successfully)
    {
      mapped_successfully = hoox_code_segment_try_remap_locally (self,
          source_offset, source_size, target_address);
    }
  }
#else
  mapped_successfully = hoox_code_segment_try_remap_locally (self,
      source_offset, source_size, target_address);
#endif

  hx_assert (mapped_successfully);
}

hx_boolean
hoox_code_segment_mark (hx_pointer code,
                        hx_size size,
                        HxError ** error)
{
#if defined (HAVE_IOS) || defined (HAVE_TVOS)
  if (hoox_process_is_debugger_attached ())
    goto fallback;

  if (hoox_code_segment_is_realize_supported ())
  {
    HooxCodeSegment * segment;

    segment = hoox_code_segment_new_static (code, size);

    hoox_code_segment_realize (segment);
    hoox_code_segment_map (segment, 0, size, code);

    hoox_code_segment_free (segment);

    return TRUE;
  }
  else
  {
    mach_port_t server_port;
    mach_vm_address_t address;
    kern_return_t kr;

    server_port = hoox_try_get_substrated_port ();
    if (server_port == MACH_PORT_NULL)
      goto fallback;

    address = HX_POINTER_TO_SIZE (code);

    kr = substrated_mark (server_port, mach_task_self (), address, size,
        &address);

    if (kr != KERN_SUCCESS)
    {
      hx_set_error (error, HOOX_ERROR, HOOX_ERROR_FAILED,
          "Unable to mark code (substrated returned %d)", kr);
      return FALSE;
    }

    return TRUE;
  }

fallback:
#endif
  {
    if (!hoox_try_mprotect (code, size, HOOX_PAGE_RX))
    {
      hx_set_error (error, HOOX_ERROR, HOOX_ERROR_INVALID_ARGUMENT,
          "Invalid address");
      return FALSE;
    }

    return TRUE;
  }
}

HX_GNUC_UNUSED static hx_boolean
hoox_code_segment_try_realize (HooxCodeSegment * self)
{
  hx_char * dylib_path;
  HooxCodeLayout layout;
  hx_uint8 * dylib_header;
  hx_size dylib_header_size;
  hx_uint8 * code_signature;
  hx_int res;
  fsignatures_t sigs;

  self->fd = hoox_file_open_tmp ("hoox-XXXXXX.dylib", &dylib_path);
  if (self->fd == -1)
    return FALSE;

  hoox_code_segment_compute_layout (self, &layout);

  dylib_header = calloc (1, layout.header_file_size);
  hoox_put_mach_headers (dylib_path, &layout, dylib_header, &dylib_header_size);

  code_signature = calloc (1, layout.code_signature_file_size);
  hoox_put_code_signature (dylib_header, self->data, &layout, code_signature);

  hoox_file_write_all (self->fd, HOOX_OFFSET_NONE, dylib_header,
      dylib_header_size);
  hoox_file_write_all (self->fd, layout.text_file_offset, self->data,
      layout.text_size);
  hoox_file_write_all (self->fd, layout.code_signature_file_offset,
      code_signature, layout.code_signature_file_size);

  sigs.fs_file_start = 0;
  sigs.fs_blob_start = HX_SIZE_TO_POINTER (layout.code_signature_file_offset);
  sigs.fs_blob_size = layout.code_signature_file_size;

  res = fcntl (self->fd, F_ADDFILESIGS, &sigs);

  unlink (dylib_path);

  free (code_signature);
  free (dylib_header);
  free (dylib_path);

  return res == 0;
}

HX_GNUC_UNUSED static hx_boolean
hoox_code_segment_try_map (HooxCodeSegment * self,
                           hx_size source_offset,
                           hx_size source_size,
                           hx_pointer target_address)
{
  hx_pointer result;

  result = mmap (target_address, source_size, PROT_READ | PROT_EXEC,
      MAP_PRIVATE | MAP_FIXED, self->fd,
      hoox_query_page_size () + source_offset);

  return result != MAP_FAILED;
}

static hx_boolean
hoox_code_segment_try_remap_locally (HooxCodeSegment * self,
                                     hx_size source_offset,
                                     hx_size source_size,
                                     hx_pointer target_address)
{
  mach_port_t self_task;
  mach_vm_address_t address;
  vm_offset_t source_address;
  vm_prot_t cur_protection, max_protection;
  kern_return_t kr;

  self_task = mach_task_self ();
  address = (mach_vm_address_t) target_address;
  source_address = (vm_offset_t) self->data + source_offset;

  mach_vm_protect (self_task, source_address, source_size, FALSE,
      VM_PROT_READ | VM_PROT_EXECUTE);
  kr = mach_vm_remap (self_task, &address, source_size, 0,
      VM_FLAGS_OVERWRITE | VM_FLAGS_FIXED, self_task, source_address, TRUE,
      &cur_protection, &max_protection, VM_INHERIT_COPY);

  if (kr == KERN_NO_SPACE)
  {
    /* Get rid of permanent map entries in target range. */
    mach_vm_protect (self_task, address, source_size, FALSE,
        VM_PROT_READ | VM_PROT_WRITE | VM_PROT_COPY);

    kr = mach_vm_remap (self_task, &address, source_size, 0,
        VM_FLAGS_OVERWRITE | VM_FLAGS_FIXED, self_task, source_address, TRUE,
        &cur_protection, &max_protection, VM_INHERIT_COPY);

    mach_vm_protect (self_task, address, source_size, FALSE,
        VM_PROT_READ | VM_PROT_EXECUTE);
  }

  return kr == KERN_SUCCESS;
}

#if defined (HAVE_IOS) || defined (HAVE_TVOS)

static hx_boolean
hoox_code_segment_try_remap_using_substrated (HooxCodeSegment * self,
                                              hx_size source_offset,
                                              hx_size source_size,
                                              hx_pointer target_address)
{
  mach_port_t server_port;
  mach_vm_address_t source_address, target_address_value;
  kern_return_t kr;

  server_port = hoox_try_get_substrated_port ();
  if (server_port == MACH_PORT_NULL)
    return FALSE;

  source_address = (mach_vm_address_t) self->data + source_offset;
  target_address_value = HX_POINTER_TO_SIZE (target_address);

  kr = substrated_mark (server_port, mach_task_self (), source_address,
      source_size, &target_address_value);

  return kr == KERN_SUCCESS;
}

#endif

static void
hoox_code_segment_compute_layout (HooxCodeSegment * self,
                                  HooxCodeLayout * layout)
{
  hx_size page_size, cs_page_size, cs_hash_count, cs_hash_size;
  hx_size cs_size, cs_file_size;

  page_size = hoox_query_page_size ();

  layout->header_file_size = page_size;

  layout->text_file_offset = layout->header_file_size;
  layout->text_file_size = self->virtual_size;
  layout->text_size = self->size;

  cs_page_size = 4096;
  cs_hash_count =
      (layout->text_file_offset + layout->text_file_size) / cs_page_size;
  cs_hash_size = HOOX_CS_HASH_SHA1_SIZE;

  cs_size = 125 + (cs_hash_count * cs_hash_size);
  cs_file_size = cs_size;
  if (cs_file_size % 4 != 0)
    cs_file_size += 4 - (cs_file_size % 4);

  layout->code_signature_file_offset =
      layout->text_file_offset + layout->text_file_size;
  layout->code_signature_file_size = cs_file_size;
  layout->code_signature_page_size = cs_page_size;
  layout->code_signature_size = cs_size;
  layout->code_signature_hash_count = cs_hash_count;
  layout->code_signature_hash_size = cs_hash_size;
}

static void
hoox_put_mach_headers (const hx_char * dylib_path,
                       const HooxCodeLayout * layout,
                       hx_pointer output,
                       hx_size * output_size)
{
  hx_size dylib_path_size;
  hoox_mach_header_t * header = output;
  hoox_segment_command_t * seg, * text_segment;
  hoox_section_t * sect;
  struct dylib_command * dl;
  struct linkedit_data_command * sig;

  dylib_path_size = strlen (dylib_path);

  if (sizeof (hx_pointer) == 4)
  {
    header->magic = MH_MAGIC;
    header->cputype = CPU_TYPE_ARM;
    header->cpusubtype = CPU_SUBTYPE_UVAXII;
  }
  else
  {
    header->magic = MH_MAGIC_64;
    header->cputype = CPU_TYPE_ARM64;
    header->cpusubtype = CPU_SUBTYPE_LITTLE_ENDIAN;
  }
  header->filetype = MH_DYLIB;
  header->ncmds = 5;
  header->flags = MH_DYLDLINK | MH_PIE;

  seg = (hoox_segment_command_t *) (header + 1);
  seg->cmd = HOOX_LC_SEGMENT;
  seg->cmdsize = sizeof (hoox_segment_command_t);
  strcpy (seg->segname, SEG_PAGEZERO);
  seg->vmaddr = 0;
  seg->vmsize = hoox_query_page_size ();
  seg->fileoff = 0;
  seg->filesize = 0;
  seg->maxprot = PROT_NONE;
  seg->initprot = PROT_NONE;
  seg->nsects = 0;
  seg->flags = 0;

  seg++;
  seg->cmd = HOOX_LC_SEGMENT;
  seg->cmdsize =
      sizeof (hoox_segment_command_t) + sizeof (hoox_section_t);
  strcpy (seg->segname, SEG_TEXT);
  seg->vmaddr = layout->text_file_offset;
  seg->vmsize = layout->text_file_size;
  seg->fileoff = layout->text_file_offset;
  seg->filesize = layout->text_file_size;
  seg->maxprot = PROT_READ | PROT_WRITE | PROT_EXEC;
  seg->initprot = PROT_READ | PROT_EXEC;
  seg->nsects = 1;
  seg->flags = 0;
  sect = (hoox_section_t *) (seg + 1);
  strcpy (sect->sectname, SECT_TEXT);
  strcpy (sect->segname, SEG_TEXT);
  sect->addr = layout->text_file_offset;
  sect->size = layout->text_size;
  sect->offset = (hx_uint32) layout->text_file_offset;
  sect->align = 4;
  sect->reloff = 0;
  sect->nreloc = 0;
  sect->flags =
      S_REGULAR | S_ATTR_SOME_INSTRUCTIONS | S_ATTR_PURE_INSTRUCTIONS;
  text_segment = seg;

  seg = (hoox_segment_command_t *) (sect + 1);
  seg->cmd = HOOX_LC_SEGMENT;
  seg->cmdsize = sizeof (hoox_segment_command_t);
  strcpy (seg->segname, SEG_LINKEDIT);
  seg->vmaddr = text_segment->vmaddr + text_segment->vmsize;
  seg->vmsize = 4096;
  seg->fileoff = layout->code_signature_file_offset;
  seg->filesize = layout->code_signature_file_size;
  seg->maxprot = PROT_READ;
  seg->initprot = PROT_READ;
  seg->nsects = 0;
  seg->flags = 0;

  dl = (struct dylib_command *) (seg + 1);
  dl->cmd = LC_ID_DYLIB;
  dl->cmdsize = (hx_uint32) (sizeof (struct dylib_command) + dylib_path_size);
  if ((dl->cmdsize % 8) != 0)
    dl->cmdsize += 8 - (dl->cmdsize % 8);
  dl->dylib.name.offset = sizeof (struct dylib_command);
  dl->dylib.timestamp = 0;
  dl->dylib.current_version = 0;
  dl->dylib.compatibility_version = 0;
  memcpy ((hx_char *) (dl + 1), dylib_path, dylib_path_size);

  sig = (struct linkedit_data_command *) (((hx_uint8 *) dl) + dl->cmdsize);
  sig->cmd = LC_CODE_SIGNATURE;
  sig->cmdsize = sizeof (struct linkedit_data_command);
  sig->dataoff = (hx_uint32) layout->code_signature_file_offset;
  sig->datasize = (hx_uint32) layout->code_signature_file_size;

  header->sizeofcmds =
      (hx_uint32) (((hx_uint8 *) (sig + 1)) - ((hx_uint8 *) (header + 1)));

  *output_size = sizeof (hoox_mach_header_t) + header->sizeofcmds;
}

static void
hoox_put_code_signature (hx_constpointer header,
                         hx_constpointer text,
                         const HooxCodeLayout * layout,
                         hx_pointer output)
{
  HooxCsSuperBlob * sb;
  HooxCsBlobIndex * bi;
  HooxCsDirectory * dir;
  hx_uint8 * ident, * hashes;
  hx_size cs_hashes_size, cs_page_size;
  HooxCsRequirements * req;
  hx_size i;

  cs_hashes_size =
      (layout->code_signature_hash_count * layout->code_signature_hash_size);

  sb = output;
  sb->magic = HX_UINT32_TO_BE (HOOX_CS_MAGIC_EMBEDDED_SIGNATURE);
  sb->length = HX_UINT32_TO_BE ((hx_uint32) layout->code_signature_size);
  sb->count = HX_UINT32_TO_BE (2);

  bi = &sb->index[0];
  bi->type = HX_UINT32_TO_BE (0);
  bi->offset = HX_UINT32_TO_BE (28);

  bi = &sb->index[1];
  bi->type = HX_UINT32_TO_BE (2);
  bi->offset = HX_UINT32_TO_BE ((hx_uint32) (113 + cs_hashes_size));

  dir = (HooxCsDirectory *) (bi + 1);

  ident = ((hx_uint8 *) dir) + 44;
  hashes = ident + 41;

  dir->magic = HX_UINT32_TO_BE (HOOX_CS_MAGIC_CODE_DIRECTORY);
  dir->length = HX_UINT32_TO_BE ((hx_uint32) (85 + cs_hashes_size));
  dir->version = HX_UINT32_TO_BE (0x00020001);
  dir->flags = HX_UINT32_TO_BE (0);
  dir->hash_offset = HX_UINT32_TO_BE ((hx_uint32) (hashes - (hx_uint8 *) dir));
  dir->ident_offset = HX_UINT32_TO_BE ((hx_uint32) (ident - (hx_uint8 *) dir));
  dir->num_special_slots = HX_UINT32_TO_BE (2);
  dir->num_code_slots =
      HX_UINT32_TO_BE ((hx_uint32) layout->code_signature_hash_count);
  dir->code_limit = HX_UINT32_TO_BE (
      (hx_uint32) (layout->text_file_offset + layout->text_file_size));
  dir->hash_size = (hx_uint8) layout->code_signature_hash_size;
  dir->hash_type = HOOX_CS_HASH_SHA1;
  dir->page_size = (hx_uint8) log2 (layout->code_signature_page_size);

  req = (HooxCsRequirements *) (hashes + cs_hashes_size);
  req->magic = HX_UINT32_TO_BE (HOOX_CS_MAGIC_REQUIREMENTS);
  req->length = HX_UINT32_TO_BE (12);
  req->count = HX_UINT32_TO_BE (0);

  CC_SHA1 (req, 12, ident + 1);

  cs_page_size = layout->code_signature_page_size;

  for (i = 0; i != layout->header_file_size / cs_page_size; i++)
  {
    CC_SHA1 ((const hx_uint8 *) header + (i * cs_page_size),
        (CC_LONG) cs_page_size, hashes);
    hashes += 20;
  }

  for (i = 0; i != layout->text_file_size / cs_page_size; i++)
  {
    CC_SHA1 ((const hx_uint8 *) text + (i * cs_page_size),
        (CC_LONG) cs_page_size, hashes);
    hashes += 20;
  }
}

static hx_int
hoox_file_open_tmp (const hx_char * tmpl,
                    hx_char ** name_used)
{
  const hx_char * tmp_dir;
  const hx_char * last_x;
  hx_int suffix_len;
  hx_char * path;
  hx_int res;

  /* mkstemps() replaces the trailing run of 'X's and keeps the suffix that
   * follows it (".dylib"); derive the suffix length from the template. */
  last_x = strrchr (tmpl, 'X');
  suffix_len = (last_x != NULL) ? (hx_int) strlen (last_x + 1) : 0;

  tmp_dir = getenv ("TMPDIR");
  if (tmp_dir == NULL)
    tmp_dir = "/tmp";

  path = hoox_build_filename (tmp_dir, tmpl);
  res = mkstemps (path, suffix_len);
  if (res == -1 ||
      !hoox_file_check_sandbox_allows (path, "file-map-executable"))
  {
    if (res != -1)
    {
      close (res);
      unlink (path);
    }
    free (path);
    path = hoox_build_filename ("/Library/Caches", tmpl);
    res = mkstemps (path, suffix_len);
  }

  if (res != -1)
  {
    *name_used = path;
  }
  else
  {
    *name_used = NULL;
    free (path);
  }

  return res;
}

static hx_char *
hoox_build_filename (const hx_char * dir,
                     const hx_char * name)
{
  hx_size size;
  hx_char * path;

  size = strlen (dir) + 1 + strlen (name) + 1;
  path = malloc (size);
  snprintf (path, size, "%s/%s", dir, name);

  return path;
}

static void
hoox_file_write_all (hx_int fd,
                     hx_ssize offset,
                     hx_constpointer data,
                     hx_size size)
{
  hx_ssize written;

  if (offset != HOOX_OFFSET_NONE)
    lseek (fd, offset, SEEK_SET);

  written = 0;
  do
  {
    hx_ssize res;

    res = write (fd, (const hx_uint8 *) data + written, size - written);
    if (res == -1)
    {
      if (errno == EINTR)
        continue;
      else
        return;
    }

    written += res;
  }
  while ((hx_size) written != size);
}

static hx_boolean
hoox_file_check_sandbox_allows (const hx_char * path,
                                const hx_char * operation)
{
  /* Lazily resolved once; a benign race on first use only re-does the
   * dlopen/dlsym work, so no once-init primitive is needed here. */
  static hx_boolean initialized = FALSE;
  static hx_int (* check) (pid_t pid, const hx_char * operation,
      HooxSandboxFilterType type, ...) = NULL;
  static HooxSandboxFilterType no_report = 0;

  if (!initialized)
  {
    void * sandbox;

    sandbox = dlopen ("/usr/lib/system/libsystem_sandbox.dylib",
        RTLD_NOLOAD | RTLD_LAZY);
    if (sandbox != NULL)
    {
      HooxSandboxFilterType * no_report_ptr;

      no_report_ptr = dlsym (sandbox, "SANDBOX_CHECK_NO_REPORT");
      if (no_report_ptr != NULL)
      {
        no_report = *no_report_ptr;

        check = dlsym (sandbox, "sandbox_check");
      }

      dlclose (sandbox);
    }

    initialized = TRUE;
  }

  if (check == NULL)
    return TRUE;

  return !check (getpid (), operation, HOOX_SANDBOX_FILTER_PATH | no_report,
      path);
}

static hx_boolean
hoox_darwin_check_xnu_version (hx_uint major,
                               hx_uint minor,
                               hx_uint micro)
{
  static hx_boolean initialized = FALSE;
  static hx_uint xnu_major = HX_MAXUINT;
  static hx_uint xnu_minor = HX_MAXUINT;
  static hx_uint xnu_micro = HX_MAXUINT;

  if (!initialized)
  {
    char buf[256] = { 0, };
    size_t size;
    HX_GNUC_UNUSED int res;
    const char * version_str;

    size = sizeof (buf);
    res = sysctlbyname ("kern.version", buf, &size, NULL, 0);
    if (res == 0)
    {
      version_str = strstr (buf, "xnu-");
      if (version_str != NULL)
      {
        version_str += 4;
        sscanf (version_str, "%u.%u.%u", &xnu_major, &xnu_minor, &xnu_micro);
      }
    }

    initialized = TRUE;
  }

  if (xnu_major > major)
    return TRUE;

  if (xnu_major == major && xnu_minor > minor)
    return TRUE;

  return xnu_major == major && xnu_minor == minor && xnu_micro >= micro;
}

#if defined (HAVE_IOS) || defined (HAVE_TVOS)

static mach_port_t
hoox_try_get_substrated_port (void)
{
  static hx_size cached_result = 0;

  if (g_once_init_enter (&cached_result))
  {
    mach_port_t server_port = MACH_PORT_NULL;

    if (getpid () == 1)
    {
      host_get_special_port (mach_host_self (), HOST_LOCAL_NODE,
          HOST_LOCKD_PORT, &server_port);
    }
    else
    {
      mach_port_t self_task, bootstrap_port;

      self_task = mach_task_self ();

      if (task_get_bootstrap_port (self_task, &bootstrap_port) == KERN_SUCCESS)
      {
        bootstrap_look_up (bootstrap_port, "cy:com.saurik.substrated",
            &server_port);

        mach_port_deallocate (self_task, bootstrap_port);
      }
    }

    if (server_port != MACH_PORT_NULL)
      _gum_register_destructor (hoox_deallocate_substrated_port);

    g_once_init_leave (&cached_result, server_port + 1);
  }

  return cached_result - 1;
}

static void
hoox_deallocate_substrated_port (void)
{
  mach_port_deallocate (mach_task_self (), hoox_try_get_substrated_port ());
}

#endif
