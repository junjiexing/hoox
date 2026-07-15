/*
 * Copyright (C) 2009-2022 Ole André Vadla Ravnås <oleavr@nowsecure.com>
 *
 * Licence: wxWindows Library Licence, Version 3.1
 */

#include "x86relocator-fixture.c"

TESTLIST_BEGIN (x86relocator)
  TESTENTRY (one_to_one)
  TESTENTRY (call_near_relative)
  TESTENTRY (call_near_relative_to_next_instruction)
#if HX_SIZEOF_VOID_P == 4
  TESTENTRY (call_near_gnu_get_pc_thunk)
  TESTENTRY (call_near_android_get_pc_thunk)
  TESTENTRY (call_near_indirect)
#endif
  TESTENTRY (jmp_short_outside_block)
  TESTENTRY (jmp_near_outside_block)
  TESTENTRY (jmp_register)
  TESTENTRY (jmp_indirect)
  TESTENTRY (jcc_short_within_block)
  TESTENTRY (jcc_short_outside_block)
  TESTENTRY (jcc_near_outside_block)
  TESTENTRY (jcxz_short_within_block)
  TESTENTRY (jcxz_short_outside_block)
  TESTENTRY (peek_next_write)
  TESTENTRY (skip_instruction)
  TESTENTRY (eob_and_eoi_on_jmp)
  TESTENTRY (eob_but_not_eoi_on_call)
  TESTENTRY (eob_and_eoi_on_ret)
  TESTENTRY (eob_but_not_eoi_on_jcc)
  TESTENTRY (eob_but_not_eoi_on_jcxz)

#if HX_SIZEOF_VOID_P == 8
  TESTENTRY (rip_relative_move_different_target)
  TESTENTRY (rip_relative_move_same_target)
  TESTENTRY (rip_relative_push)
  TESTENTRY (rip_relative_push_red_zone)
  TESTENTRY (rip_relative_cmpxchg)
  TESTENTRY (rip_relative_call)
  TESTENTRY (rip_relative_adjust_offset)
  TESTENTRY (eip_relative_adjust_offset)
#endif
TESTLIST_END ()

TESTCASE (one_to_one)
{
  hx_uint8 input[] = {
    0x55,                         /* push ebp     */
    0x8b, 0xec,                   /* mov ebp, esp */
  };
  const hx_insn * insn;

  SETUP_RELOCATOR_WITH (input);

  insn = NULL;
  hx_assert_cmpuint (hoox_x86_relocator_read_one (&fixture->rl, &insn), ==, 1);
  hx_assert_cmpint (insn->id, ==, HX_INS_PUSH);
  assert_outbuf_still_zeroed_from_offset (0);

  insn = NULL;
  hx_assert_cmpuint (hoox_x86_relocator_read_one (&fixture->rl, &insn), ==, 3);
  hx_assert_cmpint (insn->id, ==, HX_INS_MOV);
  assert_outbuf_still_zeroed_from_offset (0);

  hx_assert_true (hoox_x86_relocator_write_one (&fixture->rl));
  hx_assert_cmpint (memcmp (fixture->output, input, 1), ==, 0);
  assert_outbuf_still_zeroed_from_offset (1);

  hx_assert_true (hoox_x86_relocator_write_one (&fixture->rl));
  hx_assert_cmpint (memcmp (fixture->output + 1, input + 1, 2), ==, 0);
  assert_outbuf_still_zeroed_from_offset (3);

  hx_assert_false (hoox_x86_relocator_write_one (&fixture->rl));
}

TESTCASE (call_near_relative)
{
  hx_uint8 input[] = {
    0x55,                         /* push ebp     */
    0x8b, 0xec,                   /* mov ebp, esp */
    0xe8, 0x04, 0x00, 0x00, 0x00, /* call dummy   */
    0x8b, 0xe5,                   /* mov esp, ebp */
    0x5d,                         /* pop ebp      */
    0xc3,                         /* retn         */

/* dummy:                                         */
    0xc3                          /* retn         */
  };
  hx_int32 reloc_distance, expected_distance;

  SETUP_RELOCATOR_WITH (input);

  hoox_x86_relocator_read_one (&fixture->rl, NULL);
  hoox_x86_relocator_read_one (&fixture->rl, NULL);
  hx_assert_cmpuint (hoox_x86_relocator_read_one (&fixture->rl, NULL), ==, 8);

  hoox_x86_relocator_write_all (&fixture->rl);
  hx_assert_cmpint (memcmp (fixture->output + 3, input + 3, 5), !=, 0);
  reloc_distance = *((hx_int32 *) (fixture->output + 4));
  expected_distance =
      (hx_int32) (((hx_ssize) (input + 12)) - ((hx_ssize) (fixture->output + 8)));
  hx_assert_cmpint (reloc_distance, ==, expected_distance);
}

TESTCASE (call_near_relative_to_next_instruction)
{
  hx_uint8 input[] = {
    0xe8, 0x00, 0x00, 0x00, 0x00, /* call +0         */
    0x59                          /* pop xcx         */
  };
#if HX_SIZEOF_VOID_P == 8
  hx_uint8 expected_output[] = {
    0x50,                         /* push rax        */
    0x48, 0xb8,                   /* mov rax, <imm>  */
          0x00, 0x00, 0x00, 0x00,
          0x00, 0x00, 0x00, 0x00,
    0x48, 0x87, 0x04, 0x24        /* xchg rax, [rsp] */
  };

  *((hx_pointer *) (expected_output + 3)) = input + 5;
#else
  hx_uint8 expected_output[] = {
    0x68, 0x00, 0x00, 0x00, 0x00  /* push <imm> */
  };

  *((hx_pointer *) (expected_output + 1)) = input + 5;
#endif

  SETUP_RELOCATOR_WITH (input);

  hx_assert_cmpuint (hoox_x86_relocator_read_one (&fixture->rl, NULL), ==, 5);
  hx_assert_false (hoox_x86_relocator_eob (&fixture->rl));
  hoox_x86_relocator_write_all (&fixture->rl);
  hx_assert_cmpuint (hoox_x86_writer_offset (&fixture->cw), ==,
      sizeof (expected_output));
  hx_assert_cmpint (memcmp (fixture->output, expected_output,
      sizeof (expected_output)), ==, 0);
}

#if HX_SIZEOF_VOID_P == 4

TESTCASE (call_near_gnu_get_pc_thunk)
{
  const hx_uint8 input[] = {
    0xe8, 0x01, 0x00, 0x00, 0x00, /* call +1         */

    0xcc,                         /* int 3          */
    0x8b, 0x0c, 0x24,             /* mov ecx, [esp] */
    0xc3                          /* ret            */
  };
  hx_uint8 expected_output[] = {
    0xb9, 0x00, 0x00, 0x00, 0x00  /* mov ecx, <imm> */
  };

  *((hx_uint32 *) (expected_output + 1)) = HX_POINTER_TO_SIZE (input + 5);

  hoox_x86_writer_set_target_cpu (&fixture->cw, HOOX_CPU_IA32);
  SETUP_RELOCATOR_WITH (input);

  hx_assert_cmpuint (hoox_x86_relocator_read_one (&fixture->rl, NULL), ==, 5);
  hx_assert_false (hoox_x86_relocator_eob (&fixture->rl));
  hoox_x86_relocator_write_all (&fixture->rl);
  hx_assert_cmpuint (hoox_x86_writer_offset (&fixture->cw), ==,
      sizeof (expected_output));
  hx_assert_cmpint (memcmp (fixture->output, expected_output,
      sizeof (expected_output)), ==, 0);
}

TESTCASE (call_near_android_get_pc_thunk)
{
  const hx_uint8 input[] = {
    0xe8, 0x01, 0x00, 0x00, 0x00, /* call +1         */

    0xcc,                         /* int 3          */
    0x8b, 0x1c, 0x24,             /* mov ebx, [esp] */
    0xc3                          /* ret            */
  };
  hx_uint8 expected_output[] = {
    0xbb, 0x00, 0x00, 0x00, 0x00  /* mov ebx, <imm> */
  };

  *((hx_uint32 *) (expected_output + 1)) = HX_POINTER_TO_SIZE (input + 5);

  hoox_x86_writer_set_target_cpu (&fixture->cw, HOOX_CPU_IA32);
  SETUP_RELOCATOR_WITH (input);

  hx_assert_cmpuint (hoox_x86_relocator_read_one (&fixture->rl, NULL), ==, 5);
  hx_assert_false (hoox_x86_relocator_eob (&fixture->rl));
  hoox_x86_relocator_write_all (&fixture->rl);
  hx_assert_cmpuint (hoox_x86_writer_offset (&fixture->cw), ==,
      sizeof (expected_output));
  hx_assert_cmpint (memcmp (fixture->output, expected_output,
      sizeof (expected_output)), ==, 0);
}

#endif

TESTCASE (call_near_indirect)
{
  hx_uint8 input[] = {
    0xff, 0x15, 0x78, 0x56, 0x34, 0x12 /* call ds:012345678h */
  };

  SETUP_RELOCATOR_WITH (input);

  hx_assert_cmpuint (hoox_x86_relocator_read_one (&fixture->rl, NULL), ==, 6);
  hoox_x86_relocator_write_one (&fixture->rl);
  hx_assert_cmpint (memcmp (fixture->output, input, 6), ==, 0);
}

TESTCASE (jmp_short_outside_block)
{
  hx_uint8 input[] = {
    0xeb, 0x01  /* jmp +1 */
  };
  const hx_ssize input_end = HX_POINTER_TO_SIZE (input) + HX_N_ELEMENTS (input);
  hx_int32 reloc_distance, expected_distance;

  SETUP_RELOCATOR_WITH (input);

  hoox_x86_relocator_read_one (&fixture->rl, NULL);
  hoox_x86_relocator_write_one (&fixture->rl);

  hx_assert_cmpuint (hoox_x86_writer_offset (&fixture->cw), ==, 5);

  hx_assert_cmphex (fixture->output[0], !=, input[0]);
  hx_assert_cmphex (fixture->output[0], ==, 0xe9);

  reloc_distance = *((hx_int32 *) (fixture->output + 1));
  expected_distance =
      (hx_int32) ((input_end + 1) - ((hx_ssize) (fixture->output + 5)));
  hx_assert_cmpint (reloc_distance, ==, expected_distance);
}

TESTCASE (jmp_near_outside_block)
{
  hx_uint8 input[] = {
    0xe9, 0x01, 0x00, 0x00, 0x00, /* jmp +1 */
  };
  const hx_ssize input_end = HX_POINTER_TO_SIZE (input) + HX_N_ELEMENTS (input);
  hx_int32 reloc_distance, expected_distance;

  SETUP_RELOCATOR_WITH (input);

  hoox_x86_relocator_read_one (&fixture->rl, NULL);
  hoox_x86_relocator_write_one (&fixture->rl);

  hx_assert_cmpuint (hoox_x86_writer_offset (&fixture->cw), ==, sizeof (input));

  hx_assert_cmphex (fixture->output[0], ==, input[0]);

  reloc_distance = *((hx_int32 *) (fixture->output + 1));
  expected_distance =
      (hx_int32) ((input_end + 1) - ((hx_ssize) (fixture->output + 5)));
  hx_assert_cmpint (reloc_distance, ==, expected_distance);
}

TESTCASE (jmp_register)
{
  hx_uint8 input[] = {
    0xff, 0xe0 /* jmp eax */
  };

  SETUP_RELOCATOR_WITH (input);

  hoox_x86_relocator_read_one (&fixture->rl, NULL);
  hoox_x86_relocator_write_one (&fixture->rl);

  hx_assert_cmpuint (hoox_x86_writer_offset (&fixture->cw), ==, sizeof (input));
  hx_assert_cmpint (memcmp (fixture->output, input, 2), ==, 0);
}

TESTCASE (jmp_indirect)
{
  hx_uint8 input[] = {
#if HX_SIZEOF_VOID_P == 8
    0x48,
#endif
    0xff, 0x60, 0x08
  };

  SETUP_RELOCATOR_WITH (input);

  hoox_x86_relocator_read_one (&fixture->rl, NULL);
  hoox_x86_relocator_write_one (&fixture->rl);

  hx_assert_cmpuint (hoox_x86_writer_offset (&fixture->cw), == , sizeof (input));
  hx_assert_cmpint (memcmp (fixture->output, input, sizeof (input)), == , 0);
}

TESTCASE (jcc_short_within_block)
{
  hx_uint8 input[] = {
    0x31, 0xc0,                         /* xor eax,eax */
    0x81, 0xfb, 0x2a, 0x00, 0x00, 0x00, /* cmp ebx, 42 */
    0x75, 0x02,                         /* jnz beach   */
    0xff, 0xc0,                         /* inc eax     */

/* beach:                                              */
    0xc3                                /* retn        */
  };

  SETUP_RELOCATOR_WITH (input);

  hoox_x86_relocator_read_one (&fixture->rl, NULL);
  hoox_x86_relocator_read_one (&fixture->rl, NULL);
  hx_assert_cmpuint (hoox_x86_relocator_read_one (&fixture->rl, NULL), ==, 10);
  hoox_x86_relocator_read_one (&fixture->rl, NULL);
  hx_assert_cmpuint (hoox_x86_relocator_read_one (&fixture->rl, NULL), ==,
      sizeof (input));

  hoox_x86_relocator_write_one (&fixture->rl);
  hoox_x86_relocator_write_one (&fixture->rl);
  hoox_x86_relocator_write_one (&fixture->rl);
  hoox_x86_relocator_write_one (&fixture->rl);
  hoox_x86_writer_put_inc_reg (&fixture->cw, HOOX_HX_EAX);
  hoox_x86_relocator_write_one (&fixture->rl);

  hoox_x86_writer_flush (&fixture->cw);

  /* output should have one extra instruction of 2 bytes */
  hx_assert_cmpuint (hoox_x86_writer_offset (&fixture->cw), ==,
      sizeof (input) + 2);

  /* the first 9 bytes should be the same */
  hx_assert_cmpint (memcmp (fixture->output, input, 9), ==, 0);

  /* the jnz offset should be adjusted to account for the extra instruction */
  hx_assert_cmpint ((hx_int8) fixture->output[9], ==, ((hx_int8) input[9]) + 2);

  /* the rest should be the same */
  hx_assert_cmpint (memcmp (fixture->output + 10 + 2, input + 10, 3), ==, 0);
}

TESTCASE (jcc_short_outside_block)
{
  hx_uint8 input[] = {
    0x75, 0xfd, /* jnz -3 */
    0xc3        /* retn   */
  };
  const hx_ssize input_start = HX_POINTER_TO_SIZE (input);

  SETUP_RELOCATOR_WITH (input);

  hoox_x86_relocator_read_one (&fixture->rl, NULL);
  hoox_x86_relocator_read_one (&fixture->rl, NULL);
  hoox_x86_relocator_write_all (&fixture->rl);

  hx_assert_cmpuint (hoox_x86_writer_offset (&fixture->cw), ==, 6 + 1);
  hx_assert_cmphex (fixture->output[0], ==, 0x0f);
  hx_assert_cmphex (fixture->output[1], ==, 0x85);
  hx_assert_cmpint (*((hx_int32 *) (fixture->output + 2)), ==,
      (input_start - 1) - (hx_ssize) (fixture->output + 6));
  hx_assert_cmphex (fixture->output[6], ==, input[2]);
}

TESTCASE (jcc_near_outside_block)
{
  hx_uint8 input[] = {
    0x0f, 0x84, 0xda, 0x00, 0x00, 0x00, /* jz +218 */
    0xc3                                /* retn    */
  };
  const hx_ssize retn_start = HX_POINTER_TO_SIZE (input) + 6;

  SETUP_RELOCATOR_WITH (input);

  hoox_x86_relocator_read_one (&fixture->rl, NULL);
  hoox_x86_relocator_read_one (&fixture->rl, NULL);
  hoox_x86_relocator_write_all (&fixture->rl);

  hx_assert_cmpuint (hoox_x86_writer_offset (&fixture->cw), ==, 6 + 1);
  hx_assert_cmphex (fixture->output[0], ==, 0x0f);
  hx_assert_cmphex (fixture->output[1], ==, 0x84);
  hx_assert_cmpint (*((hx_int32 *) (fixture->output + 2)), ==,
      (retn_start + 218) - (hx_ssize) (fixture->output + 6));
  hx_assert_cmphex (fixture->output[6], ==, input[6]);
}

TESTCASE (jcxz_short_within_block)
{
  hx_uint8 input[] = {
    0xe3, 0x02,                         /* jecxz/jrcxz beach */
    0xff, 0xc0,                         /* inc eax           */

/* beach:                                                    */
    0xc3                                /* retn              */
  };
  const hx_uint8 expected_output[] = {
    0xe3, 0x04,                         /* jecxz/jrcxz beach */
    0xff, 0xc0,                         /* inc eax           */
    0xff, 0xc0,                         /* inc eax           */

/* beach:                                                    */
    0xc3                                /* retn              */
  };

  SETUP_RELOCATOR_WITH (input);

  hx_assert_cmpuint (hoox_x86_relocator_read_one (&fixture->rl, NULL), ==, 2);
  hx_assert_cmpuint (hoox_x86_relocator_read_one (&fixture->rl, NULL), ==, 4);
  hx_assert_cmpuint (hoox_x86_relocator_read_one (&fixture->rl, NULL), ==, 5);

  hoox_x86_relocator_write_one (&fixture->rl);
  hoox_x86_relocator_write_one (&fixture->rl);
  hoox_x86_writer_put_inc_reg (&fixture->cw, HOOX_HX_EAX);
  hoox_x86_relocator_write_one (&fixture->rl);

  hoox_x86_writer_flush (&fixture->cw);

  assert_output_equals (expected_output);
}

TESTCASE (jcxz_short_outside_block)
{
  hx_uint8 input[] = {
    0xe3, 0xfd, /* jecxz/jrcxz -3      */
    0xc3        /* retn                */
  };
  const hx_ssize retn_start = HX_POINTER_TO_SIZE (input) + 2;
  hx_uint8 expected_output[] = {
    0xe3, 0x02, /* jecxz/jrcxz is_true */
    0xeb, 0x05, /* jmp is_false        */

/* is_true:                            */
    0xe9, 0xaa, 0xaa, 0xaa, 0xaa,

/* is_false:                           */
    0xc3        /* retn                */
  };

  *((hx_int32 *) (expected_output + 5)) =
      (hx_int32) ((retn_start - 3) - ((hx_ssize) (fixture->output + 9)));

  SETUP_RELOCATOR_WITH (input);

  hoox_x86_relocator_read_one (&fixture->rl, NULL);
  hoox_x86_relocator_read_one (&fixture->rl, NULL);
  hoox_x86_relocator_write_all (&fixture->rl);

  hoox_x86_writer_flush (&fixture->cw);

  assert_output_equals (expected_output);
}

TESTCASE (peek_next_write)
{
  hx_uint8 input[] = {
    0x31, 0xc0, /* xor eax,eax */
    0xff, 0xc0  /* inc eax     */
  };

  SETUP_RELOCATOR_WITH (input);

  hoox_x86_relocator_read_one (&fixture->rl, NULL);
  hoox_x86_relocator_read_one (&fixture->rl, NULL);

  hx_assert_cmpint (hoox_x86_relocator_peek_next_write_insn (&fixture->rl)->id,
      ==, HX_INS_XOR);
  hoox_x86_relocator_write_one (&fixture->rl);
  hx_assert_cmpint (hoox_x86_relocator_peek_next_write_insn (&fixture->rl)->id,
      ==, HX_INS_INC);
  hx_assert_cmpint (hoox_x86_relocator_peek_next_write_insn (&fixture->rl)->id,
      ==, HX_INS_INC);
  hx_assert_true (hoox_x86_relocator_peek_next_write_source (&fixture->rl)
      == input + 2);
  hx_assert_true (hoox_x86_relocator_write_one (&fixture->rl));
  hx_assert_null (hoox_x86_relocator_peek_next_write_insn (&fixture->rl));
  hx_assert_null (hoox_x86_relocator_peek_next_write_source (&fixture->rl));
  hx_assert_false (hoox_x86_relocator_write_one (&fixture->rl));
}

TESTCASE (skip_instruction)
{
  hx_uint8 input[] = {
    0x31, 0xc0,                         /* xor eax,eax */
    0x81, 0xfb, 0x2a, 0x00, 0x00, 0x00, /* cmp ebx, 42 */
    0x75, 0x02,                         /* jnz beach   */
    0xff, 0xc0,                         /* inc eax     */

/* beach:                                              */
    0xc3                                /* retn        */
  };

  SETUP_RELOCATOR_WITH (input);

  while (!hoox_x86_relocator_eoi (&fixture->rl))
    hoox_x86_relocator_read_one (&fixture->rl, NULL);

  hoox_x86_relocator_write_one (&fixture->rl);
  hoox_x86_relocator_write_one (&fixture->rl);
  hoox_x86_relocator_write_one (&fixture->rl);
  hoox_x86_relocator_write_one (&fixture->rl);
  hoox_x86_relocator_skip_one (&fixture->rl); /* skip retn */
  hoox_x86_writer_put_inc_reg (&fixture->cw, HOOX_HX_EAX); /* put "inc eax"
                                                           * there instead */

  hoox_x86_writer_flush (&fixture->cw);

  /* output should be of almost the same size */
  hx_assert_cmpuint (hoox_x86_writer_offset (&fixture->cw), ==,
      sizeof (input) + 1);

  /* the first n - 1 bytes should be the same */
  hx_assert_cmpint (memcmp (fixture->output, input, sizeof (input) - 1), ==, 0);
}

TESTCASE (eob_and_eoi_on_jmp)
{
  hx_uint8 input[] = {
    0xeb, 0x01  /* jmp +1 */
  };

  SETUP_RELOCATOR_WITH (input);

  hx_assert_cmpuint (hoox_x86_relocator_read_one (&fixture->rl, NULL), ==, 2);
  hx_assert_true (hoox_x86_relocator_eob (&fixture->rl));
  hx_assert_true (hoox_x86_relocator_eoi (&fixture->rl));
  hx_assert_cmpuint (hoox_x86_relocator_read_one (&fixture->rl, NULL), ==, 0);
}

TESTCASE (eob_but_not_eoi_on_call)
{
  hx_uint8 input[] = {
    0xe8, 0x42, 0x00, 0x00, 0x00  /* call +0x42 */
  };

  SETUP_RELOCATOR_WITH (input);

  hx_assert_cmpuint (hoox_x86_relocator_read_one (&fixture->rl, NULL), ==, 5);
  hx_assert_true (hoox_x86_relocator_eob (&fixture->rl));
  hx_assert_false (hoox_x86_relocator_eoi (&fixture->rl));
}

TESTCASE (eob_and_eoi_on_ret)
{
  hx_uint8 input[] = {
    0xc2, 0x04, 0x00  /* retn 4 */
  };

  SETUP_RELOCATOR_WITH (input);

  hx_assert_cmpuint (hoox_x86_relocator_read_one (&fixture->rl, NULL), ==, 3);
  hx_assert_true (hoox_x86_relocator_eob (&fixture->rl));
  hx_assert_true (hoox_x86_relocator_eoi (&fixture->rl));
  hx_assert_cmpuint (hoox_x86_relocator_read_one (&fixture->rl, NULL), ==, 0);
}

TESTCASE (eob_but_not_eoi_on_jcc)
{
  hx_uint8 input[] = {
    0x74, 0x01, /* jz +1  */
    0xc3        /* ret    */
  };

  SETUP_RELOCATOR_WITH (input);

  hx_assert_cmpuint (hoox_x86_relocator_read_one (&fixture->rl, NULL), ==, 2);
  hx_assert_true (hoox_x86_relocator_eob (&fixture->rl));
  hx_assert_false (hoox_x86_relocator_eoi (&fixture->rl));
  hx_assert_cmpuint (hoox_x86_relocator_read_one (&fixture->rl, NULL), ==, 3);
  hx_assert_true (hoox_x86_relocator_eoi (&fixture->rl));
  hx_assert_cmpuint (hoox_x86_relocator_read_one (&fixture->rl, NULL), ==, 0);
}

TESTCASE (eob_but_not_eoi_on_jcxz)
{
  hx_uint8 input[] = {
    0xe3, 0x01, /* jecxz/jrcxz +1 */
    0xc3        /* ret            */
  };

  SETUP_RELOCATOR_WITH (input);

  hx_assert_cmpuint (hoox_x86_relocator_read_one (&fixture->rl, NULL), ==, 2);
  hx_assert_true (hoox_x86_relocator_eob (&fixture->rl));
  hx_assert_false (hoox_x86_relocator_eoi (&fixture->rl));
  hx_assert_cmpuint (hoox_x86_relocator_read_one (&fixture->rl, NULL), ==, 3);
  hx_assert_true (hoox_x86_relocator_eoi (&fixture->rl));
  hx_assert_cmpuint (hoox_x86_relocator_read_one (&fixture->rl, NULL), ==, 0);
}

#if HX_SIZEOF_VOID_P == 8

TESTCASE (rip_relative_move_different_target)
{
  static hx_uint8 input[] = {
    0x8b, 0x15, 0x01, 0x00, 0x00, 0x00, /* mov edx, [rip + 1] */
    0xc3,                               /* ret                */
    0x01, 0x02, 0x03, 0x04
  };
  hx_uint8 expected_output[] = {
    0x50,                               /* push rax           */
    0x48, 0xb8, 0xff, 0xff, 0xff, 0xff, /* mov rax, <rip>     */
                0xff, 0xff, 0xff, 0xff,
    0x8b, 0x90, 0x01, 0x00, 0x00, 0x00, /* mov edx, [rax + 1] */
    0x58                                /* pop rax            */
  };

  /*
   * Since our test fixture writes our output to a stack buffer, we mark our
   * input buffer as static so that it is part of the .data section and thus
   * more than 2GB from the stack. This means we can test the cases when the
   * offset to the RIP relative instruction can't simply be modified.
   */
  hx_assert (((input - expected_output) < HX_MININT32) ||
      ((input - expected_output) > HX_MAXINT32));

  *((hx_pointer *) (expected_output + 3)) = (hx_pointer) (input + 6);

  hoox_x86_writer_set_target_abi (&fixture->cw, HOOX_ABI_WINDOWS);
  SETUP_RELOCATOR_WITH (input);

  hoox_x86_relocator_read_one (&fixture->rl, NULL);
  hoox_x86_relocator_write_one (&fixture->rl);
  assert_output_equals (expected_output);
}

TESTCASE (rip_relative_move_same_target)
{
  static hx_uint8 input[] = {
    0x8b, 0x05, 0x01, 0x00, 0x00, 0x00, /* mov eax, [rip + 1] */
    0xc3,                               /* ret                */
    0x01, 0x02, 0x03, 0x04
  };
  hx_uint8 expected_output[] = {
    0x51,                               /* push rcx           */
    0x48, 0xb9, 0xff, 0xff, 0xff, 0xff, /* mov rcx, <rip>     */
                0xff, 0xff, 0xff, 0xff,
    0x8b, 0x81, 0x01, 0x00, 0x00, 0x00, /* mov eax, [rcx + 1] */
    0x59                                /* pop rcx            */
  };

  /*
   * Since our test fixture writes our output to a stack buffer, we mark our
   * input buffer as static so that it is part of the .data section and thus
   * more than 2GB from the stack. This means we can test the cases when the
   * offset to the RIP relative instruction can't simply be modified.
   */
  hx_assert (((input - expected_output) < HX_MININT32) ||
      ((input - expected_output) > HX_MAXINT32));

  *((hx_pointer *) (expected_output + 3)) = (hx_pointer) (input + 6);

  hoox_x86_writer_set_target_abi (&fixture->cw, HOOX_ABI_WINDOWS);
  SETUP_RELOCATOR_WITH (input);

  hoox_x86_relocator_read_one (&fixture->rl, NULL);
  hoox_x86_relocator_write_one (&fixture->rl);
  assert_output_equals (expected_output);
}

TESTCASE (rip_relative_push)
{
  static const hx_uint8 input[] = {
    0xff, 0x35,                         /* push [rip + imm32]   */
    0x01, 0x02, 0x03, 0x04
  };
  hx_uint8 expected_output[] = {
    0x50,                               /* push rax  */
    0x50,                               /* push rax  */

    0x48, 0xb8,                         /* mov rax, <rip> */
    0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00,

    0x48, 0x8b, 0x80,                   /* mov rax, [rax + <imm32>] */
    0x01, 0x02, 0x03, 0x04,

    0x48, 0x89, 0x44, 0x24, 0x08,       /* mov [rsp + 8], rax */
    0x58                                /* pop rax */
  };

  /*
   * Since our test fixture writes our output to a stack buffer, we mark our
   * input buffer as static so that it is part of the .data section and thus
   * more than 2GB from the stack. This means we can test the cases when the
   * offset to the RIP relative instruction can't simply be modified.
   */
  hx_assert (((input - expected_output) < HX_MININT32) ||
      ((input - expected_output) > HX_MAXINT32));

  *((hx_pointer *) (expected_output + 4)) = (hx_pointer) (input + 6);

  hoox_x86_writer_set_target_abi (&fixture->cw, HOOX_ABI_WINDOWS);
  SETUP_RELOCATOR_WITH (input);

  hoox_x86_relocator_read_one (&fixture->rl, NULL);
  hoox_x86_relocator_write_one (&fixture->rl);
  assert_output_equals (expected_output);
}

TESTCASE (rip_relative_push_red_zone)
{
  static const hx_uint8 input[] = {
    0xff, 0x35,                         /* push [rip + imm32]   */
    0x01, 0x02, 0x03, 0x04
  };
  hx_uint8 expected_output[] = {
    0x50,                               /* push rax  */
    0x48, 0x8d, 0xa4, 0x24,             /* lea rsp, [rsp - 128] */
          0x80, 0xff, 0xff, 0xff,
    0x50,                               /* push rax  */

    0x48, 0xb8,                         /* mov rax, <rip> */
    0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00,

    0x48, 0x8b, 0x80,                   /* mov rax, [rax + <imm32>] */
    0x01, 0x02, 0x03, 0x04,

    0x48, 0x89, 0x84, 0x24,             /* mov [rsp + 8 + 128], rax */
          0x88, 0x00, 0x00, 0x00,
    0x58,                               /* pop rax */
    0x48, 0x8d, 0xa4, 0x24,             /* lea rsp, [rsp + 128] */
          0x80, 0x00, 0x00, 0x00
  };

  /*
   * Since our test fixture writes our output to a stack buffer, we mark our
   * input buffer as static so that it is part of the .data section and thus
   * more than 2GB from the stack. This means we can test the cases when the
   * offset to the RIP relative instruction can't simply be modified.
   */
  hx_assert (((input - expected_output) < HX_MININT32) ||
      ((input - expected_output) > HX_MAXINT32));

  *((hx_pointer *) (expected_output + 12)) = (hx_pointer) (input + 6);

  hoox_x86_writer_set_target_abi (&fixture->cw, HOOX_ABI_UNIX);
  SETUP_RELOCATOR_WITH (input);

  hoox_x86_relocator_read_one (&fixture->rl, NULL);
  hoox_x86_relocator_write_one (&fixture->rl);
  assert_output_equals (expected_output);
}

TESTCASE (rip_relative_cmpxchg)
{
  static const hx_uint8 input[] = {
    0xf0, 0x48, 0x0f, 0xb1, 0x0d,       /* lock cmpxchg [rip + 1], rcx */
          0x01, 0x00, 0x00, 0x00
  };
  hx_uint8 expected_output[] = {
    0x52,                               /* push rdx           */
    0x48, 0xba, 0xff, 0xff, 0xff, 0xff, /* mov rdx, <rip>     */
                0xff, 0xff, 0xff, 0xff,
    0xf0, 0x48, 0x0f, 0xb1, 0x8a,       /* lock cmpxchg [rdx + 1], rcx */
                0x01, 0x00, 0x00, 0x00,
    0x5a                                /* pop rdx            */
  };

  /*
   * Since our test fixture writes our output to a stack buffer, we mark our
   * input buffer as static so that it is part of the .data section and thus
   * more than 2GB from the stack. This means we can test the cases when the
   * offset to the RIP relative instruction can't simply be modified.
   */
  hx_assert (((input - expected_output) < HX_MININT32) ||
      ((input - expected_output) > HX_MAXINT32));

  *((hx_pointer *) (expected_output + 3)) = (hx_pointer) (input + 9);

  hoox_x86_writer_set_target_abi (&fixture->cw, HOOX_ABI_WINDOWS);
  SETUP_RELOCATOR_WITH (input);

  hoox_x86_relocator_read_one (&fixture->rl, NULL);
  hoox_x86_relocator_write_one (&fixture->rl);
  assert_output_equals (expected_output);
}

TESTCASE (rip_relative_call)
{
  const hx_uint8 input_template[] = {
    0xff, 0x15,                   /* call [rip + 0x1234] */
          0x34, 0x12, 0x00, 0x00
  };
  static hx_uint8 input[sizeof (input_template) + 0x1234];
  hx_uint8 expected_output[] = {
    0x50,                         /* push rax */
    0x48, 0xb8,                   /* movabs rax, <return_address> */
          0x00, 0x00, 0x00, 0x00,
          0x00, 0x00, 0x00, 0x00,
    0x48, 0x87, 0x04, 0x24,       /* xchg qword [rsp], rax */

    0x50,                         /* push rax */
    0x48, 0xb8,                   /* movabs rax, <target_address> */
          0x00, 0x00, 0x00, 0x00,
          0x00, 0x00, 0x00, 0x00,
    0x48, 0x8b, 0x40, 0x00,       /* mov rax, qword [rax] */
    0x48, 0x87, 0x04, 0x24,       /* xchg qword [rsp], rax */
    0xc3,                         /* ret */
    /* return_address: */
  };

  /*
   * Our input buffer must be at least 0x1234 + 6 bytes long to avoid GCC
   * warning about static array bounds checking. Since our test fixture writes
   * our output to a stack buffer, we want to mark our input buffer as static so
   * that it is part of the image and thus more than 2GB from the stack. This
   * means we can test the cases when the offset to the RIP relative
   * instruction can't simply be modified. To avoid bloating the image though,
   * we instead copy in a template so that our input buffer can be uninitialized
   * and actually reside in the .bss section.
   */
  memcpy (input, input_template, sizeof (input_template));
  hx_assert (((input - expected_output) < HX_MININT32) ||
      ((input - expected_output) > HX_MAXINT32));

  *((hx_pointer *) (expected_output + 3)) =
      fixture->output + sizeof (expected_output);
  *((hx_pointer *) (expected_output + 18)) =
      (hx_pointer) (input + 6 + 0x1234);

  hoox_x86_writer_set_target_abi (&fixture->cw, HOOX_ABI_UNIX);
  SETUP_RELOCATOR_WITH (input);

  hx_assert_cmpuint (hoox_x86_relocator_read_one (&fixture->rl, NULL), ==, 6);
  hoox_x86_relocator_write_one (&fixture->rl);
  assert_output_equals (expected_output);
}

TESTCASE (rip_relative_adjust_offset)
{
  hx_uint8 input[] = {
    0x48, 0x8b, 0x05,             /* mov rax, qword ptr [rip + 0x140bc6a] */
          0x6a, 0xbc, 0x40, 0x01,
  };
  hx_uint8 expected_output[] = {
    0x48, 0x8b, 0x05,             /* mov rax, qword ptr [rip - 0x1d98dcc] */
           0x34, 0x72, 0x26, 0xfe,
  };

  SETUP_RELOCATOR_WITH (input);
  fixture->rl.input_pc = HX_UINT64_CONSTANT (0x10007043f);
  fixture->rl.output->pc = HX_UINT64_CONSTANT (0x103214e75);

  hoox_x86_relocator_read_one (&fixture->rl, NULL);
  hoox_x86_relocator_write_one (&fixture->rl);
  assert_output_equals (expected_output);
}

TESTCASE (eip_relative_adjust_offset)
{
  hx_uint8 input[] = {
    0x67, 0x8b, 0x05,             /* mov eax, dword ptr [eip + 0x12345678] */
          0x78, 0x56, 0x34, 0x12,
  };
  hx_uint8 expected_output[sizeof (input)];
  const HooxAddress input_pc = HX_UINT64_CONSTANT (0x12340000);
  const HooxAddress output_pc = HX_UINT64_CONSTANT (0x56780000);
  const hx_uint32 target = (hx_uint32) (input_pc + sizeof (input) +
      HX_UINT64_CONSTANT (0x12345678));
  const hx_int32 expected_disp = HX_INT32_TO_LE ((hx_int32)
      (target - (hx_uint32) (output_pc + sizeof (input))));

  memcpy (expected_output, input, sizeof (input));
  memcpy (expected_output + 3, &expected_disp, sizeof (expected_disp));

  SETUP_RELOCATOR_WITH (input);
  fixture->rl.input_pc = input_pc;
  fixture->cw.pc = output_pc;

  hx_assert_cmpuint (hoox_x86_relocator_read_one (&fixture->rl, NULL), ==,
      sizeof (input));
  hoox_x86_relocator_write_one (&fixture->rl);
  assert_output_equals (expected_output);
}

#endif
