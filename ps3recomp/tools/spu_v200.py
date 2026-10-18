#!/usr/bin/env python3
"""v200 (FPS, SPU job code): two mechanical passes over a lifted spu_recomp_rc.c (run AFTER spu_ibfast.py).
1. Loop-back without write-back: when a function loops to its own entry (v185 SPU_IBF_RETSELF / v177 SPU_IBF_SITE
   self case), test the condition on the LOCAL register first and jump to spu_ibf_top without storing the ~30 cached
   registers to ctx (locals are current; nothing between the top and the branch reads ctx except LS accesses, checked
   per function). Exact: same condition as the macro (SPU_IBF_COND) + the spu_tlc fast path (no slow hook / stack-reset
   entries -> only ctx->steps++, pc != 0). Otherwise the old write-back + macro run unchanged.
2. SPU_XFK: float ops (fa/fs/fm/fma/fms/fnms) skip the xfloat clamp for operands produced by another float op in the
   same basic block (spu_xfk.h). Differs only if such a result overflowed to Inf.
usage: spu_v200.py <file> [--check]   (writes <file>.pre_v200 backup once; idempotent: skips files already done)"""
import re, sys, os
FOPS = {'spu_fa', 'spu_fs', 'spu_fm', 'spu_fma', 'spu_fms', 'spu_fnms'}
asg = re.compile(r'^(\s*)(r\d+)\s*=\s*(spu_\w+)\((.*)\);\s*$')
lab = re.compile(r'^\s*\w+:\s*;?\s*$')
SAFE_CTX = re.compile(r'spu_ls_(read|write)128\(ctx,')
st = dict(retself=0, site=0, fops=0, skipped=0)
def loopback(fn):
    txt = '\n'.join(fn)
    if 'spu_ibf_top:' not in txt: return fn
    name = re.match(r'void (\w+)\(spu_context\* ctx\) \{', fn[0])
    if not name: return fn
    name = name.group(1)
    decl = '\n'.join(fn[:4])
    top = [i for i, l in enumerate(fn) if l.startswith('spu_ibf_top:')][0]
    out = list(fn); ins_at = []
    for i, l in enumerate(fn):
        m = re.search(r'SPU_IBF_RETSELF\(ctx, (0x[0-9A-F]+u), (\w+)\) SPU_RET\(ctx\);', l)
        reg, pc, kind = None, None, None
        if m and m.group(2) == name: reg, pc, kind = 'r0', m.group(1), 'retself'
        m2 = re.search(r'SPU_IBF_SITE\(ctx, (\d+), (0x[0-9A-F]+u), (\w+),', l)
        if m2 and m2.group(3) == name: reg, pc, kind = 'r' + m2.group(1), m2.group(2), 'site'
        if not kind: continue
        wb = i - 1
        if not fn[wb].lstrip().startswith('ctx->gpr['): continue
        if not re.search(r'\b%s = ctx->gpr\[' % reg, decl): continue
        ok = True
        for b in fn[top + 1:wb]:
            s = SAFE_CTX.sub('', b)
            if 'ctx' in s or 'goto' in s or 'return' in s or lab.match(b): ok = False; break
        if not ok: continue
        ins_at.append((wb, '        if (((%s._u32[0] & SPU_LS_MASK) == %s) && SPU_IBF_COND(ctx) && !(g_spu_tlc_slow | (int)g_spu_stack_reset_n)) '
                           '{ ctx->pc = %s; ctx->steps++; goto spu_ibf_top; }   /* v200 loop-back without write-back */' % (reg, pc, pc)))
        st[kind] += 1
    for wb, s in sorted(ins_at, reverse=True): out.insert(wb, s)
    return out
def xfk(lines):
    out = []; clean = set()
    for ln in lines:
        if lab.match(ln) or ln.startswith('void ') or ln.startswith('}') or 'goto' in ln or 'SPU_' in ln or 'spu_drain' in ln:
            clean = set(); out.append(ln); continue
        m = asg.match(ln)
        if m:
            ind, d, fn, args = m.groups(); a = [x.strip() for x in args.split(',')]
            if fn in FOPS and all(re.fullmatch(r'r\d+', x) for x in a):
                mask = 0
                for i, x in enumerate(a):
                    if x not in clean: mask |= 1 << i
                    else: st['skipped'] += 1
                st['fops'] += 1
                out.append('%s%s = %s_k(%s, %d);' % (ind, d, fn, ', '.join(a), mask)); clean.add(d); continue
            clean.discard(d); out.append(ln); continue
        for d in re.findall(r'\b(r\d+)\b[^=;]*=[^=]', ln): clean.discard(d)
        out.append(ln)
    return out
def main(path):
    src = open(path).read()
    if '/* v200 */' in src: print(path, 'already done'); return
    if not os.path.exists(path + '.pre_v200'): open(path + '.pre_v200', 'w').write(src)
    L = src.split('\n'); res = []; fn = []
    for l in L:
        if fn or l.startswith('void ') and l.endswith('{'):
            fn.append(l)
            if l == '}': res.extend(loopback(fn)); fn = []
        else: res.append(l)
    res.extend(fn)
    res = xfk(res)
    for i, l in enumerate(res):
        if l.startswith('#include "spu_helpers_vec.h"'): res.insert(i + 1, '#include "spu_xfk.h"   /* v200 */'); break
    else: raise SystemExit(path + ': no spu_helpers_vec.h include')
    open(path, 'w').write('\n'.join(res))
    print(path, st)
for p in sys.argv[1:]:
    for k in st: st[k] = 0
    main(p)
