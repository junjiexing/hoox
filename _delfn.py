"""Delete C function definitions (frida brace style) and their header
declarations. Frida style: return-type on its own line, `name (` at column 0,
`{` and matching `}` at column 0."""
import os, re, glob, sys, json

SRC_HEADERS = glob.glob("src/**/*.h", recursive=True)

def _find_def(lines, name):
    pat = re.compile(r'^' + re.escape(name) + r'\s*\(')
    for i, l in enumerate(lines):
        if pat.match(l):
            # signature line at column 0 → this is a definition (decls are
            # indented or on the return-type line in headers).
            return i
    return None

def delete_def(path, name):
    lines = open(path, encoding="utf-8").read().split("\n")
    sig = _find_def(lines, name)
    if sig is None:
        return False, "no def"
    # start: include the return-type line(s) immediately above (contiguous,
    # not blank / brace / preprocessor / comment-close), plus an attached
    # doc-comment block if directly above with no blank line.
    start = sig
    j = sig - 1
    while j >= 0:
        s = lines[j].strip()
        if s == "" or s[-1:] in "{};" or s[:1] == "#":
            break
        if s.startswith("*/") or s.startswith("/*") or s.startswith("*") \
           or s.startswith("//"):
            break
        start = j
        j -= 1
    # attached comment block ending right above `start`
    if start - 1 >= 0 and lines[start - 1].strip().endswith("*/"):
        k = start - 1
        while k >= 0 and not lines[k].strip().startswith("/*"):
            k -= 1
        if k >= 0:
            start = k
    # find opening brace line (== "{" at col 0) at/after sig
    ob = sig
    while ob < len(lines) and lines[ob] != "{":
        ob += 1
    if ob >= len(lines):
        return False, "no opening brace"
    # matching closing brace: first line == "}" at col 0 after ob
    cb = ob + 1
    while cb < len(lines) and lines[cb] != "}":
        cb += 1
    if cb >= len(lines):
        return False, "no closing brace"
    end = cb
    # swallow one trailing blank line
    if end + 1 < len(lines) and lines[end + 1].strip() == "":
        end += 1
    del lines[start:end + 1]
    open(path, "w", encoding="utf-8", newline="\n").write("\n".join(lines))
    return True, f"L{start+1}-{end+1}"

def delete_decl(name):
    """Remove the prototype for `name` from any src header."""
    hits = []
    declpat = re.compile(r'(?<![A-Za-z0-9_])' + re.escape(name) + r'\s*\(')
    for h in SRC_HEADERS:
        lines = open(h, encoding="utf-8").read().split("\n")
        out = []
        i = 0
        changed = False
        while i < len(lines):
            if declpat.search(lines[i]) and "{" not in lines[i]:
                # statement start: back up over return-type-only preceding line
                # (already appended). Find statement end (line ending ';').
                # Remove appended return-type line if it has no ';' and no name.
                # Determine start within `out`: if previous appended line looks
                # like a lone return-type/qualifier (no ';', not blank/#), drop it.
                while out and out[-1].strip() and out[-1].strip()[-1:] not in ";{}#" \
                      and not out[-1].strip().startswith("#") \
                      and name not in out[-1] and "(" not in out[-1]:
                    out.pop()
                # consume forward to ';'
                j = i
                while j < len(lines) and ";" not in lines[j]:
                    j += 1
                i = j + 1
                changed = True
                continue
            out.append(lines[i])
            i += 1
        if changed:
            open(h, "w", encoding="utf-8", newline="\n").write("\n".join(out))
            hits.append(h)
    return hits

if __name__ == "__main__":
    plan = json.load(open("_plan.json"))
    keys = sys.argv[1:] or ["dead_delete"]
    fails = []
    ndef = ndecl = 0
    for key in keys:
        for p, n in plan[key]:
            p = p.replace("\\", "/")
            ok, info = delete_def(p, n)
            if not ok:
                fails.append(f"{p}::{n}  DEF {info}")
            else:
                ndef += 1
            if delete_decl(n):
                ndecl += 1
    print(f"deleted defs={ndef} decls={ndecl}")
    if fails:
        print("FAILURES:")
        print("\n".join(fails))
        sys.exit(1)
