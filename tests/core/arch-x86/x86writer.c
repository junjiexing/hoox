/*
 * Copyright (C) 2009-2022 Ole André Vadla Ravnås <oleavr@nowsecure.com>
 *
 * Licence: wxWindows Library Licence, Version 3.1
 */

#include "x86writer-fixture.c"

TESTLIST_BEGIN (x86writer)
  TESTENTRY (jump_label)
  TESTENTRY (call_label)
  TESTENTRY (call_indirect)
  TESTENTRY (call_indirect_label)
  TESTENTRY (call_capi_eax_with_xdi_argument_for_ia32)
  TESTENTRY (call_capi_xbx_plus_i8_offset_ptr_with_xcx_argument_for_ia32)
  TESTENTRY (call_capi_xbx_plus_i8_offset_ptr_with_xcx_argument_for_amd64)
  TESTENTRY (call_sysapi_xbx_plus_i8_offset_ptr_with_xcx_argument_for_ia32)
  TESTENTRY (call_sysapi_xbx_plus_i8_offset_ptr_with_xcx_argument_for_amd64)
  TESTENTRY (call_sysapi_r12_plus_i32_offset_ptr_with_xcx_argument_for_amd64)
#ifdef HAVE_I386
  TESTENTRY (call_with_arguments_should_be_compatible_with_native_abi)
#endif
  TESTENTRY (flush_on_free)

  TESTENTRY (jmp_rcx)
  TESTENTRY (jmp_r8)
  TESTENTRY (jmp_rsp_ptr)
  TESTENTRY (jmp_r8_ptr)
  TESTENTRY (jmp_near_ptr_for_ia32)
  TESTENTRY (jmp_near_ptr_for_amd64)

  TESTENTRY (add_eax_ecx)
  TESTENTRY (add_rax_rcx)
  TESTENTRY (add_r8_rcx)
  TESTENTRY (inc_ecx)
  TESTENTRY (inc_rcx)
  TESTENTRY (dec_ecx)
  TESTENTRY (dec_rcx)

  TESTENTRY (lock_xadd_rcx_ptr_eax)
  TESTENTRY (lock_xadd_rcx_ptr_rax)
  TESTENTRY (lock_xadd_r15_ptr_eax)
  TESTENTRY (lock_inc_dec_imm32_ptr)

  TESTENTRY (and_ecx_edx)
  TESTENTRY (and_rdx_rsi)
  TESTENTRY (and_eax_u32)
  TESTENTRY (and_rax_u32)
  TESTENTRY (and_r13_u32)
  TESTENTRY (shl_eax_u8)
  TESTENTRY (shl_rax_u8)

  TESTENTRY (mov_ecx_rsi_offset_ptr)
  TESTENTRY (mov_rcx_rsi_offset_ptr)
  TESTENTRY (mov_r10d_rsi_offset_ptr)
  TESTENTRY (mov_r10_rsi_offset_ptr)
  TESTENTRY (mov_ecx_r11_offset_ptr)
  TESTENTRY (mov_r11_offset_ptr_ecx)
  TESTENTRY (mov_rcx_offset_ptr_r15)
  TESTENTRY (mov_reg_near_ptr_for_ia32)
  TESTENTRY (mov_reg_near_ptr_for_amd64)
  TESTENTRY (mov_near_ptr_reg_for_ia32)
  TESTENTRY (mov_near_ptr_reg_for_amd64)

  TESTENTRY (push_near_ptr_for_ia32)
  TESTENTRY (push_near_ptr_for_amd64)

  TESTENTRY (test_eax_ecx)
  TESTENTRY (test_rax_rcx)
  TESTENTRY (test_rax_r9)
  TESTENTRY (cmp_eax_i32)
  TESTENTRY (cmp_r9_i32)
  TESTENTRY (cmp_rax_i8_offset_ptr_rcx)
  TESTENTRY (cmp_r12_i8_offset_ptr_rcx)
  TESTENTRY (cmp_rsp_i8_offset_ptr_rcx)
  TESTENTRY (cmp_rsp_i32_offset_ptr_rcx)

  TESTENTRY (fxsave_xsp)
  TESTENTRY (fxsave_xcx)
  TESTENTRY (fxsave_r11)
  TESTENTRY (fxsave_r12)
  TESTENTRY (fxrstor_xsp)
  TESTENTRY (fxrstor_r11)
  TESTENTRY (fxrstor_r12)
TESTLIST_END ()

TESTCASE (jump_label)
{
  const hx_uint8 expected_code[] = {
  /* start: */
    0x81, 0xf9, 0x39, 0x05, 0x00, 0x00, /* cmp ecx, 1337              */
    0x2e, 0x74, 0x13,                   /* hnt je short handle_error  */
    0x2e, 0x7e, 0x10,                   /* hnt jle short handle_error */
    0x3e, 0x0f, 0x84,                   /* ht je near handle_error    */
          0x09, 0x00, 0x00, 0x00,
    0x2e, 0x0f, 0x8e,                   /* hnt jle near handle_error  */
          0x02, 0x00, 0x00, 0x00,
    0xeb, 0x01,                         /* jmp beach                  */
  /* handle_error: */
    0xcc,                               /* int 3                      */
  /* beach: */
    0x90,                               /* nop                        */
    0xeb, 0xe0                          /* jmp start                  */
  };
  const hx_char * start_lbl = "start";
  const hx_char * handle_error_lbl = "handle_error";
  const hx_char * beach_lbl = "beach";

  hoox_x86_writer_put_label (&fixture->cw, start_lbl);
  hoox_x86_writer_put_cmp_reg_i32 (&fixture->cw, HOOX_HX_ECX, 1337);
  hoox_x86_writer_put_jcc_short_label (&fixture->cw, HX_INS_JE,
      handle_error_lbl, HOOX_UNLIKELY);
  hoox_x86_writer_put_jcc_short_label (&fixture->cw, HX_INS_JLE,
      handle_error_lbl, HOOX_UNLIKELY);
  hoox_x86_writer_put_jcc_near_label (&fixture->cw, HX_INS_JE,
      handle_error_lbl, HOOX_LIKELY);
  hoox_x86_writer_put_jcc_near_label (&fixture->cw, HX_INS_JLE,
      handle_error_lbl, HOOX_UNLIKELY);
  hoox_x86_writer_put_jmp_short_label (&fixture->cw, beach_lbl);

  hoox_x86_writer_put_label (&fixture->cw, handle_error_lbl);
  hoox_x86_writer_put_breakpoint (&fixture->cw);

  hoox_x86_writer_put_label (&fixture->cw, beach_lbl);
  hoox_x86_writer_put_nop (&fixture->cw);
  hoox_x86_writer_put_jmp_short_label (&fixture->cw, start_lbl);

  assert_output_equals (expected_code);
}

TESTCASE (call_label)
{
  const hx_uint8 expected_code[] = {
    0xe8, 0x01, 0x00, 0x00, 0x00, /* call func */
    0xc3,                         /* retn      */
  /* func: */
    0xc3                          /* retn      */
  };
  const hx_char * func_lbl = "func";

  hoox_x86_writer_put_call_near_label (&fixture->cw, func_lbl);
  hoox_x86_writer_put_ret (&fixture->cw);

  hoox_x86_writer_put_label (&fixture->cw, func_lbl);
  hoox_x86_writer_put_ret (&fixture->cw);

  assert_output_equals (expected_code);
}

TESTCASE (call_indirect)
{
  const hx_uint8 expected_ia32_code[] = {
    0xff, 0x15, 0x78, 0x56, 0x34, 0x12 /* call [0x12345678] */
  };
  const hx_uint8 expected_amd64_code[] = {
    0xff, 0x15, 0x78, 0x56, 0x34, 0x12 /* call [rip + 6 + 0x12345678] */
  };

  hoox_x86_writer_set_target_cpu (&fixture->cw, HOOX_CPU_IA32);
  hoox_x86_writer_put_call_indirect (&fixture->cw, 0x12345678);
  assert_output_equals (expected_ia32_code);

  hoox_x86_writer_reset (&fixture->cw, fixture->output);

  hoox_x86_writer_set_target_cpu (&fixture->cw, HOOX_CPU_AMD64);
  hx_assert_false (hoox_x86_writer_put_call_indirect (&fixture->cw,
      HOOX_ADDRESS (fixture->output) + HX_UINT64_CONSTANT (0x7fffffff) + 6 + 1));

  hoox_x86_writer_reset (&fixture->cw, fixture->output);

  hoox_x86_writer_set_target_cpu (&fixture->cw, HOOX_CPU_AMD64);
  hoox_x86_writer_put_call_indirect (&fixture->cw,
      HOOX_ADDRESS (fixture->output) + 0x12345678 + 6);
  assert_output_equals (expected_amd64_code);
}

TESTCASE (call_indirect_label)
{
  const hx_char * addr_lbl = "label";
  const hx_uint8 expected_amd64_code[] = {
    0xff, 0x15, 0x01, 0x00, 0x00, 0x00, /* call [rip + label_delta] */
    0xc3,                               /* retn                     */
  /* label: */
  };
  hx_uint8 expected_ia32_code[] = {
    0xff, 0x15, 0x78, 0x56, 0x34, 0x12, /* call [label] */
    0xc3,                               /* retn         */
  /* label: */
  };

  *(hx_uint32 *) ((hx_pointer) (expected_ia32_code + 2)) =
      HX_UINT32_TO_LE ((hx_uint32) HOOX_ADDRESS (fixture->output) + 7);

  hoox_x86_writer_set_target_cpu (&fixture->cw, HOOX_CPU_AMD64);
  hoox_x86_writer_put_call_indirect_label (&fixture->cw, addr_lbl);
  hoox_x86_writer_put_ret (&fixture->cw);
  hoox_x86_writer_put_label (&fixture->cw, addr_lbl);
  assert_output_equals (expected_amd64_code);

  hoox_x86_writer_reset (&fixture->cw, fixture->output);

  hoox_x86_writer_set_target_cpu (&fixture->cw, HOOX_CPU_IA32);
  hoox_x86_writer_put_call_indirect_label (&fixture->cw, addr_lbl);
  hoox_x86_writer_put_ret (&fixture->cw);
  hoox_x86_writer_put_label (&fixture->cw, addr_lbl);
  assert_output_equals (expected_ia32_code);
}

TESTCASE (call_capi_eax_with_xdi_argument_for_ia32)
{
  const hx_uint8 expected_code[] = {
    0x57,                         /* push edi   */
    0xff, 0xd0,                   /* call eax   */
    0x83, 0xc4, 0x04              /* add esp, 4 */
  };

  hoox_x86_writer_set_target_cpu (&fixture->cw, HOOX_CPU_IA32);
  hoox_x86_writer_put_call_reg_with_arguments (&fixture->cw,
      HOOX_CALL_CAPI, HOOX_HX_XAX,
      1,
      HOOX_ARG_REGISTER, HOOX_HX_XDI);

  assert_output_equals (expected_code);
}

TESTCASE (call_capi_xbx_plus_i8_offset_ptr_with_xcx_argument_for_ia32)
{
  const hx_uint8 expected_code[] = {
    0x51,                         /* push ecx                   */
    0xff, 0x53, 0x15,             /* call dword near [ebx + 21] */
    0x83, 0xc4, 0x04              /* add esp, 4                 */
  };

  hoox_x86_writer_set_target_cpu (&fixture->cw, HOOX_CPU_IA32);
  hoox_x86_writer_put_call_reg_offset_ptr_with_arguments (&fixture->cw,
      HOOX_CALL_CAPI, HOOX_HX_XBX, 21,
      1,
      HOOX_ARG_REGISTER, HOOX_HX_XCX);

  assert_output_equals (expected_code);
}

TESTCASE (call_capi_xbx_plus_i8_offset_ptr_with_xcx_argument_for_amd64)
{
  const hx_uint8 expected_code[] = {
    0x48, 0x83, 0xec, 0x20,       /* sub rsp, 32                */
    0xff, 0x53, 0x15,             /* call dword near [rbx + 21] */
    0x48, 0x83, 0xc4, 0x20        /* add rsp, 32                */
  };

  hoox_x86_writer_set_target_cpu (&fixture->cw, HOOX_CPU_AMD64);
  hoox_x86_writer_put_call_reg_offset_ptr_with_arguments (&fixture->cw,
      HOOX_CALL_CAPI, HOOX_HX_XBX, 21,
      1,
      HOOX_ARG_REGISTER, HOOX_HX_XCX);

  assert_output_equals (expected_code);
}

TESTCASE (call_sysapi_xbx_plus_i8_offset_ptr_with_xcx_argument_for_ia32)
{
  const hx_uint8 expected_code[] = {
    0x51,                         /* push ecx                   */
    0xff, 0x53, 0x2a              /* call dword near [ebx + 42] */
  };

  hoox_x86_writer_set_target_cpu (&fixture->cw, HOOX_CPU_IA32);
  hoox_x86_writer_put_call_reg_offset_ptr_with_arguments (&fixture->cw,
      HOOX_CALL_SYSAPI, HOOX_HX_XBX, 42,
      1,
      HOOX_ARG_REGISTER, HOOX_HX_XCX);

  assert_output_equals (expected_code);
}

TESTCASE (call_sysapi_xbx_plus_i8_offset_ptr_with_xcx_argument_for_amd64)
{
  const hx_uint8 expected_code[] = {
    0x48, 0x83, 0xec, 0x20,       /* sub rsp, 32                */
    0xff, 0x53, 0x2a,             /* call dword near [rbx + 42] */
    0x48, 0x83, 0xc4, 0x20        /* add rsp, 32                */
  };

  hoox_x86_writer_set_target_cpu (&fixture->cw, HOOX_CPU_AMD64);
  hoox_x86_writer_put_call_reg_offset_ptr_with_arguments (&fixture->cw,
      HOOX_CALL_SYSAPI, HOOX_HX_XBX, 42,
      1,
      HOOX_ARG_REGISTER, HOOX_HX_XCX);

  assert_output_equals (expected_code);
}

TESTCASE (call_sysapi_r12_plus_i32_offset_ptr_with_xcx_argument_for_amd64)
{
  const hx_uint8 expected_code[] = {
    0x48, 0x83, 0xec, 0x20,       /* sub rsp, 32                */
    0x41, 0xff, 0x94, 0x24,       /* call [r12 - 0xf00d]        */
          0xf3, 0x0f, 0xff, 0xff,
    0x48, 0x83, 0xc4, 0x20        /* add rsp, 32                */
  };

  hoox_x86_writer_set_target_cpu (&fixture->cw, HOOX_CPU_AMD64);
  hoox_x86_writer_put_call_reg_offset_ptr_with_arguments (&fixture->cw,
      HOOX_CALL_SYSAPI, HOOX_HX_R12, -0xf00d,
      1,
      HOOX_ARG_REGISTER, HOOX_HX_XCX);

  assert_output_equals (expected_code);
}

#ifdef HAVE_I386

TESTCASE (call_with_arguments_should_be_compatible_with_native_abi)
{
  hx_pointer page;
  HooxX86Writer cw;
  HxCallback func;

  page = hoox_alloc_n_pages (1, HOOX_PAGE_RW);

  hoox_x86_writer_init (&cw, page);
  hoox_x86_writer_put_call_address_with_arguments (&cw, HOOX_CALL_CAPI,
      HOOX_ADDRESS (hoox_test_native_function), 4,
      HOOX_ARG_ADDRESS, HOOX_ADDRESS ("red"),
      HOOX_ARG_ADDRESS, HOOX_ADDRESS ("green"),
      HOOX_ARG_ADDRESS, HOOX_ADDRESS ("blue"),
      HOOX_ARG_ADDRESS, HOOX_ADDRESS ("you"));
  hoox_x86_writer_put_ret (&cw);
  hoox_x86_writer_clear (&cw);

  hoox_mprotect (page, hoox_query_page_size (), HOOX_PAGE_RX);

  func = HOOX_POINTER_TO_FUNCPTR (HxCallback, page);
  func ();

  hoox_free_pages (page);
}

#endif

TESTCASE (flush_on_free)
{
  const hx_uint8 expected_code[] = {
    0xe8, 0x00, 0x00, 0x00, 0x00, /* call func */
    0xc3                          /* retn      */
  };
  HooxX86Writer * cw = &fixture->cw;
  const hx_char * func_lbl = "func";

  hoox_x86_writer_put_call_near_label (cw, func_lbl);
  hoox_x86_writer_put_label (cw, func_lbl);
  hoox_x86_writer_put_ret (cw);

  assert_output_equals (expected_code);
}

TESTCASE (jmp_rcx)
{
  /* jmp rcx; ud2 */
  const hx_uint8 expected_code[] = { 0xff, 0xe1, 0x0f, 0x0b };
  hoox_x86_writer_put_jmp_reg (&fixture->cw, HOOX_HX_RCX);
  assert_output_equals (expected_code);
}

TESTCASE (jmp_r8)
{
  /* jmp r8; ud2 */
  const hx_uint8 expected_code[] = { 0x41, 0xff, 0xe0, 0x0f, 0x0b };
  hoox_x86_writer_put_jmp_reg (&fixture->cw, HOOX_HX_R8);
  assert_output_equals (expected_code);
}

TESTCASE (jmp_rsp_ptr)
{
  /* jmp qword ptr [rsp]; ud2 */
  const hx_uint8 expected_code[] = { 0xff, 0x24, 0x24, 0x0f, 0x0b };
  hoox_x86_writer_put_jmp_reg_ptr (&fixture->cw, HOOX_HX_RSP);
  assert_output_equals (expected_code);
}

TESTCASE (jmp_r8_ptr)
{
  /* jmp qword ptr [r8]; ud2 */
  const hx_uint8 expected_code[] = { 0x41, 0xff, 0x20, 0x0f, 0x0b };
  hoox_x86_writer_put_jmp_reg_ptr (&fixture->cw, HOOX_HX_R8);
  assert_output_equals (expected_code);
}

TESTCASE (jmp_near_ptr_for_ia32)
{
  /* jmp qword ptr [rip + 0x12345678]; ud2 */
  const hx_uint8 expected_code[] = { 0xff, 0x25, 0x78, 0x56, 0x34, 0x12, 0x0f,
      0x0b };
  hoox_x86_writer_set_target_cpu (&fixture->cw, HOOX_CPU_IA32);
  hoox_x86_writer_put_jmp_near_ptr (&fixture->cw, 0x12345678);
  assert_output_equals (expected_code);
}

TESTCASE (jmp_near_ptr_for_amd64)
{
  /* jmp qword ptr [rip + 0x16]; ud2 */
  const hx_uint8 expected_code[] = { 0xff, 0x25, 0x16, 0x00, 0x00, 0x00, 0x0f,
      0x0b };
  hoox_x86_writer_set_target_cpu (&fixture->cw, HOOX_CPU_AMD64);
  hoox_x86_writer_put_jmp_near_ptr (&fixture->cw,
      HOOX_ADDRESS (fixture->output + 28));
  assert_output_equals (expected_code);
}

TESTCASE (add_eax_ecx)
{
  const hx_uint8 expected_code[] = { 0x01, 0xc8 };
  hoox_x86_writer_put_add_reg_reg (&fixture->cw, HOOX_HX_EAX, HOOX_HX_ECX);
  assert_output_equals (expected_code);
}

TESTCASE (add_rax_rcx)
{
  const hx_uint8 expected_code[] = { 0x48, 0x01, 0xc8 };
  hoox_x86_writer_put_add_reg_reg (&fixture->cw, HOOX_HX_RAX, HOOX_HX_RCX);
  assert_output_equals (expected_code);
}

TESTCASE (add_r8_rcx)
{
  const hx_uint8 expected_code[] = { 0x49, 0x01, 0xc8 };
  hoox_x86_writer_put_add_reg_reg (&fixture->cw, HOOX_HX_R8, HOOX_HX_RCX);
  assert_output_equals (expected_code);
}

TESTCASE (inc_ecx)
{
  const hx_uint8 expected_code[] = { 0xff, 0xc1 };
  hoox_x86_writer_put_inc_reg (&fixture->cw, HOOX_HX_ECX);
  assert_output_equals (expected_code);
}

TESTCASE (inc_rcx)
{
  const hx_uint8 expected_code[] = { 0x48, 0xff, 0xc1 };
  hoox_x86_writer_put_inc_reg (&fixture->cw, HOOX_HX_RCX);
  assert_output_equals (expected_code);
}

TESTCASE (dec_ecx)
{
  const hx_uint8 expected_code[] = { 0xff, 0xc9 };
  hoox_x86_writer_put_dec_reg (&fixture->cw, HOOX_HX_ECX);
  assert_output_equals (expected_code);
}

TESTCASE (dec_rcx)
{
  const hx_uint8 expected_code[] = { 0x48, 0xff, 0xc9 };
  hoox_x86_writer_put_dec_reg (&fixture->cw, HOOX_HX_RCX);
  assert_output_equals (expected_code);
}

TESTCASE (lock_xadd_rcx_ptr_eax)
{
  const hx_uint8 expected_code[] = { 0xf0, 0x0f, 0xc1, 0x01 };
  hoox_x86_writer_put_lock_xadd_reg_ptr_reg (&fixture->cw, HOOX_HX_RCX,
      HOOX_HX_EAX);
  assert_output_equals (expected_code);
}

TESTCASE (lock_xadd_rcx_ptr_rax)
{
  const hx_uint8 expected_code[] = { 0xf0, 0x48, 0x0f, 0xc1, 0x01 };
  hoox_x86_writer_put_lock_xadd_reg_ptr_reg (&fixture->cw, HOOX_HX_RCX,
      HOOX_HX_RAX);
  assert_output_equals (expected_code);
}

TESTCASE (lock_xadd_r15_ptr_eax)
{
  const hx_uint8 expected_code[] = { 0xf0, 0x41, 0x0f, 0xc1, 0x07 };
  hoox_x86_writer_put_lock_xadd_reg_ptr_reg (&fixture->cw, HOOX_HX_R15,
      HOOX_HX_EAX);
  assert_output_equals (expected_code);
}

TESTCASE (lock_inc_dec_imm32_ptr)
{
  hx_pointer target;
  hx_uint8 expected_code[] = { 0xf0, 0xff, 0x05, 0x00, 0x00, 0x00, 0x00 };

  target = fixture->output + 32;

#if HX_SIZEOF_VOID_P == 4
  hoox_x86_writer_set_target_cpu (&fixture->cw, HOOX_CPU_IA32);
  *((hx_uint32 *) (expected_code + 3)) =
      HX_UINT32_TO_LE (HX_POINTER_TO_SIZE (target));
#else
  hoox_x86_writer_set_target_cpu (&fixture->cw, HOOX_CPU_AMD64);
  *((hx_int32 *) (expected_code + 3)) =
      HX_INT32_TO_LE (32 - sizeof (expected_code));
#endif

  hoox_x86_writer_put_lock_inc_imm32_ptr (&fixture->cw, target);
  assert_output_equals (expected_code);

  hoox_x86_writer_reset (&fixture->cw, fixture->output);

  expected_code[2] = 0x0d;
  hoox_x86_writer_put_lock_dec_imm32_ptr (&fixture->cw, target);
  assert_output_equals (expected_code);
}

TESTCASE (and_ecx_edx)
{
  const hx_uint8 expected_code[] = { 0x21, 0xd1 };
  hoox_x86_writer_put_and_reg_reg (&fixture->cw, HOOX_HX_ECX, HOOX_HX_EDX);
  assert_output_equals (expected_code);
}

TESTCASE (and_rdx_rsi)
{
  const hx_uint8 expected_code[] = { 0x48, 0x21, 0xf2 };
  hoox_x86_writer_put_and_reg_reg (&fixture->cw, HOOX_HX_RDX, HOOX_HX_RSI);
  assert_output_equals (expected_code);
}

TESTCASE (and_eax_u32)
{
  const hx_uint8 expected_code[] = { 0x25, 0xff, 0xff, 0xff, 0xff };
  hoox_x86_writer_put_and_reg_u32 (&fixture->cw, HOOX_HX_EAX, 0xffffffff);
  assert_output_equals (expected_code);
}

TESTCASE (and_rax_u32)
{
  const hx_uint8 expected_code[] = { 0x48, 0x25, 0xff, 0xff, 0xff, 0xff };
  hoox_x86_writer_put_and_reg_u32 (&fixture->cw, HOOX_HX_RAX, 0xffffffff);
  assert_output_equals (expected_code);
}

TESTCASE (and_r13_u32)
{
  const hx_uint8 expected_code[] = { 0x49, 0x81, 0xe5, 0xff, 0xff, 0xff, 0xff };
  hoox_x86_writer_put_and_reg_u32 (&fixture->cw, HOOX_HX_R13, 0xffffffff);
  assert_output_equals (expected_code);
}

TESTCASE (shl_eax_u8)
{
  const hx_uint8 expected_code[] = { 0xc1, 0xe0, 0x07 };
  hoox_x86_writer_put_shl_reg_u8 (&fixture->cw, HOOX_HX_EAX, 7);
  assert_output_equals (expected_code);
}

TESTCASE (shl_rax_u8)
{
  const hx_uint8 expected_code[] = { 0x48, 0xc1, 0xe0, 0x07 };
  hoox_x86_writer_put_shl_reg_u8 (&fixture->cw, HOOX_HX_RAX, 7);
  assert_output_equals (expected_code);
}

TESTCASE (mov_ecx_rsi_offset_ptr)
{
  const hx_uint8 expected_code[] = { 0x8b, 0x8e, 0x37, 0x13, 0x00, 0x00 };
  hoox_x86_writer_put_mov_reg_reg_offset_ptr (&fixture->cw, HOOX_HX_ECX,
      HOOX_HX_RSI, 0x1337);
  assert_output_equals (expected_code);
}

TESTCASE (mov_rcx_rsi_offset_ptr)
{
  const hx_uint8 expected_code[] = { 0x48, 0x8b, 0x8e, 0x37, 0x13, 0x00, 0x00 };
  hoox_x86_writer_put_mov_reg_reg_offset_ptr (&fixture->cw, HOOX_HX_RCX,
      HOOX_HX_RSI, 0x1337);
  assert_output_equals (expected_code);
}

TESTCASE (mov_r10d_rsi_offset_ptr)
{
  const hx_uint8 expected_code[] = { 0x44, 0x8b, 0x96, 0x37, 0x13, 0x00, 0x00 };
  hoox_x86_writer_put_mov_reg_reg_offset_ptr (&fixture->cw, HOOX_HX_R10D,
      HOOX_HX_RSI, 0x1337);
  assert_output_equals (expected_code);
}

TESTCASE (mov_r10_rsi_offset_ptr)
{
  const hx_uint8 expected_code[] = { 0x4c, 0x8b, 0x96, 0x37, 0x13, 0x00, 0x00 };
  hoox_x86_writer_put_mov_reg_reg_offset_ptr (&fixture->cw, HOOX_HX_R10,
      HOOX_HX_RSI, 0x1337);
  assert_output_equals (expected_code);
}

TESTCASE (mov_ecx_r11_offset_ptr)
{
  const hx_uint8 expected_code[] = { 0x41, 0x8b, 0x8b, 0x37, 0x13, 0x00, 0x00 };
  hoox_x86_writer_put_mov_reg_reg_offset_ptr (&fixture->cw, HOOX_HX_ECX,
      HOOX_HX_R11, 0x1337);
  assert_output_equals (expected_code);
}

TESTCASE (mov_r11_offset_ptr_ecx)
{
  const hx_uint8 expected_code[] = { 0x41, 0x89, 0x8b, 0x37, 0x13, 0x00, 0x00 };
  hoox_x86_writer_put_mov_reg_offset_ptr_reg (&fixture->cw, HOOX_HX_R11, 0x1337,
      HOOX_HX_ECX);
  assert_output_equals (expected_code);
}

TESTCASE (mov_rcx_offset_ptr_r15)
{
  const hx_uint8 expected_code[] = { 0x4c, 0x89, 0xb9, 0x37, 0x13, 0x00, 0x00 };
  hoox_x86_writer_put_mov_reg_offset_ptr_reg (&fixture->cw, HOOX_HX_RCX, 0x1337,
      HOOX_HX_R15);
  assert_output_equals (expected_code);
}

TESTCASE (mov_reg_near_ptr_for_ia32)
{
  hoox_x86_writer_set_target_cpu (&fixture->cw, HOOX_CPU_IA32);

  {
    const hx_uint8 expected_code[] = { 0x8b, 0x25, 0x78, 0x56, 0x34, 0x12 };
    hoox_x86_writer_put_mov_reg_near_ptr (&fixture->cw, HOOX_HX_ESP, 0x12345678);
    assert_output_equals (expected_code);
  }

  hoox_x86_writer_reset (&fixture->cw, fixture->output);
  hoox_x86_writer_set_target_cpu (&fixture->cw, HOOX_CPU_IA32);

  {
    const hx_uint8 expected_code[] = { 0xa1, 0x78, 0x56, 0x34, 0x12 };
    hoox_x86_writer_put_mov_reg_near_ptr (&fixture->cw, HOOX_HX_EAX, 0x12345678);
    assert_output_equals (expected_code);
  }

  hoox_x86_writer_reset (&fixture->cw, fixture->output);
  hoox_x86_writer_set_target_cpu (&fixture->cw, HOOX_CPU_IA32);

  {
    const hx_uint8 expected_code[] = { 0x8b, 0x0d, 0x78, 0x56, 0x34, 0x12 };
    hoox_x86_writer_put_mov_reg_near_ptr (&fixture->cw, HOOX_HX_ECX, 0x12345678);
    assert_output_equals (expected_code);
  }
}

TESTCASE (mov_reg_near_ptr_for_amd64)
{
  hoox_x86_writer_set_target_cpu (&fixture->cw, HOOX_CPU_AMD64);

  {
    const hx_uint8 expected_code[] = { 0x48, 0x8b, 0x25, 0x15, 0x00, 0x00, 0x00 };
    hoox_x86_writer_put_mov_reg_near_ptr (&fixture->cw, HOOX_HX_RSP,
        HOOX_ADDRESS (fixture->output + 28));
    assert_output_equals (expected_code);
  }

  hoox_x86_writer_reset (&fixture->cw, fixture->output);
  hoox_x86_writer_set_target_cpu (&fixture->cw, HOOX_CPU_AMD64);

  {
    const hx_uint8 expected_code[] = { 0x48, 0x8b, 0x05, 0x15, 0x00, 0x00, 0x00 };
    hoox_x86_writer_put_mov_reg_near_ptr (&fixture->cw, HOOX_HX_RAX,
        HOOX_ADDRESS (fixture->output + 28));
    assert_output_equals (expected_code);
  }

  hoox_x86_writer_reset (&fixture->cw, fixture->output);
  hoox_x86_writer_set_target_cpu (&fixture->cw, HOOX_CPU_AMD64);

  {
    const hx_uint8 expected_code[] = { 0x48, 0x8b, 0x0d, 0x15, 0x00, 0x00, 0x00 };
    hoox_x86_writer_put_mov_reg_near_ptr (&fixture->cw, HOOX_HX_RCX,
        HOOX_ADDRESS (fixture->output + 28));
    assert_output_equals (expected_code);
  }
}

TESTCASE (mov_near_ptr_reg_for_ia32)
{
  hoox_x86_writer_set_target_cpu (&fixture->cw, HOOX_CPU_IA32);

  {
    const hx_uint8 expected_code[] = { 0x89, 0x25, 0x78, 0x56, 0x34, 0x12 };
    hoox_x86_writer_put_mov_near_ptr_reg (&fixture->cw, 0x12345678, HOOX_HX_ESP);
    assert_output_equals (expected_code);
  }

  hoox_x86_writer_reset (&fixture->cw, fixture->output);
  hoox_x86_writer_set_target_cpu (&fixture->cw, HOOX_CPU_IA32);

  {
    const hx_uint8 expected_code[] = { 0xa3, 0x78, 0x56, 0x34, 0x12 };
    hoox_x86_writer_put_mov_near_ptr_reg (&fixture->cw, 0x12345678, HOOX_HX_EAX);
    assert_output_equals (expected_code);
  }

  hoox_x86_writer_reset (&fixture->cw, fixture->output);
  hoox_x86_writer_set_target_cpu (&fixture->cw, HOOX_CPU_IA32);

  {
    const hx_uint8 expected_code[] = { 0x89, 0x0d, 0x78, 0x56, 0x34, 0x12 };
    hoox_x86_writer_put_mov_near_ptr_reg (&fixture->cw, 0x12345678, HOOX_HX_ECX);
    assert_output_equals (expected_code);
  }
}

TESTCASE (mov_near_ptr_reg_for_amd64)
{
  hoox_x86_writer_set_target_cpu (&fixture->cw, HOOX_CPU_AMD64);

  {
    const hx_uint8 expected_code[] = { 0x48, 0x89, 0x25, 0x15, 0x00, 0x00, 0x00 };
    hoox_x86_writer_put_mov_near_ptr_reg (&fixture->cw,
        HOOX_ADDRESS (fixture->output + 28), HOOX_HX_RSP);
    assert_output_equals (expected_code);
  }

  hoox_x86_writer_reset (&fixture->cw, fixture->output);
  hoox_x86_writer_set_target_cpu (&fixture->cw, HOOX_CPU_AMD64);

  {
    const hx_uint8 expected_code[] = { 0x48, 0x89, 0x05, 0x15, 0x00, 0x00, 0x00 };
    hoox_x86_writer_put_mov_near_ptr_reg (&fixture->cw,
        HOOX_ADDRESS (fixture->output + 28), HOOX_HX_RAX);
    assert_output_equals (expected_code);
  }

  hoox_x86_writer_reset (&fixture->cw, fixture->output);
  hoox_x86_writer_set_target_cpu (&fixture->cw, HOOX_CPU_AMD64);

  {
    const hx_uint8 expected_code[] = { 0x48, 0x89, 0x0d, 0x15, 0x00, 0x00, 0x00 };
    hoox_x86_writer_put_mov_near_ptr_reg (&fixture->cw,
        HOOX_ADDRESS (fixture->output + 28), HOOX_HX_RCX);
    assert_output_equals (expected_code);
  }
}

TESTCASE (push_near_ptr_for_ia32)
{
  const hx_uint8 expected_code[] = { 0xff, 0x35, 0x78, 0x56, 0x34, 0x12 };
  hoox_x86_writer_set_target_cpu (&fixture->cw, HOOX_CPU_IA32);
  hoox_x86_writer_put_push_near_ptr (&fixture->cw, 0x12345678);
  assert_output_equals (expected_code);
}

TESTCASE (push_near_ptr_for_amd64)
{
  const hx_uint8 expected_code[] = { 0xff, 0x35, 0x16, 0x00, 0x00, 0x00 };
  hoox_x86_writer_set_target_cpu (&fixture->cw, HOOX_CPU_AMD64);
  hoox_x86_writer_put_push_near_ptr (&fixture->cw,
      HOOX_ADDRESS (fixture->output + 28));
  assert_output_equals (expected_code);
}

TESTCASE (test_eax_ecx)
{
  const hx_uint8 expected_code[] = { 0x85, 0xc8 };
  hoox_x86_writer_put_test_reg_reg (&fixture->cw, HOOX_HX_EAX, HOOX_HX_ECX);
  assert_output_equals (expected_code);
}

TESTCASE (test_rax_rcx)
{
  const hx_uint8 expected_code[] = { 0x48, 0x85, 0xc8 };
  hoox_x86_writer_put_test_reg_reg (&fixture->cw, HOOX_HX_RAX, HOOX_HX_RCX);
  assert_output_equals (expected_code);
}

TESTCASE (test_rax_r9)
{
  const hx_uint8 expected_code[] = { 0x4c, 0x85, 0xc8 };
  hoox_x86_writer_put_test_reg_reg (&fixture->cw, HOOX_HX_RAX, HOOX_HX_R9);
  assert_output_equals (expected_code);
}

TESTCASE (cmp_eax_i32)
{
  const hx_uint8 expected_code[] = { 0x3d, 0xff, 0xff, 0xff, 0xff };
  hoox_x86_writer_put_cmp_reg_i32 (&fixture->cw, HOOX_HX_EAX, -1);
  assert_output_equals (expected_code);
}

TESTCASE (cmp_r9_i32)
{
  const hx_uint8 expected_code[] = { 0x49, 0x81, 0xf9, 0x37, 0x13, 0x00, 0x00 };
  hoox_x86_writer_put_cmp_reg_i32 (&fixture->cw, HOOX_HX_R9, 0x1337);
  assert_output_equals (expected_code);
}

TESTCASE (cmp_rax_i8_offset_ptr_rcx)
{
  const hx_uint8 expected_code[] = { 0x48, 0x39, 0x48, 0x0a };
  hoox_x86_writer_put_cmp_reg_offset_ptr_reg (&fixture->cw, HOOX_HX_RAX, 0x0a,
      HOOX_HX_RCX);
  assert_output_equals (expected_code);
}

TESTCASE (cmp_r12_i8_offset_ptr_rcx)
{
  const hx_uint8 expected_code[] = { 0x49, 0x39, 0x4c, 0x24, 0x0a };
  hoox_x86_writer_put_cmp_reg_offset_ptr_reg (&fixture->cw, HOOX_HX_R12, 0x0a,
      HOOX_HX_RCX);
  assert_output_equals (expected_code);
}

TESTCASE (cmp_rsp_i8_offset_ptr_rcx)
{
  const hx_uint8 expected_code[] = { 0x48, 0x39, 0x4c, 0x24, 0x0a };
  hoox_x86_writer_put_cmp_reg_offset_ptr_reg (&fixture->cw, HOOX_HX_RSP, 0x0a,
      HOOX_HX_RCX);
  assert_output_equals (expected_code);
}

TESTCASE (cmp_rsp_i32_offset_ptr_rcx)
{
  const hx_uint8 expected_code[] = {
    0x48, 0x39, 0x8c, 0x24,
    0xaa, 0x00, 0x00, 0x00
  };
  hoox_x86_writer_put_cmp_reg_offset_ptr_reg (&fixture->cw, HOOX_HX_RSP, 0xaa,
      HOOX_HX_RCX);
  assert_output_equals (expected_code);
}

TESTCASE (fxsave_xsp)
{
  const hx_uint8 expected_code[] = { 0x0f, 0xae, 0x04, 0x24 };
  hoox_x86_writer_put_fxsave_reg_ptr (&fixture->cw, HOOX_HX_XSP);
  assert_output_equals (expected_code);
}

TESTCASE (fxsave_xcx)
{
  const hx_uint8 expected_code[] = { 0x0f, 0xae, 0x01 };
  hoox_x86_writer_put_fxsave_reg_ptr (&fixture->cw, HOOX_HX_XCX);
  assert_output_equals (expected_code);
}

TESTCASE (fxsave_r11)
{
  const hx_uint8 expected_code[] = { 0x41, 0x0f, 0xae, 0x03 };
  hoox_x86_writer_set_target_cpu (&fixture->cw, HOOX_CPU_AMD64);
  hoox_x86_writer_put_fxsave_reg_ptr (&fixture->cw, HOOX_HX_R11);
  assert_output_equals (expected_code);
}

TESTCASE (fxsave_r12)
{
  const hx_uint8 expected_code[] = { 0x41, 0x0f, 0xae, 0x04, 0x24 };
  hoox_x86_writer_set_target_cpu (&fixture->cw, HOOX_CPU_AMD64);
  hoox_x86_writer_put_fxsave_reg_ptr (&fixture->cw, HOOX_HX_R12);
  assert_output_equals (expected_code);
}

TESTCASE (fxrstor_xsp)
{
  const hx_uint8 expected_code[] = { 0x0f, 0xae, 0x0c, 0x24 };
  hoox_x86_writer_put_fxrstor_reg_ptr (&fixture->cw, HOOX_HX_XSP);
  assert_output_equals (expected_code);
}

TESTCASE (fxrstor_r11)
{
  const hx_uint8 expected_code[] = { 0x41, 0x0f, 0xae, 0x0b };
  hoox_x86_writer_set_target_cpu (&fixture->cw, HOOX_CPU_AMD64);
  hoox_x86_writer_put_fxrstor_reg_ptr (&fixture->cw, HOOX_HX_R11);
  assert_output_equals (expected_code);
}

TESTCASE (fxrstor_r12)
{
  const hx_uint8 expected_code[] = { 0x41, 0x0f, 0xae, 0x0c, 0x24 };
  hoox_x86_writer_set_target_cpu (&fixture->cw, HOOX_CPU_AMD64);
  hoox_x86_writer_put_fxrstor_reg_ptr (&fixture->cw, HOOX_HX_R12);
  assert_output_equals (expected_code);
}

#ifdef HAVE_I386

static void
hoox_test_native_function (const hx_char * arg1,
                          const hx_char * arg2,
                          const hx_char * arg3,
                          const hx_char * arg4)
{
  hx_assert_cmpstr (arg1, ==, "red");
  hx_assert_cmpstr (arg2, ==, "green");
  hx_assert_cmpstr (arg3, ==, "blue");
  hx_assert_cmpstr (arg4, ==, "you");
}

#endif
