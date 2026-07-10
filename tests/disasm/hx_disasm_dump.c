/*
 * Differential-test helper (batch mode): read one hex byte-string per line
 * from stdin and print, per line, the decode of the FIRST instruction as
 * `len id rip target` ("- - - -" if not decodable), for cross-checking
 * against capstone (see diff_capstone.py). One process handles the whole
 * corpus, so the harness stays fast.
 *
 * Usage: hx_disasm_dump <32|64>   (hex lines on stdin)
 *
 * Licence: wxWindows Library Licence, Version 3.1
 */

#include "capstone.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int
hexval (char c)
{
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}

int
main (int argc, char ** argv)
{
  csh handle;
  cs_mode mode;
  char line[512];
  cs_insn * insn;

  if (argc < 2)
  {
    fprintf (stderr, "usage: %s <32|64>  (hex lines on stdin)\n", argv[0]);
    return 2;
  }

  mode = (strcmp (argv[1], "64") == 0) ? CS_MODE_64 : CS_MODE_32;

  if (cs_open (CS_ARCH_X86, mode, &handle) != CS_ERR_OK)
    return 1;
  cs_option (handle, CS_OPT_DETAIL, CS_OPT_ON);
  insn = cs_malloc (handle);

  while (fgets (line, sizeof (line), stdin) != NULL)
  {
    uint8_t buf[256];
    size_t len = 0;
    size_t i;
    const uint8_t * code;
    size_t remaining;
    uint64_t address = 0x100000;

    for (i = 0; line[i] != '\0' && line[i + 1] != '\0' && len < sizeof (buf);
        i += 2)
    {
      int hi = hexval (line[i]);
      int lo = hexval (line[i + 1]);
      if (hi < 0 || lo < 0)
        break;
      buf[len++] = (uint8_t) ((hi << 4) | lo);
    }

    code = buf;
    remaining = len;

    if (len != 0 && cs_disasm_iter (handle, &code, &remaining, &address, insn))
    {
      cs_x86 * x86 = &insn->detail->x86;
      int rip = (x86->op_count >= 1 &&
          x86->operands[0].type == X86_OP_MEM &&
          x86->operands[0].mem.base == X86_REG_RIP) ? 1 : 0;
      long long target = -1;
      if (x86->op_count >= 1 && x86->operands[0].type == X86_OP_IMM)
        target = (long long) x86->operands[0].imm;
      printf ("%u %u %d %lld\n", insn->size, insn->id, rip, target);
    }
    else
    {
      printf ("- - - -\n");
    }
    fflush (stdout);
  }

  cs_free (insn, 1);
  cs_close (&handle);

  return 0;
}
