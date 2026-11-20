"""Minimal SH-4 (little-endian) decoder for Dreamcast binaries.
decode(op, pc) -> (mnemonic_text, kind, target, extra)
kind: None | 'bra' | 'bsr' | 'bcc' (conditional) | 'bcc_s' | 'rts' | 'jmp' | 'jsr' | 'braf' | 'bsrf' | 'rte' | 'lit4' | 'lit2' | 'bad'
"""
def s8(x):  return x-256 if x&0x80 else x
def s12(x): return x-4096 if x&0x800 else x
T=[]
def P(pat, fmt, kind=None): 
    mask=val=0
    for ch in pat:
        mask<<=4; val<<=4
        if ch in '0123456789ABCDEF': mask|=15; val|=int(ch,16)
    T.append((mask,val,pat,fmt,kind))
for pat,fmt,*k in [
 ('0n02','stc sr,r{n}'),('0n12','stc gbr,r{n}'),('0n22','stc vbr,r{n}'),('0n32','stc ssr,r{n}'),('0n42','stc spc,r{n}'),
 ('0n03','bsrf r{n}','bsrf'),('0n23','braf r{n}','braf'),('0n83','pref @r{n}'),('0n93','ocbi @r{n}'),('0nA3','ocbp @r{n}'),('0nB3','ocbwb @r{n}'),('0nC3','movca.l r0,@r{n}'),
 ('0nm4','mov.b r{m},@(r0,r{n})'),('0nm5','mov.w r{m},@(r0,r{n})'),('0nm6','mov.l r{m},@(r0,r{n})'),('0nm7','mul.l r{m},r{n}'),
 ('0008','clrt'),('0018','sett'),('0028','clrmac'),('0038','ldtlb'),('0048','clrs'),('0058','sets'),
 ('0009','nop'),('0019','div0u'),('0n29','movt r{n}'),
 ('0n0A','sts mach,r{n}'),('0n1A','sts macl,r{n}'),('0n2A','sts pr,r{n}'),('0n3A','stc sgr,r{n}'),('0n5A','sts fpul,r{n}'),('0n6A','sts fpscr,r{n}'),('0nFA','stc dbr,r{n}'),
 ('000B','rts','rts'),('001B','sleep'),('002B','rte','rte'),
 ('0nmC','mov.b @(r0,r{m}),r{n}'),('0nmD','mov.w @(r0,r{m}),r{n}'),('0nmE','mov.l @(r0,r{m}),r{n}'),('0nmF','mac.l @r{m}+,@r{n}+'),
 ('1nmd','mov.l r{m},@({d4},r{n})'),
 ('2nm0','mov.b r{m},@r{n}'),('2nm1','mov.w r{m},@r{n}'),('2nm2','mov.l r{m},@r{n}'),('2nm4','mov.b r{m},@-r{n}'),('2nm5','mov.w r{m},@-r{n}'),('2nm6','mov.l r{m},@-r{n}'),
 ('2nm7','div0s r{m},r{n}'),('2nm8','tst r{m},r{n}'),('2nm9','and r{m},r{n}'),('2nmA','xor r{m},r{n}'),('2nmB','or r{m},r{n}'),('2nmC','cmp/str r{m},r{n}'),('2nmD','xtrct r{m},r{n}'),('2nmE','mulu.w r{m},r{n}'),('2nmF','muls.w r{m},r{n}'),
 ('3nm0','cmp/eq r{m},r{n}'),('3nm2','cmp/hs r{m},r{n}'),('3nm3','cmp/ge r{m},r{n}'),('3nm4','div1 r{m},r{n}'),('3nm5','dmulu.l r{m},r{n}'),('3nm6','cmp/hi r{m},r{n}'),('3nm7','cmp/gt r{m},r{n}'),
 ('3nm8','sub r{m},r{n}'),('3nmA','subc r{m},r{n}'),('3nmB','subv r{m},r{n}'),('3nmC','add r{m},r{n}'),('3nmD','dmuls.l r{m},r{n}'),('3nmE','addc r{m},r{n}'),('3nmF','addv r{m},r{n}'),
 ('4n00','shll r{n}'),('4n10','dt r{n}'),('4n20','shal r{n}'),('4n01','shlr r{n}'),('4n11','cmp/pz r{n}'),('4n21','shar r{n}'),
 ('4n02','sts.l mach,@-r{n}'),('4n12','sts.l macl,@-r{n}'),('4n22','sts.l pr,@-r{n}'),('4n32','stc.l sgr,@-r{n}'),('4n52','sts.l fpul,@-r{n}'),('4n62','sts.l fpscr,@-r{n}'),('4nF2','stc.l dbr,@-r{n}'),
 ('4n03','stc.l sr,@-r{n}'),('4n13','stc.l gbr,@-r{n}'),('4n23','stc.l vbr,@-r{n}'),('4n33','stc.l ssr,@-r{n}'),('4n43','stc.l spc,@-r{n}'),
 ('4n04','rotl r{n}'),('4n24','rotcl r{n}'),('4n05','rotr r{n}'),('4n15','cmp/pl r{n}'),('4n25','rotcr r{n}'),
 ('4n06','lds.l @r{n}+,mach'),('4n16','lds.l @r{n}+,macl'),('4n26','lds.l @r{n}+,pr'),('4n56','lds.l @r{n}+,fpul'),('4n66','lds.l @r{n}+,fpscr'),('4nF6','ldc.l @r{n}+,dbr'),
 ('4n07','ldc.l @r{n}+,sr'),('4n17','ldc.l @r{n}+,gbr'),('4n27','ldc.l @r{n}+,vbr'),('4n37','ldc.l @r{n}+,ssr'),('4n47','ldc.l @r{n}+,spc'),
 ('4n08','shll2 r{n}'),('4n18','shll8 r{n}'),('4n28','shll16 r{n}'),('4n09','shlr2 r{n}'),('4n19','shlr8 r{n}'),('4n29','shlr16 r{n}'),
 ('4n0A','lds r{n},mach'),('4n1A','lds r{n},macl'),('4n2A','lds r{n},pr'),('4n5A','lds r{n},fpul'),('4n6A','lds r{n},fpscr'),('4nFA','ldc r{n},dbr'),
 ('4n0B','jsr @r{n}','jsr'),('4n1B','tas.b @r{n}'),('4n2B','jmp @r{n}','jmp'),('4nmC','shad r{m},r{n}'),('4nmD','shld r{m},r{n}'),
 ('4n0E','ldc r{n},sr'),('4n1E','ldc r{n},gbr'),('4n2E','ldc r{n},vbr'),('4n3E','ldc r{n},ssr'),('4n4E','ldc r{n},spc'),('4nmF','mac.w @r{m}+,@r{n}+'),
 ('5nmd','mov.l @({d4},r{m}),r{n}'),
 ('6nm0','mov.b @r{m},r{n}'),('6nm1','mov.w @r{m},r{n}'),('6nm2','mov.l @r{m},r{n}'),('6nm3','mov r{m},r{n}'),('6nm4','mov.b @r{m}+,r{n}'),('6nm5','mov.w @r{m}+,r{n}'),('6nm6','mov.l @r{m}+,r{n}'),
 ('6nm7','not r{m},r{n}'),('6nm8','swap.b r{m},r{n}'),('6nm9','swap.w r{m},r{n}'),('6nmA','negc r{m},r{n}'),('6nmB','neg r{m},r{n}'),('6nmC','extu.b r{m},r{n}'),('6nmD','extu.w r{m},r{n}'),('6nmE','exts.b r{m},r{n}'),('6nmF','exts.w r{m},r{n}'),
 ('7nii','add #{i},r{n}'),
 ('80md','mov.b r0,@({d1},r{m})'),('81md','mov.w r0,@({d2},r{m})'),('84md','mov.b @({d1},r{m}),r0'),('85md','mov.w @({d2},r{m}),r0'),
 ('88ii','cmp/eq #{i},r0'),('89ii','bt {t8}','bcc'),('8Bii','bf {t8}','bcc'),('8Dii','bt/s {t8}','bcc_s'),('8Fii','bf/s {t8}','bcc_s'),
 ('9nii','mov.w @({l2}),r{n}','lit2'),('Aiii','bra {t12}','bra'),('Biii','bsr {t12}','bsr'),
 ('C0ii','mov.b r0,@({u},gbr)'),('C1ii','mov.w r0,@({u2},gbr)'),('C2ii','mov.l r0,@({u4},gbr)'),('C3ii','trapa #{u}'),
 ('C4ii','mov.b @({u},gbr),r0'),('C5ii','mov.w @({u2},gbr),r0'),('C6ii','mov.l @({u4},gbr),r0'),('C7ii','mova @({l4}),r0','mova'),
 ('C8ii','tst #{u},r0'),('C9ii','and #{u},r0'),('CAii','xor #{u},r0'),('CBii','or #{u},r0'),('CCii','tst.b #{u},@(r0,gbr)'),('CDii','and.b #{u},@(r0,gbr)'),('CEii','xor.b #{u},@(r0,gbr)'),('CFii','or.b #{u},@(r0,gbr)'),
 ('Dnii','mov.l @({l4}),r{n}','lit4'),('Enii','mov #{i},r{n}','imm'),
 ('Fnm0','fadd fr{m},fr{n}'),('Fnm1','fsub fr{m},fr{n}'),('Fnm2','fmul fr{m},fr{n}'),('Fnm3','fdiv fr{m},fr{n}'),('Fnm4','fcmp/eq fr{m},fr{n}'),('Fnm5','fcmp/gt fr{m},fr{n}'),
 ('Fnm6','fmov.s @(r0,r{m}),fr{n}'),('Fnm7','fmov.s fr{m},@(r0,r{n})'),('Fnm8','fmov.s @r{m},fr{n}'),('Fnm9','fmov.s @r{m}+,fr{n}'),('FnmA','fmov.s fr{m},@r{n}'),('FnmB','fmov.s fr{m},@-r{n}'),('FnmC','fmov fr{m},fr{n}'),('FnmE','fmac fr0,fr{m},fr{n}'),
 ('Fn0D','fsts fpul,fr{n}'),('Fn1D','flds fr{n},fpul'),('Fn2D','float fpul,fr{n}'),('Fn3D','ftrc fr{n},fpul'),('Fn4D','fneg fr{n}'),('Fn5D','fabs fr{n}'),('Fn6D','fsqrt fr{n}'),('Fn7D','fsrra fr{n}'),
 ('Fn8D','fldi0 fr{n}'),('Fn9D','fldi1 fr{n}'),('FnAD','fcnvsd fpul,dr{n}'),('FnBD','fcnvds dr{n},fpul'),('FnED','fipr fv{m4},fv{n4}'),
 ('F3FD','fschg'),('FBFD','frchg'),('F1FD','ftrv xmtrx,fv0'),('F5FD','ftrv xmtrx,fv4'),('F9FD','ftrv xmtrx,fv8'),('FDFD','ftrv xmtrx,fv12'),('FnFD','fsca fpul,dr{n}'),
]: P(pat,fmt,k[0] if k else None)
# exact matches first (more fixed nibbles win)
T.sort(key=lambda t:-bin(t[0]).count('1'))
_cache={}
def _find(op):
    r=_cache.get(op)
    if r is None:
        r=False
        for t in T:
            if op&t[0]==t[1]: r=t; break
        if r is False and (op&0xF08F) in (0x0082,0x4083,0x4087,0x408E): r=(0,0,'bank','stc/ldc r_bank',None)
        _cache[op]=r
    return r
def decode(op, pc):
    t=_find(op)
    if not t: return ('.word 0x%04x'%op,'bad',None,None)
    _,_,pat,fmt,kind=t
    n=(op>>8)&15; m=(op>>4)&15; d=op&15; i8=op&255
    if pat[:2] in ('80','81','84','85'): m=(op>>4)&15
    tgt=None; extra=None
    if kind in('bcc','bcc_s'): tgt=pc+4+s8(i8)*2
    elif kind in('bra','bsr'): tgt=pc+4+s12(op&0xFFF)*2
    elif kind=='lit2': tgt=pc+4+i8*2
    elif kind in('lit4','mova'): tgt=(pc&~3)+4+i8*4
    elif kind=='imm': extra=s8(i8)
    if kind in('jsr','jmp','braf','bsrf','lit4','lit2','imm','mova'): extra=(n if kind!='mova' else 0, extra)
    txt=fmt.format(n=n,m=m,d4=d*4,d1=d,d2=d*2,i=s8(i8),u=i8,u2=i8*2,u4=i8*4,t8='0x%08x'%(tgt or 0),t12='0x%08x'%(tgt or 0),l2='0x%08x'%(tgt or 0),l4='0x%08x'%(tgt or 0),m4=(n&3),n4=(n>>2))
    return (txt,kind,tgt,extra)
