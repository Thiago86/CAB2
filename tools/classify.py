"""Classifica as funcoes de out/functions.csv: regiao, acesso a hardware, syscalls da BIOS, strings e arquivos.
uso: classify.py 1ST_READ.BIN out"""
import sys,csv,json,re,collections,struct
BASE=0x0C010000
data=open(sys.argv[1],'rb').read(); out=sys.argv[2]
rows=list(csv.DictReader(open(out+'/functions.csv')))
lit=json.load(open(out+'/literals_by_function.json'))
cg=json.load(open(out+'/callgraph.json'))
strs={BASE+m.start():m.group().decode('latin1') for m in re.finditer(rb'[\x20-\x7e]{4,}',data)}
A=sorted(int(r['address'],16) for r in rows)
# --- blocos de funcoes alinhadas em 16 bytes (assembly/biblioteca): janelas deslizantes de 12 funcoes
runs=[];cur=None
for i in range(len(A)):
    win=A[i:i+12]
    dense=len(win)==12 and sum((a&15)==0 for a in win)>=10
    if dense and cur is None: cur=A[i]
    if not dense and cur is not None:
        runs.append((cur,A[i+9] if i+9<len(A) else A[-1])); cur=None
if cur is not None: runs.append((cur,A[-1]))
def region(a):
    if a>=0x0C140000: return 'sdk'
    for s,e in runs:
        if s<=a<=e: return 'lib_asm'
    return 'game' if a<(runs[0][0] if runs else 0x0C098000) else 'lib_c'
BIOS={0x0C0000B0:'bios_sysinfo',0x0C0000B4:'bios_romfont',0x0C0000B8:'bios_flashrom',0x0C0000BC:'bios_gdrom_misc',0x0C0000E0:'bios_exit'}
def hw(v):
    if 0xA05F8000<=v<0xA05FA000 or 0x005F8000<=v<0x005FA000: return 'pvr'
    if 0xA0700000<=v<0xA0800000 or 0x00700000<=v<0x00800000: return 'aica'
    if 0xA05F7000<=v<0xA05F8000: return 'gdrom_reg'
    if 0xA05F6C00<=v<0xA05F6D00: return 'maple'
    if 0xA05F6800<=v<0xA05F6C00: return 'holly_sys'
    if 0xE0000000<=v<0xE4000000 or 0x10000000<=v<0x14000000: return 'ta_or_sq_const'
    if any(b<v<b+0x50 or (v==b and b!=0xFF000000) for b in (0xFF000000,0xFF200000,0xFF800000,0xFFA00000,0xFFC00000,0xFFC80000,0xFFD00000,0xFFD80000,0xFFE00000,0xFFE80000)): return 'sh4_reg'
    if 0xA4000000<=v<0xA6000000 or 0x04000000<=v<0x06000000: return 'vram'
    return None
FILE=re.compile(r'^[\w]+\.(BIN|AFS|ADX|PVR|MLT|DRV|INI)$',re.I)
# caracteristicas de FPU por funcao, lidas do desmontado
feat=collections.defaultdict(set); curf=None
for l in open(out+'/1ST_READ.asm'):
    if l.startswith('; ---- sub_'): curf=int(l[11:19],16); continue
    if curf is None or len(l)<17: continue
    t=l[16:]
    for k,v in (('ftrv','fpu_ftrv'),('fipr','fpu_fipr'),('fsrra','fpu_fsrra'),('fsca','fpu_fsca'),('fsqrt','fpu_fsqrt'),('fdiv','fpu_fdiv'),('fmac','fpu_fmac'),('pref','sq_pref'),('fschg','fpu_fschg'),('trapa','trapa')):
        if t.startswith(k): feat[curf].add(v)
callers=collections.Counter()
for f,cs in cg.items():
    for c in cs: callers[c]+=1
sym=[];stat=collections.Counter();tagstat=collections.Counter()
for r in rows:
    a=int(r['address'],16); key='%08x'%a; tags=set(); ss=[]; files=[]
    for v in lit.get(key,[]):
        if v in BIOS: tags.add(BIOS[v])
        h=hw(v)
        if h: tags.add(h)
        if v in strs:
            s=strs[v]
            (files if FILE.match(s) else ss).append(s)
    if files: tags.add('loads_files')
    tags|=feat.get(a,set())
    if int(r['callees'])==0 and int(r['indirect_calls'])==0: tags.add('leaf')
    reg=region(a); stat[reg]+=1
    for t in tags: tagstat[(reg,t)]+=1
    hint=next((t for t in ('bios_gdrom_misc','bios_flashrom','bios_sysinfo','bios_exit','pvr','ta_or_sq_const','aica','maple','gdrom_reg','loads_files') if t in tags),None)
    name='%s_%s%08x'%(reg,(hint+'_') if hint else '',a)
    sym.append((a,name,reg,r['insn_count'],callers[key],';'.join(sorted(tags)),'|'.join(files[:6]),'|'.join(x[:40] for x in ss[:3])))
import os
mp=os.path.join(os.path.dirname(os.path.abspath(out)),'names_manual.csv'); manual={}
if os.path.exists(mp):
    for r in csv.DictReader(open(mp,encoding='utf-8')): manual[int(r['address'],16)]=r['name']
sym=[(x[0],manual.get(x[0],x[1]))+x[2:] for x in sym]
with open(out+'/symbols.csv','w',newline='') as o:
    w=csv.writer(o); w.writerow(['address','name','region','insn_count','callers','tags','files','strings'])
    for s in sym: w.writerow(['0x%08x'%s[0]]+list(s[1:]))
print('blocos alinhados:',[(hex(s),hex(e)) for s,e in runs])
print('regioes:',dict(stat))
print('tags por regiao:'); 
for (reg,t),n in sorted(tagstat.items()): print('  %-8s %-16s %d'%(reg,t,n))

# --- fronteira do port: funcoes de biblioteca/SDK chamadas diretamente por codigo do jogo
reg={('%08x'%x[0]):x[2] for x in sym}; nm={('%08x'%x[0]):x for x in sym}
b=collections.defaultdict(set)
for f,cs in cg.items():
    if reg.get(f)=='game':
        for c in cs:
            if reg.get(c) in('lib_asm','lib_c','sdk'): b[c].add(f)
with open(out+'/port_boundary.csv','w',newline='') as o:
    w=csv.writer(o); w.writerow(['address','name','region','insn_count','game_callers','tags','strings'])
    for c,fs in sorted(b.items(),key=lambda kv:-len(kv[1])):
        x=nm[c]; w.writerow(['0x'+c,x[1],x[2],x[3],len(fs),x[5],x[7]])
bc=collections.Counter(reg[c] for c in b)
print('fronteira jogo->biblioteca:',len(b),dict(bc))
tot=sum(len(v) for v in b.values()); nmd=sum(len(v) for c,v in b.items() if int(c,16) in manual)
print('chamadas do jogo a biblioteca: %d; para funcoes com nome manual: %d (%.0f%%)'%(tot,nmd,100*nmd/tot))
print('chamadas diretas do jogo cobertas pelas 50 mais usadas: %.0f%%'%(100*sum(sorted((len(v) for v in b.values()),reverse=True)[:50])/sum(len(v) for v in b.values())))
gi=sum(int(r['indirect_calls']) for r in rows if reg['%08x'%int(r['address'],16)]=='game')
print('chamadas indiretas nao resolvidas em codigo do jogo:',gi)
