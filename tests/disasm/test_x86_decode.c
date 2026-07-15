/*
 * Golden-vector test for address-size handling in the in-tree x86 decoder.
 *
 * Licence: wxWindows Library Licence, Version 3.1
 */

#include <hx_disasm.h>

#include <stdint.h>
#include <stdio.h>

static int failures = 0;

#define CHECK(expr)                                                           \
    do                                                                        \
    {                                                                         \
      if (!(expr))                                                            \
      {                                                                       \
        fprintf (stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #expr);      \
        failures++;                                                           \
      }                                                                       \
    }                                                                         \
    while (0)

static void
decode_and_check (hx_csh handle,
                  const uint8_t * bytes,
                  size_t input_size,
                  uint16_t expected_size,
                  hx_x86_reg expected_base,
                  uint8_t expected_disp_size,
                  int64_t expected_disp)
{
  const uint8_t * code = bytes;
  size_t remaining = input_size;
  uint64_t address = 0x100000;
  hx_insn * insn = hx_insn_alloc (handle);
  const hx_x86 * x86;

  CHECK (insn != NULL);
  if (insn == NULL)
    return;

  CHECK (hx_disasm_iter (handle, &code, &remaining, &address, insn));
  x86 = &insn->detail->x86;
  CHECK (insn->size == expected_size);
  CHECK (code == bytes + expected_size);
  CHECK (remaining == input_size - expected_size);
  CHECK (x86->operands[0].type == HX_OP_MEM);
  CHECK (x86->operands[0].mem.base == expected_base);
  CHECK (x86->encoding.disp_size == expected_disp_size);
  CHECK (x86->disp == expected_disp);

  hx_insn_free (insn, 1);
}

int
main (void)
{
  hx_csh h32, h64;
  static const uint8_t addr16_disp16[] = {
    0x67, 0x01, 0xb1, 0xd2, 0xc3, 0x22, 0x75
  };
  static const uint8_t addr16_no_sib[] = {
    0x67, 0x8b, 0x24, 0x11
  };
  static const uint8_t addr16_absolute[] = {
    0x67, 0x8b, 0x06, 0x34, 0x12
  };
  static const uint8_t eip_relative[] = {
    0x67, 0x8b, 0x05, 0x78, 0x56, 0x34, 0x12
  };
  static const uint8_t rip_relative[] = {
    0x8b, 0x05, 0x78, 0x56, 0x34, 0x12
  };

  CHECK (hx_open (HX_ARCH_X86, HX_MODE_32, &h32) == HX_ERR_OK);
  CHECK (hx_open (HX_ARCH_X86, HX_MODE_64, &h64) == HX_ERR_OK);
  if (failures != 0)
    return 1;

  hx_option (h32, HX_OPT_DETAIL, HX_OPT_ON);
  hx_option (h64, HX_OPT_DETAIL, HX_OPT_ON);

  decode_and_check (h32, addr16_disp16, sizeof (addr16_disp16), 5,
      HX_REG_RBX, 2, (int16_t) 0xc3d2);
  decode_and_check (h32, addr16_no_sib, sizeof (addr16_no_sib), 3,
      HX_REG_RSI, 0, 0);
  decode_and_check (h32, addr16_absolute, sizeof (addr16_absolute), 5,
      HX_REG_INVALID, 2, 0x1234);
  decode_and_check (h64, eip_relative, sizeof (eip_relative), 7,
      HX_REG_EIP, 4, 0x12345678);
  decode_and_check (h64, rip_relative, sizeof (rip_relative), 6,
      HX_REG_RIP, 4, 0x12345678);

  hx_close (&h32);
  hx_close (&h64);

  if (failures == 0)
  {
    printf ("x86 decoder: all tests passed\n");
    return 0;
  }
  printf ("x86 decoder: %d failure(s)\n", failures);
  return 1;
}
