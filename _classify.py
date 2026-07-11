import os, re, json

COMPILED = set(map(os.path.normpath, [
    "src/compat/hxmem.c","src/compat/hxthread.c","src/compat/hxarray.c",
    "src/compat/hxhash.c","src/compat/hxlist.c","src/compat/hxstring.c",
    "src/compat/hxstrfuncs.c",
    "src/disasm/hx_disasm_x86.c",
    "src/arch/x86/hooxx86writer.c","src/arch/x86/hooxx86reader.c",
    "src/arch/x86/hooxx86relocator.c",
    "src/core/hooxspinlock.c","src/core/hooxmetalarray.c",
    "src/core/hooxmetalhash.c","src/core/hooxmemory.c","src/core/hooxcodesegment.c",
    "src/core/hooxcloak-stub.c","src/core/hooxcodeallocator.c",
    "src/core/hooxinvocationcontext.c","src/core/hooxinvocationlistener.c",
    "src/core/hooxinterceptor.c","src/core/hoox.c",
    "src/backend/windows/hooxmemory-windows.c",
    "src/backend/windows/hooxtls-windows.c",
    "src/backend/windows/hoox_process-windows.c",
    "src/backend/windows/hoox_msvc_intrinsics.c",
    "src/backend/x86/hooxcpucontext-x86.c","src/backend/x86/hooxcpu-x86.c",
    "src/backend/x86/hooxinterceptor-x86.c",
]))

def read(p):
    return open(p, encoding="utf-8", errors="ignore").read()

allc = []
for r,_d,fs in os.walk("src"):
    for f in fs:
        if f.endswith(".c"):
            allc.append(os.path.normpath(os.path.join(r,f)))

# text corpora
comp_c = "".join(read(p) for p in allc if p in COMPILED)
arm_c = "".join(read(p) for p in allc
                if p not in COMPILED and ("arm" in p or "arm64" in p))
harness = ""
tests_core = ""
for r,_d,fs in os.walk("tests"):
    for f in fs:
        if f.endswith((".c",".h")):
            t = read(os.path.join(r,f))
            if os.sep+"harness"+os.sep in os.path.join(r,f):
                harness += t
            else:
                tests_core += t

pub = set(re.findall(r'\b(hoox_[a-z0-9_]+)\s*\(', read("include/hoox.h")))

# Macro bodies in src headers: a function reached only through a #define
# (e.g. hx_slice_new0 -> hx_slice_alloc0) is USED, not dead. Collect the text
# of every #define (with backslash line-continuations) across src headers.
macro_text = ""
for r,_d,fs in os.walk("src"):
    for f in fs:
        if not f.endswith(".h"):
            continue
        lines = read(os.path.join(r,f)).split("\n")
        i = 0
        while i < len(lines):
            if re.match(r'\s*#\s*define\b', lines[i]):
                macro_text += lines[i] + "\n"
                while lines[i].rstrip().endswith("\\") and i+1 < len(lines):
                    i += 1
                    macro_text += lines[i] + "\n"
            i += 1

FN = re.compile(r'^([A-Za-z_][A-Za-z0-9_]*)\s*\(')
def funcs(p):
    out=[]; lines=read(p).split("\n")
    for i,l in enumerate(lines):
        m=FN.match(l)
        if not m: continue
        n=m.group(1)
        if n in ("if","for","while","switch","return","sizeof","else","do",
                 "HX_STMT_START","HX_DEFINE_BOXED_TYPE"): continue
        prev=lines[i-1].strip() if i>0 else ""
        if prev=="" or prev[-1:] in ";{}" or prev[:1] in "*/#": continue
        if not re.search(r'[A-Za-z_]', prev): continue
        out.append(n)
    return out

def cnt(name, text):
    return len(re.findall(r'(?<![A-Za-z0-9_])'+re.escape(name)+r'(?![A-Za-z0-9_])', text))

plan = {"dead_delete":[], "arm_kept":[], "testonly_harness_keep":[],
        "testonly_delete":[], "macro_kept":[], "public":[], "live":0}
for p in sorted(COMPILED):
    if not os.path.exists(p):  # e.g. hooxlibc.c already removed
        continue
    for n in funcs(p):
        rc = cnt(n, comp_c)          # includes its own definition (=1 baseline)
        if rc > 1:
            plan["live"] += 1; continue
        if n in pub:
            plan["public"].append((p,n)); continue
        if cnt(n, macro_text) > 0:
            plan["macro_kept"].append((p,n)); continue
        ra = cnt(n, arm_c)
        if ra > 0:
            plan["arm_kept"].append((p,n)); continue
        th = cnt(n, harness); tc = cnt(n, tests_core)
        if th > 0:
            plan["testonly_harness_keep"].append((p,n)); continue
        if tc > 0:
            plan["testonly_delete"].append((p,n)); continue
        plan["dead_delete"].append((p,n))

json.dump(plan, open("_plan.json","w"), indent=1)
def by_file(key):
    d={}
    for p,n in plan[key]: d.setdefault(p,[]).append(n)
    return d

print("LIVE:", plan["live"], " PUBLIC-uncalled:", len(plan["public"]))
for key in ["dead_delete","arm_kept","testonly_harness_keep","testonly_delete","macro_kept"]:
    d=by_file(key)
    total=sum(len(v) for v in d.values())
    print(f"\n=== {key}: {total} ===")
    for f in sorted(d):
        print(f"  {f} ({len(d[f])}): {', '.join(d[f])}")
