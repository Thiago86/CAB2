import sys, struct, json, collections
sys.path.insert(0, sys.argv[3])
import sh4
BASE=0x0C010000
def norm(v):
    return v&0x1FFFFFFF if isinstance(v,int) and (v&0x1F000000)==0x0C000000 and (v>>29)!=7 else v
data=open(sys.argv[1],'rb').read(); out=sys.argv[2]
END=BASE+len(data)
def inimg(a): return BASE<=a<END
def w16(a): return struct.unpack_from('<H',data,a-BASE)[0]
def w32(a): return struct.unpack_from('<I',data,a-BASE)[0]
code={}          # addr -> text
lit={}           # addr -> size of literal data referenced pc-relative
funcs=collections.OrderedDict()   # entry -> set(callees)
xref=collections.defaultdict(set) # literal value -> funcs that load it
indirect=collections.Counter()
def is_code_ptr(p):
    return inimg(p) and not p&1 and (p<0x0C098000 or 0x0C140000<=p<0x0C16E000) and p not in lit and sh4.decode(w16(p),p)[1]!='bad' and w16(p) not in (0,0xFFFF)
tables={}       # endereco da tabela -> (tipo, n entradas)
def table_ptrs(T):
    out=[]; zeros=0; n=0
    for i in range(256):
        a=T+4*i
        if not inimg(a+3) or a in code: break
        p=norm(w32(a))
        if p==0 and zeros<8: zeros+=1; n=i+1; continue      # entradas nulas (estado sem handler)
        if not is_code_ptr(p): break
        zeros=0; n=i+1; out.append(p)
    if out: tables[T]=('ptr',n,list(out)); lit.update({T+4*i:4 for i in range(n)})
    return out
def table_rel(T,size,origin):
    out=[]; lo=None
    for i in range(512):
        a=T+size*i
        if not inimg(a+size-1) or (lo is not None and a>=lo): break
        v=w16(a) if size==2 else w32(a)
        v=v-(1<<(8*size)) if v>>(8*size-1) else v
        t=origin+v
        if not inimg(t) or t&1 or abs(t-origin)>0x4000: break
        out.append(t)
        if t>T: lo=t if lo is None else min(lo,t)
    if out: tables[T]=('rel%d'%size,len(out),list(out)); lit.update({T+size*i:size for i in range(len(out))})
    return out
def explore(entry):
    """recursive descent inside one function; returns callees"""
    callees=set(); work=[(entry,{})]; seen=set(); walked=set()   # walked = enderecos percorridos como fluxo normal; seen tambem inclui slots de atraso
    while work:
        pc,regs=work.pop(); regs=dict(regs)
        while True:
            if pc in walked or not inimg(pc) or pc&1: break
            walked.add(pc); seen.add(pc)
            op=w16(pc); txt,kind,tgt,extra=sh4.decode(op,pc)
            if kind=='bad': code[pc]=txt; break
            code[pc]=txt
            delay=kind in('bra','bsr','bcc_s','rts','jmp','jsr','braf','bsrf','rte')
            def do_delay():
                if delay and inimg(pc+2):
                    t2,k2,g2,e2=sh4.decode(w16(pc+2),pc+2); code[pc+2]=t2; seen.add(pc+2)
                    track(pc+2,k2,g2,e2)
            def track(a,k,g,e):
                o=w16(a); n_=(o>>8)&15; m_=(o>>4)&15
                if (o&0xF00F) in (0x200B,0x2009,0x300C):          # or / and / add rm,rn com valores conhecidos
                    x=regs.get(m_); y=regs.get(n_)
                    if isinstance(x,int) and isinstance(y,int):
                        regs[n_]=((x|y) if o&15==0xB else (x&y) if o&15==9 else (x+y))&0xFFFFFFFF
                    else: regs.pop(n_,None)
                    return
                if (o&0xF000)==0x7000:                             # add #imm,rn
                    x=regs.get(n_)
                    if isinstance(x,int): regs[n_]=(x+(o&255)-(256 if o&0x80 else 0))&0xFFFFFFFF
                    else: regs.pop(n_,None)
                    return
                if (o&0xFF00) in (0xCB00,0xC900):                  # or/and #imm,r0
                    x=regs.get(0)
                    if isinstance(x,int): regs[0]=(x|(o&255)) if o&0xFF00==0xCB00 else (x&(o&255))
                    else: regs.pop(0,None)
                    return
                if o&0xF00F==0x6003:                      # mov rm,rn
                    if m_ in regs: regs[n_]=regs[m_]
                    else: regs.pop(n_,None)
                    return
                if o&0xF00F in (0x000E,0x000D):           # mov.l/mov.w @(r0,rm),rn : leitura de tabela
                    b=regs.get(m_); b0=regs.get(0)
                    base=b if isinstance(b,int) and inimg(b) else (b0 if isinstance(b0,int) and inimg(b0) else None)
                    if base is not None: regs[n_]=('tbl' if o&15==0xE else 'tblw',base)
                    else: regs.pop(n_,None)
                    return
                if k=='lit4' and inimg(g) and g+4<=END:
                    lit[g]=4; v=norm(w32(g)); regs[e[0]]=v; xref[v].add(entry)
                elif k=='lit2' and inimg(g): lit[g]=2; regs.pop(e[0],None)
                elif k=='imm': regs[e[0]]=e[1]&0xFFFFFFFF
                elif k=='mova' and inimg(g): regs[0]=g
                else:
                    # crude: any instruction writing rN as destination invalidates it
                    t=code[a]
                    for r in list(regs):
                        if t.endswith(',r%d'%r) or t.endswith(' r%d'%r): regs.pop(r,None)
            if kind=='jsr':
                v=norm(regs.get(extra[0])); do_delay()
                # delay slot may itself load the target register? rare; ignore
                if isinstance(v,tuple):
                    ps=table_ptrs(v[1]) if v[0]=='tbl' else []
                    if ps: callees.update(ps)
                    else: indirect[entry]+=1
                elif v is not None and inimg(v) and not v&1: callees.add(v)
                else: indirect[entry]+=1
                for r in range(8): regs.pop(r,None)
                pc+=4; continue
            if kind=='bsr':
                do_delay(); callees.add(tgt)
                for r in range(8): regs.pop(r,None)
                pc+=4; continue
            if kind=='bsrf':
                do_delay(); indirect[entry]+=1
                for r in range(8): regs.pop(r,None)
                pc+=4; continue
            if kind in('bcc','bcc_s'):
                do_delay(); work.append((tgt,regs)); pc+=4 if kind=='bcc_s' else 2; continue
            if kind=='bra':
                do_delay(); pc=tgt; continue
            if kind=='jmp':
                v=norm(regs.get(extra[0])); do_delay()
                if isinstance(v,tuple):
                    for t in (table_ptrs(v[1]) if v[0]=='tbl' else []): work.append((t,{}))
                elif v is not None and inimg(v) and not v&1: callees.add(v)   # tail call
                break
            if kind=='braf':
                v=regs.get(extra[0]); do_delay()
                if isinstance(v,tuple):
                    for t in table_rel(v[1],2 if v[0]=='tblw' else 4,pc+4): work.append((t,{}))
                else:
                    # tabela de trechos de codigo em linha: `and #N,r0 ; shll2/shll r0 ; braf r0` -> alvos = pc+4 + passo*k (k=0..N)
                    nmax=None; step=None
                    for back in range(2,14,2):
                        o2=w16(pc-back)
                        if (o2&0xFF00)==0xC900 and nmax is None: nmax=o2&0xFF
                        if o2==0x4008 and step is None: step=4
                        if o2==0x4000 and step is None: step=2
                    if nmax is not None and step and nmax<=31:
                        ts=[pc+4+step*k for k in range(nmax+1)]
                        tables[pc]=('code%d'%step,len(ts),ts)
                        for t in ts: work.append((t,{}))
                    elif nmax is None:
                        # sem `and` visivel: le os pares (bra,nop) em linha logo apos o braf; no maximo 8 entradas; descarta pares nop/nop no fim
                        ts=[]
                        for k in range(8):
                            a=pc+4+4*k
                            if not inimg(a+3): break
                            w1,w2=w16(a),w16(a+2)
                            if w2==0x0009 and ((w1&0xF000)==0xA000 or w1==0x0009): ts.append(a)
                            else: break
                        while ts and w16(ts[-1])==0x0009: ts.pop()
                        if len(ts)>=2:
                            ts=[pc+4]+[t for t in ts if t!=pc+4] if w16(pc+4)==0x0009 else ts
                            tables[pc]=('code4',len(ts),ts)
                            for t in ts: work.append((t,{}))
                break
            if kind in('rts','rte'): do_delay(); break
            track(pc,kind,tgt,extra); pc+=2
    return callees, seen
entry_pts=[BASE]
import os
_ep=os.path.join(os.path.dirname(os.path.abspath(out)),'extra_entries.txt')
if os.path.exists(_ep):
    for _l in open(_ep):
        _l=_l.split('#')[0].strip()
        if _l:
            _v=norm(int(_l,16))
            if inimg(_v) and not _v&1: entry_pts.append(_v)
queue=list(entry_pts); fseen={}
def run():
    while queue:
        f=queue.pop()
        if f in funcs: continue
        c,s=explore(f); funcs[f]=c; fseen[f]=s
        queue.extend(x for x in c if x not in funcs)
run(); n_direct=len(funcs)
# pass 2: function pointers stored in data/literals (callbacks, vtables, tasks)
def plausible(a):
    if not inimg(a) or a&1 or a in code or a in lit: return False
    ops=[w16(a+i*2) for i in range(6) if inimg(a+i*2+1)]
    if any(sh4.decode(o,a)[1]=='bad' for o in ops) or ops[0] in(0,0xFFFF): return False
    return any(o==0x4F22 or (o&0xFF0F)==0x2F06 for o in ops[:4])   # sts.l pr,@-r15 / mov.l rN,@-r15
changed=True; n_ptr=0
while changed:
    changed=False
    for off in range(0,len(data)-3,4):
        v=norm(struct.unpack_from('<I',data,off)[0])
        if v not in funcs and plausible(v) and (BASE+off) not in code:
            queue.append(v); run(); n_ptr+=1; changed=True
stats=dict(image_size=len(data),base=hex(BASE),functions=len(funcs),from_entry=n_direct,from_pointers=n_ptr,
           code_bytes=len(code)*2,literal_bytes=sum(lit.values()),indirect_call_sites=sum(indirect.values()))
caddrs=sorted(code); stats['code_min']=hex(caddrs[0]); stats['code_max']=hex(caddrs[-1])
callers=collections.defaultdict(set)
for f,cs in funcs.items():
    for c in cs: callers[c].add(f)
with open(out+'/functions.csv','w') as o:
    o.write('address,insn_count,callees,callers,indirect_calls\n')
    for f in sorted(funcs): o.write('0x%08x,%d,%d,%d,%d\n'%(f,len(fseen[f]),len(funcs[f]),len(callers[f]),indirect[f]))
json.dump({'%08x'%f:sorted('%08x'%c for c in cs) for f,cs in funcs.items()},open(out+'/callgraph.json','w'))
# string xrefs
import re
strs={BASE+m.start():m.group().decode('latin1') for m in re.finditer(rb'[\x20-\x7e]{5,}',data)}
with open(out+'/string_xrefs.txt','w') as o:
    for a in sorted(strs):
        if a in xref: o.write('0x%08x %-50r <- %s\n'%(a,strs[a][:60],' '.join('0x%08x'%f for f in sorted(xref[a])[:8])))
with open(out+'/1ST_READ.asm','w') as o:
    for a in caddrs:
        if a in funcs: o.write('\n; ---- sub_%08x  (callers: %d)\n'%(a,len(callers[a])))
        t=code[a]
        if 'mov.l @(0x' in t:
            g=int(t.split('(')[1].split(')')[0],16)
            if inimg(g) and g+4<=END:
                v=norm(w32(g)); t+='\t; =0x%08x'%v
                if v in strs: t+=' "%s"'%strs[v][:40]
                elif v in funcs: t+=' sub_%08x'%v
        o.write('%08x  %04x  %s\n'%(a,w16(a),t))
stats['tables']=collections.Counter(v[0] for v in tables.values()); json.dump({'%08x'%f:sorted(x for x in fseen[f]) for f in funcs},open(out+'/function_insns.json','w')); json.dump({'%08x'%k:v for k,v in sorted(tables.items())},open(out+'/tables.json','w'))
stats['string_xrefs']=sum(1 for a in strs if a in xref)
# hardware register literals
hw={'PVR(0xA05F8xxx)':0,'AICA(0xA07xxxxx)':0,'G1/GD-ROM(0xA05F7xxx)':0,'Maple(0xA05F6Cxx)':0,'TA/StoreQueue(0xE0..,0x10..)':0}
hwf=collections.defaultdict(set)
for v,fs in xref.items():
    k=None
    if 0xA05F8000<=v<0xA05FA000 or 0x005F8000<=v<0x005FA000: k='PVR(0xA05F8xxx)'
    elif 0xA0700000<=v<0xA0800000 or 0x00700000<=v<0x00800000: k='AICA(0xA07xxxxx)'
    elif 0xA05F7000<=v<0xA05F8000: k='G1/GD-ROM(0xA05F7xxx)'
    elif 0xA05F6C00<=v<0xA05F6D00: k='Maple(0xA05F6Cxx)'
    elif 0xE0000000<=v<0xE4000000 or 0x10000000<=v<0x14000000: k='TA/StoreQueue(0xE0..,0x10..)'
    if k: hwf[k]|=fs
stats['funcs_touching_hw']={k:len(v) for k,v in hwf.items()}
json.dump({k:sorted('%08x'%f for f in v) for k,v in hwf.items()},open(out+'/hw_functions.json','w'))
lf=collections.defaultdict(list)
for v,fs in xref.items():
    for f in fs: lf['%08x'%f].append(v)
json.dump(lf,open(out+'/literals_by_function.json','w'))
print(json.dumps(stats,indent=1))
