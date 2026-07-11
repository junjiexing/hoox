# hoox porting status & guide

## Status

| Target | Decoder | writer/reloc/reader | interceptor | tests | state |
|---|---|---|---|---|---|
| **Windows x64** | ✅ hx_disasm_x86 | ✅ | ✅ | ✅ 8 suites | **complete + tested** |
| **Windows x86** | ✅ hx_disasm_x86 | ✅ | ✅ | ✅ 8 suites | **complete + tested** |
| Linux/macOS x86_64 | ✅ (arch-shared) | ✅ | — backend | — | backend pending (M8) |
| **arm64** | ⏳ needed | ✅ extracted+adapted | ✅ extracted+adapted | — | **sources in-tree; decoder+shim pending** |
| **arm/thumb** | ⏳ needed | ✅ extracted+adapted | ✅ extracted+adapted | — | **sources in-tree; decoder+shim pending** |

The x86/x64 path is fully working and tested (inline hooking: attach/detach/
replace/revert; writer/relocator vs frida's 91 tests; decoder vs capstone;
single-file amalgamation). arm/arm64 sources are **extracted from frida-gum
and adapted** (glib→nano-glib, `<gum/…>`→local includes) under `src/arch/arm64`,
`src/arch/arm`, `src/backend/arm64`, `src/backend/arm`, but are **not yet wired
into the build** because they need their own instruction decoder + capstone-shim
extension (the arm/arm64 relocators consume `cs_arm64`/`cs_arm` detail exactly
as the x86 ones consume `cs_x86`).

## Remaining work to bring up arm64 (then arm)

Mirrors how x86 was done (M2/M4/M5):

1. **Extend the capstone shim (`src/disasm/capstone.h`)** with the arm64 surface
   the extracted code uses (already enumerated):
   - `arm64_reg`: `X0..X30` (contiguous), `W0..W30`, `S0..S31`, `D0..D31`,
     `Q0..Q31`, plus `SP FP LR XZR WZR INVALID` — keep each class contiguous so
     the writer's `BASE + n` arithmetic works.
   - `arm64_insn` (44 ids): `ADR ADRP B BL BLR BLRAA/AAZ/AB/ABZ BR BRAA/AAZ/AB/ABZ
     CBNZ CBZ HVC LDAXP LDAXR/B/H LDR LDRSW LDXP LDXR/B/H MOV RET RETAA RETAB SMC
     STLXP STLXR/B/H STP STXP STXR/B/H SVC TBNZ TBZ`.
   - `arm64_cc` (`INVALID AL NV` + the b.cond set), `ARM64_OP_REG/IMM`,
     `cs_arm64_op { type; reg; imm; }`, `cs_arm64 { op_count; operands[]; cc; }`,
     add `cs_arm64` to the `cs_detail` union, and `CS_ARCH_ARM64`.
2. **Write `src/disasm/hx_disasm_arm64.c`** — a fixed-width (4-byte) decoder.
   Unlike x86 it needs no length table; classify the ~25 relocatable ids by
   bitfield masks and fill `operands[0..2]` (imm target / dst reg / literal base)
   + `cc` + `op_count` + `regs_read/write` (2 spots). No encoding offsets needed
   (arm64 relocation rewrites whole instructions, not byte-splices). Reference
   Detours' ARM64 relocation for the branch/adr/ldr-literal fix-ups.
3. **Wire `src/arch/arm64` + `src/backend/arm64` into CMake** under
   `HOOX_ARCH_FAMILY STREQUAL "arm64"`, plus the interceptor `guminterceptor-arm64-glue.S`
   / masm and `memcpy-advsimd.S` (MSVC uses the `.masm` variant).
4. **Validate**: differential-test the decoder against capstone (aarch64) and
   port frida's `arm64writer`/`arm64relocator`/`interceptor-arm64` suites. On a
   non-arm64 host, object-compile only.

**arm/thumb** follows the same steps (`hx_disasm_arm.c` covering ARM + Thumb-2;
the relocators also read `shift`/`subtracted`/`writeback` and re-parse raw IT
bytes — see PLAN §2.3).

## Notes

- The arm64 interceptor uses `cs_disasm_iter` directly in `detect_hook_size`
  (like x86) — that call migrates to `hx_disasm` too.
- `gumcpucontext-arm64.c` / `gumcpucontext-arm.c` are extracted and only depend
  on `GumCpuContext` (in `gumdefs.h`); they compile as-is once wired.
