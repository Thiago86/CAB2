"""Ciclo de descoberta: roda o boot recompilado; se ele salta para codigo da imagem que o mapa nao conhece,
acrescenta o alvo a extra_entries.txt, regenera tudo e repete.  uso: discover.py RAIZ 1ST_READ.BIN IP.BIN [iteracoes]"""
import sys,os,re,subprocess,json
root,binp,ipp=[os.path.abspath(x) for x in sys.argv[1:4]]; iters=int(sys.argv[4]) if len(sys.argv)>4 else 8
T=os.path.join(root,'tools'); OUT=os.path.join(root,'out'); RC=os.path.join(root,'recomp'); EX=os.path.join(root,'extra_entries.txt')
TRK=os.environ.get('CT2_TRACK','/home/claude/work/crt2/track03.bin'); BASE=0x0C010000; END=BASE+os.path.getsize(binp)
def sh(cmd,**k): return subprocess.run(cmd,capture_output=True,text=True,**k)
def build():
    for s in (['analyze.py',binp,OUT,T],['classify.py',binp,OUT],['sh4c.py',binp,OUT,RC,'8']):
        r=sh([sys.executable,'-I',os.path.join(T,s[0])]+s[1:])
        if r.returncode: print(r.stdout[-400:],r.stderr[-400:]); sys.exit(1)
    srcs=sorted(f for f in os.listdir(RC) if f.startswith('ct2_funcs_') and f.endswith('.c'))+['ct2_dispatch.c','sh4rt.c','ta.c']
    objs=[]; procs=[]
    for s in srcs:
        o=os.path.join(RC,s+'.o'); objs.append(o); procs.append(subprocess.Popen(['gcc','-O0','-w','-c','-I'+RC,'-D_DEFAULT_SOURCE',os.path.join(RC,s),'-o',o]))
        if len(procs)>=2: [p.wait() for p in procs]; procs=[]
    [p.wait() for p in procs]
    r=sh(['gcc','-O0','-w','-D_DEFAULT_SOURCE','-I'+RC,os.path.join(RC,'boot.c')]+objs+['-o',os.path.join(RC,'boot'),'-lm'])
    if r.returncode: print(r.stderr[-600:]); sys.exit(1)
def norm(v): return v&0x1FFFFFFF if (v&0x1F000000)==0x0C000000 and (v>>29)!=7 else v
added=[]
for it in range(1,iters+1):
    build()
    env=dict(os.environ,CT2_AUTOPLAY='1',CT2_START_FRAME=os.environ.get('CT2_START_FRAME','400'),CT2_MAXSAVE='0')
    r=sh([os.path.join(RC,'boot'),binp,ipp,'0',os.environ.get('CT2_SECS','60')]+([TRK] if os.path.exists(TRK) else []),timeout=300,env=env); o=r.stdout
    m=re.search(r'=== PAROU: (.*?) ===\nendereco: 0x([0-9a-f]+)',o)
    nreg=re.search(r'tocados: (\d+)',o); 
    stk=re.findall(r'^  f_([0-9a-f]{8})$',o,re.M)
    print('iteracao %d: %s | registradores de hardware: %s | pilha: %s'%(it,(m.group(1)+' em 0x'+m.group(2)) if m else 'parou sem mensagem de falha',nreg.group(1) if nreg else '?',' '.join(stk[:4])))
    if not m or not m.group(1).startswith('salto para endereco sem funcao'): break
    a=norm(int(m.group(2),16))
    if not(BASE<=a<END) or a&1: break
    new=[a]
    # vizinhos do mesmo formato (trechos de 8 bytes: carrega constante, carrega alvo, jmp @rN, nop): sao casos de um mesmo switch
    d=open(binp,'rb').read(); w=lambda x:int.from_bytes(d[x-BASE:x-BASE+2],'little')
    isstub=lambda x: BASE<=x<END-8 and (w(x)&0xF000)==0xD000 and ((w(x+2)&0xF000) in (0xD000,0xE000)) and (w(x+4)&0xF0FF)==0x402B and w(x+6)==0x0009
    if isstub(a):
        for step in (8,-8):
            x=a+step
            while isstub(x): new.append(x); x+=step
    known=set()
    for l in open(EX):
        l=l.split('#')[0].strip()
        if l: known.add(norm(int(l,16)))
    with open(EX,'a') as f:
        for x in new:
            if x in known: continue
            f.write('%08x   # descoberto em tempo de execucao, saltado a partir de f_%s\n'%(x,stk[0] if stk else '?')); added.append(x)
print('entradas acrescentadas:',['%08x'%a for a in added])
open(os.path.join(OUT,'boot_log.txt'),'w').write(o)
