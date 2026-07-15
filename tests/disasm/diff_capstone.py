#!/usr/bin/env python3
"""
Differential test for hoox's x86/x64 decoder against capstone (the pip
`capstone` module, v5 — the same major version as frida's fork).

For a curated set of important encodings plus a large random corpus, compare
hoox's decode (via the hx_disasm_dump helper) against capstone on:
  - instruction length (the primary invariant the relocator relies on)
  - RIP-relative detection
  - branch/call target

Curated cases must match exactly. The random corpus must match within a small
tolerance (exotic FPU/3DNow/AVX edge cases the compact decoder approximates are
not exercised by real function prologues).
"""

import random
import subprocess
import sys

try:
    import capstone
except ImportError:
    print("SKIP: capstone module not available")
    sys.exit(0)

from capstone import x86 as cx

DUMP = sys.argv[1] if len(sys.argv) > 1 else "hx_disasm_dump"
ADDR = 0x100000


def hoox_decode_batch(mode, datas):
    """Decode a list of byte-strings in one subprocess. Returns a list of
    dicts (or None per entry that hoox could not decode)."""
    stdin = "".join(d.hex() + "\n" for d in datas)
    out = subprocess.run([DUMP, str(mode)], input=stdin, capture_output=True,
                         text=True)
    results = []
    for line in out.stdout.splitlines():
        parts = line.split()
        if not parts or parts[0] == "-":
            results.append(None)
        else:
            results.append({"len": int(parts[0]), "id": int(parts[1]),
                            "rip": int(parts[2]), "target": int(parts[3])})
    return results


def cs_decode(md, data):
    md.detail = True
    for insn in md.disasm(data, ADDR):
        rip = 0
        target = -1
        for op in insn.operands:
            if op.type == cx.X86_OP_MEM and op.mem.base == cx.X86_REG_RIP:
                rip = 1
            if op.type == cx.X86_OP_IMM:
                if insn.group(capstone.CS_GRP_JUMP) or \
                   insn.group(capstone.CS_GRP_CALL):
                    target = op.imm & 0xffffffffffffffff
        is_branch = bool(insn.group(capstone.CS_GRP_JUMP) or
                         insn.group(capstone.CS_GRP_CALL) or
                         insn.group(capstone.CS_GRP_RET))
        return {"len": insn.size, "rip": rip, "target": target,
                "is_branch": is_branch, "mnem": insn.mnemonic}
    return None


CURATED_64 = [
    "55",              # push rbp
    "4889e5",          # mov rbp, rsp
    "4883ec20",        # sub rsp, 0x20
    "488d0500000000",  # lea rax, [rip+0]
    "e900000000",      # jmp rel32
    "e800000000",      # call rel32
    "eb10",            # jmp rel8
    "7405",            # je rel8
    "0f8400000000",    # je rel32
    "c3",              # ret
    "c20800",          # ret 8
    "ff2500000000",    # jmp [rip+0]
    "488b0500000000",  # mov rax, [rip+0]
    "0f1f440000",      # nop dword [rax+rax]
    "4c8d1500000000",  # lea r10, [rip+0]
    "f3c3",            # rep ret
    "660f1f440000",    # nop word
    "48ff2500000000",  # rex.w jmp [rip]
    "90",              # nop
    "cc",              # int3
    "0f05",            # syscall
    "488905" + "00000000",  # mov [rip+0], rax
    "678b0578563412",  # mov eax, [eip+0x12345678] (not RIP-relative)
]

CURATED_32 = [
    "55", "89e5", "83ec10", "e900000000", "e800000000", "eb10", "7405",
    "0f8400000000", "c3", "c20800", "6a00", "68000000", "90", "cc",
    "6701b1d2c32275",  # addr16 ModRM + disp16; trailing bytes are next insn
]


def check_curated(md, mode, cases):
    fails = 0
    datas = [bytes.fromhex(h) for h in cases]
    ours_all = hoox_decode_batch(mode, datas)
    for idx, hexs in enumerate(cases):
        data = datas[idx]
        ours = ours_all[idx] if idx < len(ours_all) else None
        theirs = cs_decode(md, data)
        if theirs is None:
            continue
        if ours is None:
            print("CURATED FAIL %s mode%d: hoox could not decode (cs len=%d %s)"
                  % (hexs, mode, theirs["len"], theirs["mnem"]))
            fails += 1
            continue
        if ours["len"] != theirs["len"]:
            print("CURATED FAIL %s mode%d: len hoox=%d cs=%d (%s)"
                  % (hexs, mode, ours["len"], theirs["len"], theirs["mnem"]))
            fails += 1
        if ours["rip"] != theirs["rip"]:
            print("CURATED FAIL %s mode%d: rip hoox=%d cs=%d (%s)"
                  % (hexs, mode, ours["rip"], theirs["rip"], theirs["mnem"]))
            fails += 1
        if theirs["target"] >= 0 and ours["target"] >= 0 and \
                ours["target"] != theirs["target"]:
            print("CURATED FAIL %s mode%d: target hoox=%x cs=%x (%s)"
                  % (hexs, mode, ours["target"], theirs["target"],
                     theirs["mnem"]))
            fails += 1
    return fails


def check_random(md, mode, n, rng):
    total = 0
    len_mismatch = 0
    rip_mismatch = 0
    target_mismatch = 0
    examples = []

    samples = [bytes(rng.randrange(256) for _ in range(15)) for _ in range(n)]
    ours_all = hoox_decode_batch(mode, samples)

    for idx in range(n):
        data = samples[idx]
        theirs = cs_decode(md, data)
        if theirs is None:
            continue
        ours = ours_all[idx] if idx < len(ours_all) else None
        if ours is None:
            continue
        total += 1
        if ours["len"] != theirs["len"]:
            len_mismatch += 1
            if len(examples) < 20:
                examples.append("len %s hoox=%d cs=%d %s" %
                                (data.hex(), ours["len"], theirs["len"],
                                 theirs["mnem"]))
        else:
            if ours["rip"] != theirs["rip"]:
                rip_mismatch += 1
                if len(examples) < 20:
                    examples.append("rip %s hoox=%d cs=%d %s" %
                                    (data.hex(), ours["rip"], theirs["rip"],
                                     theirs["mnem"]))
            if theirs["target"] >= 0 and ours["target"] >= 0 and \
                    ours["target"] != theirs["target"]:
                target_mismatch += 1
                if len(examples) < 20:
                    examples.append("tgt %s hoox=%x cs=%x %s" %
                                    (data.hex(), ours["target"],
                                     theirs["target"], theirs["mnem"]))
    return total, len_mismatch, rip_mismatch, target_mismatch, examples


def main():
    rng = random.Random(0x1337)
    fails = 0

    md64 = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64)
    md32 = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)

    print("== curated 64-bit ==")
    fails += check_curated(md64, 64, CURATED_64)
    print("== curated 32-bit ==")
    fails += check_curated(md32, 32, CURATED_32)

    if fails:
        print("CURATED: %d failure(s)" % fails)
        return 1
    print("curated: all passed")

    for mode, md in ((64, md64), (32, md32)):
        total, lm, rm, tm, ex = check_random(md, mode, 20000, rng)
        rate = (100.0 * lm / total) if total else 0.0
        print("== random mode%d: %d decoded, len_mismatch=%d (%.3f%%), "
              "rip_mismatch=%d, target_mismatch=%d ==" %
              (mode, total, lm, rate, rm, tm))
        for e in ex:
            print("   ", e)
        # Length is the critical invariant; allow a tiny tolerance for exotic
        # encodings not seen in real prologues.
        if rate > 0.5:
            fails += 1
        if tm > 0:
            fails += 1

    if fails:
        print("DIFF: FAILED")
        return 1
    print("DIFF: OK")
    return 0


if __name__ == "__main__":
    sys.exit(main())
