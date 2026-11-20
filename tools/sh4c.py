"""Tradutor estatico SH-4 -> C.  uso: sh4c.py 1ST_READ.BIN out recomp [n_arquivos]
Cada funcao do mapa vira `void f_XXXXXXXX(CPU *c)`; desvios viram goto; slots de atraso sao expandidos;
literais PC-relativos viram constantes. Instrucoes nao suportadas viram rt_unsupported() e a funcao e marcada parcial."""
import sys, json, struct, csv, collections, os
img=open(sys.argv[1],'rb').read(); out=sys.argv[2]; dst=sys.argv[3]; NFILES=int(sys.argv[4]) if len(sys.argv)>4 else 8
BASE=0x0C010000; END=BASE+len(img); REAL=0x80000000
def inimg(a): return BASE<=a<END
def u16(a): return struct.unpack_from('<H',img,a-BASE)[0]
def u32(a): return struct.unpack_from('<I',img,a-BASE)[0]
def s8(x): return x-256 if x&0x80 else x
def s12(x): return x-4096 if x&0x800 else x
insns={int(k,16):v for k,v in json.load(open(out+'/function_insns.json')).items()}
tables=json.load(open(out+'/tables.json')); ttargets=set(x for v in tables.values() for x in v[2])
sym={int(r['address'],16):r['name'] for r in csv.DictReader(open(out+'/symbols.csv'))}
entries=set(insns)
def R(n): return 'c->r[%d]'%n
def sx8(v): return '0x%08xu'%(v&0xFFFFFFFF)

ALU2={0x9:'{d}&={s}',0xA:'{d}^={s}',0xB:'{d}|={s}',0xC:'{d}+={s}',0x8:'{d}-={s}',0x3:'{d}={s}'}
def tr(op,pc):
    """instrucao sem desvio -> lista de comandos C; None se nao suportada. pc = endereco normalizado."""
    n=(op>>8)&15; m=(op>>4)&15; d=op&15; i8=op&255; hi=op>>12; rn=R(n); rm=R(m); st=[]
    A=st.append
    if hi==0:
        if op==0x0009: return []
        if op==0x0008: return ['c->t=0;']
        if op==0x0018: return ['c->t=1;']
        if op==0x0028: return ['c->mach=c->macl=0;']
        if op==0x0019: return ['c->m=c->q=c->t=0;']
        if d==4: return ['rt_w8(c,%s+R0,%s);'%(rn,rm)] if False else ['rt_w8(c,%s+c->r[0],%s);'%(rn,rm)]
        if d==5: return ['rt_w16(c,%s+c->r[0],%s);'%(rn,rm)]
        if d==6: return ['rt_w32(c,%s+c->r[0],%s);'%(rn,rm)]
        if d==7: return ['c->macl=%s*%s;'%(rn,rm)]
        if d==0xC: return ['%s=rt_r8(c,%s+c->r[0]);'%(rn,rm)]
        if d==0xD: return ['%s=rt_r16(c,%s+c->r[0]);'%(rn,rm)]
        if d==0xE: return ['%s=rt_r32(c,%s+c->r[0]);'%(rn,rm)]
        lo=op&0xFF
        if lo==0x29: return ['%s=c->t;'%rn]
        if lo==0x0A: return ['%s=c->mach;'%rn]
        if lo==0x1A: return ['%s=c->macl;'%rn]
        if lo==0x2A: return ['%s=c->pr;'%rn]
        if lo==0x5A: return ['%s=c->fpul;'%rn]
        if lo==0x6A: return ['%s=c->fpscr;'%rn]
        if lo==0x02: return ['%s=rt_get_sr(c);'%rn]
        if lo==0x12: return ['%s=c->gbr;'%rn]
        if lo==0x22: return ['%s=c->vbr;'%rn]
        if lo==0x83: return ['rt_pref(c,%s);'%rn]
        if lo in (0x93,0xA3,0xB3): return []
        if lo==0xC3: return ['rt_w32(c,%s,c->r[0]);'%rn]
        return None
    if hi==1: return ['rt_w32(c,%s+%d,%s);'%(rn,d*4,rm)]
    if hi==2:
        if d==0: return ['rt_w8(c,%s,%s);'%(rn,rm)]
        if d==1: return ['rt_w16(c,%s,%s);'%(rn,rm)]
        if d==2: return ['rt_w32(c,%s,%s);'%(rn,rm)]
        if d==4: return ['%s-=1; rt_w8(c,%s,%s);'%(rn,rn,rm)]
        if d==5: return ['%s-=2; rt_w16(c,%s,%s);'%(rn,rn,rm)]
        if d==6: return ['%s-=4; rt_w32(c,%s,%s);'%(rn,rn,rm)]
        if d==7: return ['c->q=%s>>31; c->m=%s>>31; c->t=(c->m!=c->q);'%(rn,rm)]
        if d==8: return ['c->t=((%s&%s)==0);'%(rn,rm)]
        if d in(9,0xA,0xB): return [ALU2[d].format(d=rn,s=rm)+';']
        if d==0xC: return ['{ uint32_t x_=%s^%s; c->t=!((x_&0xFF)&&(x_&0xFF00)&&(x_&0xFF0000)&&(x_&0xFF000000)); }'%(rn,rm)]
        if d==0xD: return ['%s=(%s<<16)|(%s>>16);'%(rn,rm,rn)] if False else ['%s=(%s<<16)|(%s>>16);'%(rn,rm,rn)]
        if d==0xE: return ['c->macl=(uint32_t)(uint16_t)%s*(uint32_t)(uint16_t)%s;'%(rn,rm)]
        if d==0xF: return ['c->macl=(uint32_t)((int32_t)(int16_t)%s*(int32_t)(int16_t)%s);'%(rn,rm)]
        return None
    if hi==3:
        if d==0: return ['c->t=(%s==%s);'%(rn,rm)]
        if d==2: return ['c->t=(%s>=%s);'%(rn,rm)]
        if d==3: return ['c->t=((int32_t)%s>=(int32_t)%s);'%(rn,rm)]
        if d==6: return ['c->t=(%s>%s);'%(rn,rm)]
        if d==7: return ['c->t=((int32_t)%s>(int32_t)%s);'%(rn,rm)]
        if d==4: return ['{ uint32_t old_q=c->q, t0, t1; c->q=%s>>31; %s=(%s<<1)|c->t; t0=%s; '
                        'if(old_q==0){ if(c->m==0){ %s-=%s; t1=(%s>t0); c->q=c->q?(t1==0):t1; } else { %s+=%s; t1=(%s<t0); c->q=c->q?t1:(t1==0); } }'
                        ' else { if(c->m==0){ %s+=%s; t1=(%s<t0); c->q=c->q?(t1==0):t1; } else { %s-=%s; t1=(%s>t0); c->q=c->q?t1:(t1==0); } }'
                        ' c->t=(c->q==c->m); }'%(rn,rn,rn,rn, rn,rm,rn, rn,rm,rn, rn,rm,rn, rn,rm,rn)]
        if d in(8,0xC): return [ALU2[d].format(d=rn,s=rm)+';']
        if d==5: return ['{ uint64_t p_=(uint64_t)%s*(uint64_t)%s; c->mach=(uint32_t)(p_>>32); c->macl=(uint32_t)p_; }'%(rn,rm)]
        if d==0xD: return ['{ int64_t p_=(int64_t)(int32_t)%s*(int64_t)(int32_t)%s; c->mach=(uint32_t)((uint64_t)p_>>32); c->macl=(uint32_t)p_; }'%(rn,rm)]
        if d==0xE: return ['{ uint32_t t1=%s+%s, t0=%s; %s=t1+c->t; c->t=(t0>t1); if(t1>%s) c->t=1; }'%(rn,rm,rn,rn,rn)]
        if d==0xA: return ['{ uint32_t t1=%s-%s, t0=%s; %s=t1-c->t; c->t=(t0<t1); if(t1<%s) c->t=1; }'%(rn,rm,rn,rn,rn)]
        if d==0xF: return ['{ uint32_t dd=%s>>31, ss=(%s>>31)+dd, aa; %s+=%s; aa=(%s>>31)+dd; c->t=((ss==0||ss==2)&&aa==1); }'%(rn,rm,rn,rm,rn)]
        if d==0xB: return ['{ uint32_t dd=%s>>31, ss=((%s>>31)^dd), aa; %s-=%s; aa=((%s>>31)^dd); c->t=(ss==1&&aa==1); }'%(rn,rm,rn,rm,rn)]
        return None
    if hi==4:
        lo=op&0xFF
        S={0x00:'c->t=%s>>31; %s<<=1;',0x20:'c->t=%s>>31; %s<<=1;',0x01:'c->t=%s&1; %s>>=1;',0x21:'c->t=%s&1; %s=(uint32_t)((int32_t)%s>>1);',
           0x10:'%s-=1; c->t=(%s==0);',0x11:'c->t=((int32_t)%s>=0);',0x15:'c->t=((int32_t)%s>0);',
           0x04:'{ uint32_t t_=%s>>31; %s=(%s<<1)|t_; c->t=t_; }',0x24:'{ uint32_t t_=%s>>31; %s=(%s<<1)|c->t; c->t=t_; }',
           0x05:'{ uint32_t t_=%s&1; %s=(%s>>1)|(t_<<31); c->t=t_; }',0x25:'{ uint32_t t_=%s&1; %s=(%s>>1)|((uint32_t)c->t<<31); c->t=t_; }',
           0x08:'%s<<=2;',0x18:'%s<<=8;',0x28:'%s<<=16;',0x09:'%s>>=2;',0x19:'%s>>=8;',0x29:'%s>>=16;'}
        if lo in S:
            f=S[lo]; return [f.replace('%s',rn)]
        if lo==0x02: return ['%s-=4; rt_w32(c,%s,c->mach);'%(rn,rn)]
        if lo==0x12: return ['%s-=4; rt_w32(c,%s,c->macl);'%(rn,rn)]
        if lo==0x22: return ['%s-=4; rt_w32(c,%s,c->pr);'%(rn,rn)]
        if lo==0x52: return ['%s-=4; rt_w32(c,%s,c->fpul);'%(rn,rn)]
        if lo==0x62: return ['%s-=4; rt_w32(c,%s,c->fpscr);'%(rn,rn)]
        if lo==0x03: return ['%s-=4; rt_w32(c,%s,rt_get_sr(c));'%(rn,rn)]
        if lo==0x13: return ['%s-=4; rt_w32(c,%s,c->gbr);'%(rn,rn)]
        if lo==0x23: return ['%s-=4; rt_w32(c,%s,c->vbr);'%(rn,rn)]
        if lo==0x06: return ['c->mach=rt_r32(c,%s); %s+=4;'%(rn,rn)]
        if lo==0x16: return ['c->macl=rt_r32(c,%s); %s+=4;'%(rn,rn)]
        if lo==0x26: return ['c->pr=rt_r32(c,%s); %s+=4;'%(rn,rn)]
        if lo==0x56: return ['c->fpul=rt_r32(c,%s); %s+=4;'%(rn,rn)]
        if lo==0x66: return ['rt_set_fpscr(c,rt_r32(c,%s)); %s+=4;'%(rn,rn)]
        if lo==0x07: return ['rt_set_sr(c,rt_r32(c,%s)); %s+=4;'%(rn,rn)]
        if lo==0x17: return ['c->gbr=rt_r32(c,%s); %s+=4;'%(rn,rn)]
        if lo==0x27: return ['c->vbr=rt_r32(c,%s); %s+=4;'%(rn,rn)]
        if lo==0x0A: return ['c->mach=%s;'%rn]
        if lo==0x1A: return ['c->macl=%s;'%rn]
        if lo==0x2A: return ['c->pr=%s;'%rn]
        if lo==0x5A: return ['c->fpul=%s;'%rn]
        if lo==0x6A: return ['rt_set_fpscr(c,%s);'%rn]
        if lo==0x0E: return ['rt_set_sr(c,%s);'%rn]
        if lo==0x1E: return ['c->gbr=%s;'%rn]
        if lo==0x2E: return ['c->vbr=%s;'%rn]
        if lo==0x1B: return ['{ uint32_t v_=rt_r8(c,%s)&0xFF; c->t=(v_==0); rt_w8(c,%s,v_|0x80); }'%(rn,rn)]
        if d==0xC: return ['{ int32_t s_=(int32_t)%s; uint32_t k_=(uint32_t)s_&31; if(s_>=0) %s<<=k_; else if(k_==0) %s=((int32_t)%s<0)?0xFFFFFFFFu:0; else %s=(uint32_t)((int32_t)%s>>(32-k_)); }'%(rm,rn,rn,rn,rn,rn)]
        if d==0xD: return ['{ int32_t s_=(int32_t)%s; uint32_t k_=(uint32_t)s_&31; if(s_>=0) %s<<=k_; else if(k_==0) %s=0; else %s>>=(32-k_); }'%(rm,rn,rn,rn)]
        return None
    if hi==5: return ['%s=rt_r32(c,%s+%d);'%(rn,rm,d*4)]
    if hi==6:
        if d==0: return ['%s=rt_r8(c,%s);'%(rn,rm)]
        if d==1: return ['%s=rt_r16(c,%s);'%(rn,rm)]
        if d==2: return ['%s=rt_r32(c,%s);'%(rn,rm)]
        if d==3: return ['%s=%s;'%(rn,rm)]
        if d==4: return ['%s=rt_r8(c,%s);%s'%(rn,rm,'' if n==m else ' %s+=1;'%rm)]
        if d==5: return ['%s=rt_r16(c,%s);%s'%(rn,rm,'' if n==m else ' %s+=2;'%rm)]
        if d==6: return ['%s=rt_r32(c,%s);%s'%(rn,rm,'' if n==m else ' %s+=4;'%rm)]
        if d==7: return ['%s=~%s;'%(rn,rm)]
        if d==8: return ['%s=(%s&0xFFFF0000u)|((%s&0xFF)<<8)|((%s>>8)&0xFF);'%(rn,rm,rm,rm)]
        if d==9: return ['%s=(%s<<16)|(%s>>16);'%(rn,rm,rm)]
        if d==0xA: return ['{ uint32_t t_=0-%s, v_=t_-c->t; c->t=(0<t_); if(t_<v_) c->t=1; %s=v_; }'%(rm,rn)]
        if d==0xB: return ['%s=0-%s;'%(rn,rm)]
        if d==0xC: return ['%s=%s&0xFF;'%(rn,rm)]
        if d==0xD: return ['%s=%s&0xFFFF;'%(rn,rm)]
        if d==0xE: return ['%s=(uint32_t)(int32_t)(int8_t)%s;'%(rn,rm)]
        if d==0xF: return ['%s=(uint32_t)(int32_t)(int16_t)%s;'%(rn,rm)]
    if hi==7: return ['%s+=%s;'%(rn,sx8(s8(i8)))]
    if hi==8:
        sub=(op>>8)&15
        if sub==0: return ['rt_w8(c,%s+%d,c->r[0]);'%(rm,d)]
        if sub==1: return ['rt_w16(c,%s+%d,c->r[0]);'%(rm,d*2)]
        if sub==4: return ['c->r[0]=rt_r8(c,%s+%d);'%(rm,d)]
        if sub==5: return ['c->r[0]=rt_r16(c,%s+%d);'%(rm,d*2)]
        if sub==8: return ['c->t=(c->r[0]==%s);'%sx8(s8(i8))]
        return None
    if hi==9:
        a=pc+4+i8*2
        if inimg(a): v=u16(a); v=v-65536 if v&0x8000 else v; return ['%s=%s;'%(rn,sx8(v))]
        return ['%s=rt_r16(c,0x%08xu);'%(rn,a|REAL)]
    if hi==0xD:
        a=(pc&~3)+4+i8*4
        if inimg(a): return ['%s=0x%08xu;'%(rn,u32(a))]
        return ['%s=rt_r32(c,0x%08xu);'%(rn,a|REAL)]
    if hi==0xC:
        sub=(op>>8)&15; u=i8
        if sub==0: return ['rt_w8(c,c->gbr+%d,c->r[0]);'%u]
        if sub==1: return ['rt_w16(c,c->gbr+%d,c->r[0]);'%(u*2)]
        if sub==2: return ['rt_w32(c,c->gbr+%d,c->r[0]);'%(u*4)]
        if sub==4: return ['c->r[0]=rt_r8(c,c->gbr+%d);'%u]
        if sub==5: return ['c->r[0]=rt_r16(c,c->gbr+%d);'%(u*2)]
        if sub==6: return ['c->r[0]=rt_r32(c,c->gbr+%d);'%(u*4)]
        if sub==7: return ['c->r[0]=0x%08xu;'%((((pc&~3)+4+u*4))|REAL)]
        if sub==8: return ['c->t=((c->r[0]&%d)==0);'%u]
        if sub==9: return ['c->r[0]&=%d;'%u]
        if sub==0xA: return ['c->r[0]^=%d;'%u]
        if sub==0xB: return ['c->r[0]|=%d;'%u]
        if sub==0xC: return ['c->t=((rt_r8(c,c->gbr+c->r[0])&0xFF&%d)==0);'%u]
        if sub in(0xD,0xE,0xF):
            o={0xD:'&',0xE:'^',0xF:'|'}[sub]
            return ['{ uint32_t a_=c->gbr+c->r[0]; rt_w8(c,a_,(rt_r8(c,a_)&0xFF)%s%d); }'%(o,u)]
        return None
    if hi==0xE: return ['%s=%s;'%(rn,sx8(s8(i8)))]
    if hi==0xF: return fpu(op,n,m,d,rn)
    return None
def fpu(op,n,m,d,rn):
    fr='c->fr[%d]'; FRn=fr%n; FRm=fr%m
    SZ='(c->fpscr&FPSCR_SZ)'
    if d in(0,1,2,3):
        o='+-*/'[d]; return ['NEED_SP(); %s %s= %s;'%(FRn,o,FRm)]
    if d==4: return ['NEED_SP(); c->t=(%s==%s);'%(FRn,FRm)]
    if d==5: return ['NEED_SP(); c->t=(%s>%s);'%(FRn,FRm)]
    if d==6: return ['if(%s) rt_ld64(c,%d,%s+c->r[0]); else %s=rt_rf(c,%s+c->r[0]);'%(SZ,n,R(m),FRn,R(m))]
    if d==7: return ['if(%s) rt_st64(c,%d,%s+c->r[0]); else rt_wf(c,%s+c->r[0],%s);'%(SZ,m,R(n),R(n),FRm)]
    if d==8: return ['if(%s) rt_ld64(c,%d,%s); else %s=rt_rf(c,%s);'%(SZ,n,R(m),FRn,R(m))]
    if d==9: return ['if(%s){ rt_ld64(c,%d,%s); %s+=8; } else { %s=rt_rf(c,%s); %s+=4; }'%(SZ,n,R(m),R(m),FRn,R(m),R(m))]
    if d==0xA: return ['if(%s) rt_st64(c,%d,%s); else rt_wf(c,%s,%s);'%(SZ,m,R(n),R(n),FRm)]
    if d==0xB: return ['if(%s){ %s-=8; rt_st64(c,%d,%s); } else { %s-=4; rt_wf(c,%s,%s); }'%(SZ,R(n),m,R(n),R(n),R(n),FRm)]
    if d==0xC: return ['if(%s) rt_mv64(c,%d,%d); else %s=%s;'%(SZ,m,n,FRn,FRm)]
    if d==0xE: return ['NEED_SP(); %s += c->fr[0]*%s;'%(FRn,FRm)]
    if d==0xD:
        if m==0: return ['c->fr[%d]=0;'%n] if False else ['{ uint32_t v_=c->fpul; memcpy(&%s,&v_,4); }'%FRn]
        if m==1: return ['memcpy(&c->fpul,&%s,4);'%FRn]
        if m==2: return ['NEED_SP(); %s=(float)(int32_t)c->fpul;'%FRn]
        if m==3: return ['NEED_SP(); c->fpul=rt_ftrc(%s);'%FRn]
        if m==4: return ['%s=-%s;'%(FRn,FRn)]
        if m==5: return ['%s=fabsf(%s);'%(FRn,FRn)]
        if m==6: return ['NEED_SP(); %s=sqrtf(%s);'%(FRn,FRn)]
        if m==7: return ['%s=1.0f/sqrtf(%s);'%(FRn,FRn)]
        if m==8: return ['%s=0.0f;'%FRn]
        if m==9: return ['%s=1.0f;'%FRn]
        if m==0xE: return ['rt_fipr(c,%d,%d);'%(n&3,n>>2)]
        if m==0xF:
            if n&1==0: return ['rt_fsca(c,%d);'%n]
            if n&3==1: return ['rt_ftrv(c,%d);'%(n>>2)]
            if n==3: return ['c->fpscr^=FPSCR_SZ;']
            if n==0xB: return ['rt_frchg(c);']
    return None

# ------------------------------------------------------------------ controle de fluxo / funcao
def real(a): return (a|REAL)&0xFFFFFFFF
def classify(op):
    hi=op>>12; lo=op&0xFF
    if hi==0xA: return 'bra'
    if hi==0xB: return 'bsr'
    if op>>8==0x89: return 'bt'
    if op>>8==0x8B: return 'bf'
    if op>>8==0x8D: return 'bts'
    if op>>8==0x8F: return 'bfs'
    if op==0x000B: return 'rts'
    if op==0x002B: return 'rte'
    if hi==4 and lo==0x2B: return 'jmp'
    if hi==4 and lo==0x0B: return 'jsr'
    if hi==0 and lo==0x23: return 'braf'
    if hi==0 and lo==0x03: return 'bsrf'
    return None
unsup=collections.Counter()
def emit_function(f):
    addrs=sorted(insns[f]); aset=set(addrs); L=[]; partial=[]
    def lab(a): return 'L_%08x'%a
    def stmts(op,pc):
        s=tr(op,pc)
        if s is None:
            partial.append((pc,op)); unsup['%04x'%op]+=1
            return ['rt_unsupported(c,0x%08xu,0x%04xu);'%(real(pc),op)]
        return s
    def slot(a):
        if (a+2) in aset: return stmts(u16(a+2),a+2)
        partial.append((a+2,0)); return ['rt_fail(c,"slot de atraso fora do mapa",0x%08xu);'%real(a+2)]
    def jump(t):
        return 'goto %s;'%lab(t) if t in aset else 'rt_fail(c,"alvo de desvio fora do mapa",0x%08xu);'%real(t)
    cases=[x for x in sorted(aset) if x in ttargets]
    sw=lambda var,dflt:'switch(%s&0x1FFFFFFFu){ %s default: %s }'%(var,' '.join('case 0x%08xu: goto %s;'%(x,lab(x)) for x in cases),dflt)
    if addrs and addrs[0]!=f: L.append('  goto %s;'%lab(f))
    for i,a in enumerate(addrs):
        op=u16(a); k=classify(op); n=(op>>8)&15; ft=a+2
        L.append('%s:;'%lab(a))
        if k is None:
            L+=['  '+x for x in stmts(op,a)]
        elif k=='bra':
            t=a+4+s12(op&0xFFF)*2; L+=['  '+x for x in slot(a)]+['  '+jump(t)]; ft=None
        elif k=='bsr':
            t=a+4+s12(op&0xFFF)*2; ft=a+4
            call='f_%08x(c);'%t if t in entries else 'rt_call(c,0x%08xu);'%real(t)
            L+=['  c->pr=0x%08xu;'%real(a+4)]+['  '+x for x in slot(a)]+['  '+call]
        elif k in('bt','bf'):
            t=a+4+s8(op&255)*2; L.append('  if(%sc->t) %s'%('' if k=='bt' else '!',jump(t)))
        elif k in('bts','bfs'):
            t=a+4+s8(op&255)*2; ft=a+4
            L+=['  { uint32_t t_=c->t;']+['    '+x for x in slot(a)]+['    if(%st_) %s }'%('' if k=='bts' else '!',jump(t))]
        elif k=='rts':
            L+=['  {']+['    '+x for x in slot(a)]+['    return; }']; ft=None
        elif k=='jsr':
            ft=a+4; L+=['  { uint32_t tgt_=%s; c->pr=0x%08xu;'%(R(n),real(a+4))]+['    '+x for x in slot(a)]+['    rt_call(c,tgt_); }']
        elif k=='bsrf':
            ft=a+4; L+=['  { uint32_t tgt_=0x%08xu+%s; c->pr=0x%08xu;'%(real(a+4),R(n),real(a+4))]+['    '+x for x in slot(a)]+['    rt_call(c,tgt_); }']
        elif k=='jmp':
            ft=None; L+=['  { uint32_t tgt_=%s;'%R(n)]+['    '+x for x in slot(a)]+['    '+sw('tgt_','rt_call(c,tgt_); return;')+' }']
        elif k=='braf':
            ft=None; L+=['  { uint32_t tgt_=0x%08xu+%s;'%(real(a+4),R(n))]+['    '+x for x in slot(a)]+['    '+sw('tgt_','rt_fail(c,"braf sem alvo mapeado",tgt_);')+' }']
        elif k=='rte':
            partial.append((a,op)); unsup['%04x'%op]+=1; L.append('  rt_unsupported(c,0x%08xu,0x%04xu);'%(real(a),op)); ft=None
        if ft is not None:
            nxt=addrs[i+1] if i+1<len(addrs) else None
            if nxt!=ft: L.append('  '+jump(ft))
    name=sym.get(f,'').replace('*/','').replace('/*','')
    head='/* %s%s */\nstatic void b_%08x(CPU *c) {\n'%(name,' [PARCIAL: %d instrucoes nao suportadas]'%len(partial) if partial else '',f)
    return head+'\n'.join(L)+'\n}\nvoid f_%08x(CPU *c) { rt_enter(c,0x%08xu); b_%08x(c); rt_leave(c); }\n'%(f,real(f),f), partial

os.makedirs(dst,exist_ok=True)
fl=sorted(insns); total=sum(len(insns[f]) for f in fl); per=total//NFILES+1
rep={}; files=[[]]; acc=0
for f in fl:
    code,partial=emit_function(f); rep[f]=len(partial)
    files[-1].append(code); acc+=len(insns[f])
    if acc>=per and len(files)<NFILES: files.append([]); acc=0
for i,fs in enumerate(files):
    with open(os.path.join(dst,'ct2_funcs_%02d.c'%i),'w') as o:
        o.write('#include "sh4rt.h"\n#include "ct2_funcs.h"\n')
        txt='\n'.join(fs)
        # remendo manual (cheat Timer): NOP condicional em 0x8C01DD40 (GameShark 0101DD40 00000009)
        a='L_0c01dd40:;\n  rt_w32(c,c->r[14]+12,c->r[1]);'
        if a in txt: txt=txt.replace(a,'L_0c01dd40:;\n  if(!ct2_cheat_notimer) rt_w32(c,c->r[14]+12,c->r[1]);   /* cheat Timer: NOP em 0x8C01DD40 */')
        a2='L_0c01a296:;\n  rt_w32(c,c->r[14]+4,c->r[3]);'
        if a2 in txt: txt=txt.replace(a2,'L_0c01a296:;\n  rt_w32(c,c->r[14]+4,ct2_edge_step((c->r[14]-0x0c1ea858u)/0x34u,c->r[3]));   /* v15u: borda sticky */')
        if 'static void b_0c033b1e' in txt:
            k=txt.index('static void b_0c033b1e'); a3='  c->r[4]=rt_r32(c,c->r[7]+4);'; m=txt.index(a3,k)
            txt=txt[:m]+a3+' ct2_edge_acc[0]=0;'+txt[m+len(a3):]
        a4='L_0c149aea:;\n  rt_w32(c,c->r[14]+16,c->r[5]);'
        if a4 in txt: txt=txt.replace(a4,'L_0c149aea:;\n  rt_w32(c,c->r[14]+16,ct2_lib_edge((c->r[14]-0x0c2cef88u)/0x34u,c->r[5]));   /* v15v: borda sticky */')
        a5='  c->r[3]=rt_r32(c,c->r[13]+16);\nL_0c01a296:;'
        if a5 in txt: txt=txt.replace(a5,'  c->r[3]=rt_r32(c,c->r[13]+16); ct2_lib_take((c->r[14]-0x0c1ea858u)/0x34u);\nL_0c01a296:;')
        a6='L_0c010a42:;\n  NEED_SP(); c->fr[4] += c->fr[0]*c->fr[3];'
        if a6 in txt: txt=txt.replace(a6,a6+'\n  if(!ct2_hop_orig()) c->fr[4]=c->fr[5];   /* v15zr: Crazy Hop sempre com forca maxima */')
        o.write(txt)
with open(os.path.join(dst,'ct2_funcs.h'),'w') as o:
    o.write('#ifndef CT2_FUNCS_H\n#define CT2_FUNCS_H\n#include "sh4rt.h"\n')
    for f in fl: o.write('void f_%08x(CPU *c);\n'%f)
    o.write('#endif\n')
HLE = {0x0c095c58:'hle_longjmp', 0x0c095ccc:'hle_setjmp'}   # setjmp/longjmp da biblioteca de backup como corotinas (sh4rt.c)
with open(os.path.join(dst,'ct2_dispatch.c'),'w') as o:
    o.write('#include "sh4rt.h"\n#include "ct2_funcs.h"\n#include <stdlib.h>\n#include <stdio.h>\n')
    o.write('typedef struct { uint32_t a; void (*f)(CPU*); } Ent;\n')
    o.write('void hle_setjmp(CPU *c); void hle_longjmp(CPU *c);   /* v15zz: setjmp/longjmp da biblioteca de backup como corotinas (sh4rt.c) */\n')
    o.write('static const Ent tab[]={\n')
    for f in fl: o.write(' {0x%08xu,%s},\n'%(f,HLE.get(f,'f_%08x'%f)))
    o.write('};\nconst unsigned ct2_nfuncs=%d;\n'%len(fl))
    o.write('static int cmp(const void*k,const void*e){ uint32_t a=*(const uint32_t*)k,b=((const Ent*)e)->a; return a<b?-1:a>b; }\n')
    o.write('void rt_call(CPU *c, uint32_t addr){ uint32_t a=addr&0x1FFFFFFFu; const Ent *e=bsearch(&a,tab,sizeof tab/sizeof tab[0],sizeof tab[0],cmp);\n  if(!e){ if(!rt_native_call(c,addr)) rt_missing(c,addr); return; }\n  { static int init=0, n=0; static uint32_t w[16]; if(!init){ init=1; const char*q=getenv("CT2_RETW"); while(q&&*q&&n<16){ w[n++]=(uint32_t)strtoul(q,(char**)&q,16)&0x1FFFFFFFu; if(*q==\',\') q++; } }   /* depuracao: CT2_RETW=lista hex -> imprime r0 ao retornar */\n    if(n){ int hit=0; for(int i=0;i<n;i++) if(w[i]==a) hit=1; if(hit){ uint32_t r4=c->r[4],r5=c->r[5],r6=c->r[6]; e->f(c); printf("[ret] f_%08x(r4=%08x r5=%08x r6=%08x) -> r0=%08x\\n",a,r4,r5,r6,c->r[0]); return; } } }\n  e->f(c); }\n')
complete=sum(1 for v in rep.values() if v==0)
json.dump({'%08x'%f:v for f,v in rep.items() if v},open(out+'/recomp_partial.json','w'))
print('funcoes: %d | completas: %d (%.1f%%) | parciais: %d | instrucoes nao suportadas (por opcode): %s'%(len(fl),complete,100*complete/len(fl),len(fl)-complete,unsup.most_common(12)))
print('instrucoes traduzidas:',total,'em',len(files),'arquivos')
