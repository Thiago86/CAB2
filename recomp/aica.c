/* aica.c - emulacao do subsistema de som do Dreamcast: CPU ARM7 (ARMv4, so modo ARM) + chip AICA (sem DSP).
   O jogo carrega na RAM de som o driver oficial "AICAsoundDrv Ver1.78j"; aqui ele roda de verdade.
   Premissas (nao verificadas em hardware real): ~256 instrucoes ARM por amostra de 44,1 kHz; curvas de envelope aproximadas;
   DSP (reverb/efeitos), LFO e filtro ignorados. */
#ifndef GENV
#define GENV(n) ({ static int _gi=0; static const char *_gv=0; if(!_gi){ _gv=getenv(n); _gi=1; } _gv; })   /* v15o: getenv() em laco quente custava ms por quadro no Windows */
#endif
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "aica.h"

#define ARAM_MASK 0x1FFFFFu
static uint8_t *aram;                 /* 2 MB, compartilhada com o SH-4 */
static uint8_t  rg[0x10000];          /* registradores AICA (0x700000..0x70FFFF visto do SH-4; 0x800000.. visto do ARM) */
static int      arm_run=0;
static unsigned long long arm_instr=0, n_fiq=0, n_ksc=0;
static unsigned long long n_mcipd=0;
int aica_trace=0; int ct2_free_mute=0; static uint32_t samples_total;
static volatile int lk=0;
#define LOCK() while(__sync_lock_test_and_set(&lk,1)) {}
#define UNLOCK() __sync_lock_release(&lk)

/* ---------------- CPU ARM7 ---------------- */
typedef struct {
    uint32_t R[16];                   /* registradores do modo atual; R[15] = endereco da instrucao atual */
    uint32_t usr_r8_12[5], fiq_r8_12[5];
    uint32_t bank_sp[6], bank_lr[6], spsr[6];   /* 0 usr/sys, 1 fiq, 2 irq, 3 svc, 4 abt, 5 und */
    uint32_t N,Z,C,V, I,F, mode;
} ARM;
static ARM A;
static int bidx(uint32_t m){ switch(m&0x1F){ case 0x11: return 1; case 0x12: return 2; case 0x13: return 3; case 0x17: return 4; case 0x1B: return 5; default: return 0; } }
static void set_mode(uint32_t nm){
    nm&=0x1F; if(nm==A.mode) return;
    int ob=bidx(A.mode), nb=bidx(nm);
    if(ob!=nb){
        A.bank_sp[ob]=A.R[13]; A.bank_lr[ob]=A.R[14];
        if(ob==1){ memcpy(A.fiq_r8_12,&A.R[8],20); memcpy(&A.R[8],A.usr_r8_12,20); }
        if(nb==1){ memcpy(A.usr_r8_12,&A.R[8],20); memcpy(&A.R[8],A.fiq_r8_12,20); }
        A.R[13]=A.bank_sp[nb]; A.R[14]=A.bank_lr[nb];
    }
    A.mode=nm;
}
static uint32_t get_cpsr(void){ return (A.N<<31)|(A.Z<<30)|(A.C<<29)|(A.V<<28)|(A.I<<7)|(A.F<<6)|A.mode; }
static void set_cpsr_all(uint32_t v){ A.N=v>>31&1; A.Z=v>>30&1; A.C=v>>29&1; A.V=v>>28&1; A.I=v>>7&1; A.F=v>>6&1; set_mode(v&0x1F); }
static uint32_t *ureg(int n){   /* registrador do banco USUARIO (para LDM/STM com ^) */
    if(bidx(A.mode)==0) return &A.R[n];
    if(n==13) return &A.bank_sp[0]; if(n==14) return &A.bank_lr[0];
    if(n>=8&&n<=12&&bidx(A.mode)==1) return &A.usr_r8_12[n-8];
    return &A.R[n];
}

/* barramento do ARM */
static uint32_t aica_rd_reg(uint32_t off,int size);
static void aica_wr_reg(uint32_t off,uint32_t v,int size,int from_arm);
static uint32_t mrd(uint32_t a,int size){
    if(a>=0x800000u&&a<0x810000u) return aica_rd_reg(a-0x800000u,size);
    a&=ARAM_MASK; uint32_t v=0;
    { static int er=-1; static int nr; if(er<0) er=GENV("CT2_ARMRING")!=0; if(er&&samples_total>1500000&&nr<400&&a>=0x400&&a<0x500&&size==4){ static uint32_t lastp=0; uint32_t v0; memcpy(&v0,aram+(a&~3u),4); if(v0){ nr++; uint32_t vv; memcpy(&vv,aram+(a&~3u),4); fprintf(stderr,"[ring] t=%u pc=%08x ld %03x=%08x\n",samples_total,A.R[15],a,vv); lastp=a; } } }
    { static int en=-1; if(en<0) en=GENV("CT2_ARMLD")!=0; if(en && a!=A.R[15]){ static uint32_t hist[1<<14]; static unsigned long long tot; hist[(a>>8)&0x3FFF]++; if(++tot%50000000ULL==0){ for(int i=0;i<(1<<14);i++) if(hist[i]>200000) fprintf(stderr,"ARMLD page %05x: %u\n",i<<8,hist[i]); } } }
    if(size==4){ a&=~3u; memcpy(&v,aram+a,4); } else if(size==2){ a&=~1u; uint16_t h; memcpy(&h,aram+a,2); v=h; } else v=aram[a];
    return v;
}
static void mwr(uint32_t a,uint32_t v,int size){
    { static int ew=-1; static int nw; if(ew<0) ew=GENV("CT2_ARMSLOT")!=0; if(ew&&samples_total>=1792020&&samples_total<=1792031&&nw<200&&(a&0x1FFFFF)>=0xd980&&(a&0x1FFFFF)<0xd9c0){ nw++; fprintf(stderr,"[slot] t=%u pc=%05x [%x]=%x sz%d\n",samples_total,A.R[15],a&0x1FFFFF,v,size); } }
    if(a>=0x800000u&&a<0x810000u){ static int e6=-1; static int n6; if(e6<0) e6=GENV("CT2_ARMCH6")!=0; if(e6){ uint32_t o=a-0x800000u; if(samples_total>=1791900&&samples_total<=1792200&&(o<0x2000?(o>>7)==6:(o==0x2800||o==0x2804||o==0x280c))&&n6<80){ n6++; fprintf(stderr,"[arm6] t=%u pc=%08x reg %04x = %08x sz%d\n",samples_total,A.R[15],o,v,size); } } }
    if(a>=0x800000u&&a<0x810000u){ { static int en=-1; static int nl; if(en<0) en=GENV("CT2_ARMWR")!=0; static int sk; if(en&&nl<300&&++sk>4000){ uint32_t o=a-0x800000u; if(o<0x2000&&(o&0x7F)<2&&(v&0x8000)||(o>=0x2800&&o<0x2c00&&o!=0x2894&&o!=0x28a4)){ nl++; fprintf(stderr,"[armwr] pc=%08x reg %04x = %08x sz%d\n",A.R[15],o,v,size); } } } aica_wr_reg(a-0x800000u,v,size,1); return; }
    a&=ARAM_MASK;
    if(size==4){ a&=~3u; memcpy(aram+a,&v,4); } else if(size==2){ a&=~1u; uint16_t h=(uint16_t)v; memcpy(aram+a,&h,2); } else aram[a]=(uint8_t)v;
}
static inline uint32_t ror32(uint32_t x,unsigned n){ n&=31; return n?(x>>n)|(x<<(32-n)):x; }
static inline uint32_t rdr(int n){ return n==15?A.R[15]+8:A.R[n]; }

static int cond_ok(uint32_t c){
    switch(c){ case 0:return A.Z; case 1:return !A.Z; case 2:return A.C; case 3:return !A.C; case 4:return A.N; case 5:return !A.N; case 6:return A.V; case 7:return !A.V;
        case 8:return A.C&&!A.Z; case 9:return !A.C||A.Z; case 10:return A.N==A.V; case 11:return A.N!=A.V; case 12:return !A.Z&&A.N==A.V; case 13:return A.Z||A.N!=A.V; default:return 1; }
}
static void take_exception(uint32_t newmode,uint32_t vec,uint32_t retaddr,int disable_f){
    uint32_t old=get_cpsr(); set_mode(newmode); A.spsr[bidx(newmode)]=old; A.R[14]=retaddr; A.I=1; if(disable_f) A.F=1; A.R[15]=vec;
}
static int pc_written;
static void wr_pc(uint32_t v){ A.R[15]=v&~3u; pc_written=1; }

static void arm_step(void){
    uint32_t pc=A.R[15]; uint32_t in=mrd(pc,4); pc_written=0; arm_instr++;
    { static int et=-1; static int nt; if(et<0) et=GENV("CT2_ARMPC")!=0; if(et&&samples_total>=1792027&&samples_total<=1792030&&nt<6000){ static uint32_t lp; if(pc==0x1c44){ uint32_t r=A.R[10]; fprintf(stderr,"[pc1c44] r10=%x [r10]=%02x [r10+3c]=%x r9=%x\n",r,r<0x200000?aram[r]:0,r<0x200000?*(uint32_t*)(aram+r+0x3c):0,A.R[9]); } if(pc!=lp+4){ nt++; fprintf(stderr,"[pc] t=%u %05x (r0=%x r9=%x r11=%x)\n",samples_total,pc,A.R[0],A.R[9],A.R[11]); } lp=pc; } }
    { static int en=-1; static long win=0; static uint8_t seen[0x8000]; static uint32_t cur=0; static int ntrig; if(en<0) en=GENV("CT2_ARMCMD")!=0;
      if(en){ if(win==0 && (arm_instr&255)==0 && ntrig<12){ for(int q=0;q<64;q++){ uint32_t w; memcpy(&w,aram+0x400+q*4,4); if((w&0xFF)==0xa9||(w&0xFF)==0xa5){ if(w!=cur){ cur=w; win=400000; ntrig++; memset(seen,0,sizeof seen); fprintf(stderr,"[armcmd] palavra %08x no slot %d (ptr44=%02x)\n",w,q,aram[0x44]); } break; } } }
        if(win>0){ win--; if(pc<0x8000) seen[pc>>2]=1; if(win==0){ fprintf(stderr,"[armcmd] pcs:"); int run=-1; for(int i=0;i<0x2000;i++){ if(seen[i]&&run<0) run=i; if(!seen[i]&&run>=0){ fprintf(stderr," %x-%x",run*4,(i-1)*4); run=-1; } } fprintf(stderr,"\n"); } } } }
    if(!cond_ok(in>>28)){ A.R[15]=pc+4; return; }
    int cls=(in>>25)&7;
    if(cls==0 && (in&0x0FFFFFF0u)==0x012FFF10u){ wr_pc(A.R[in&15]); goto done; }
    if(cls==0 && (in&0x0FC000F0u)==0x00000090u){   /* MUL/MLA */
        int rd=(in>>16)&15, rn=(in>>12)&15, rs=(in>>8)&15, rm=in&15; uint32_t r=A.R[rm]*A.R[rs]; if(in&(1u<<21)) r+=A.R[rn];
        A.R[rd]=r; if(in&(1u<<20)){ A.N=r>>31; A.Z=r==0; } goto done; }
    if(cls==0 && (in&0x0F8000F0u)==0x00800090u){   /* UMULL/UMLAL/SMULL/SMLAL */
        int rdhi=(in>>16)&15, rdlo=(in>>12)&15, rs=(in>>8)&15, rm=in&15; uint64_t r;
        if(in&(1u<<22)) r=(uint64_t)((int64_t)(int32_t)A.R[rm]*(int64_t)(int32_t)A.R[rs]); else r=(uint64_t)A.R[rm]*(uint64_t)A.R[rs];
        if(in&(1u<<21)) r+=((uint64_t)A.R[rdhi]<<32)|A.R[rdlo];
        A.R[rdlo]=(uint32_t)r; A.R[rdhi]=(uint32_t)(r>>32); if(in&(1u<<20)){ A.N=(uint32_t)(r>>63); A.Z=r==0; } goto done; }
    if(cls==0 && (in&0x0FB000F0u)==0x01000090u){   /* SWP/SWPB */
        int rn=(in>>16)&15, rd=(in>>12)&15, rm=in&15; uint32_t ad=A.R[rn]; int b=(in>>22)&1;
        uint32_t t=mrd(ad,b?1:4); if(!b) t=ror32(t,(ad&3)*8); mwr(ad,A.R[rm],b?1:4); A.R[rd]=t; goto done; }
    if(cls==0 && (in&0x90)==0x90 && (in&0x60)!=0){   /* LDRH/STRH/LDRSB/LDRSH */
        int p=(in>>24)&1,u=(in>>23)&1,imm=(in>>22)&1,w=(in>>21)&1,l=(in>>20)&1; int rn=(in>>16)&15, rd=(in>>12)&15, sh=(in>>5)&3;
        uint32_t off=imm?(((in>>4)&0xF0)|(in&15)):A.R[in&15]; uint32_t base=rdr(rn); uint32_t ad=p?(u?base+off:base-off):base;
        if(l){ uint32_t v; if(sh==1){ v=mrd(ad,2); } else if(sh==2){ v=(uint32_t)(int32_t)(int8_t)mrd(ad,1); } else { v=(uint32_t)(int32_t)(int16_t)mrd(ad,2); }
            if(!p||w) A.R[rn]=p?ad:(u?base+off:base-off); if(rd==15) wr_pc(v); else A.R[rd]=v; }
        else { uint32_t v=rd==15?A.R[15]+12:A.R[rd]; mwr(ad,v,2); if(!p||w) A.R[rn]=p?ad:(u?base+off:base-off); }
        goto done; }
    if(cls==0||cls==1){
        uint32_t op=(in>>21)&15; int s=(in>>20)&1; int rn=(in>>16)&15, rd=(in>>12)&15;
        /* MRS / MSR */
        if(!s && op>=8 && op<=11){
            if((in&0x0FBF0FFFu)==0x010F0000u){ A.R[rd]=(in&(1u<<22))?A.spsr[bidx(A.mode)]:get_cpsr(); goto done; }
            if((in&0x0DB0F000u)==0x0120F000u){
                uint32_t v; if(cls==1) v=ror32(in&255,((in>>8)&15)*2); else v=A.R[in&15];
                uint32_t mask=0; if(in&(1u<<19)) mask|=0xFF000000u; if(in&(1u<<16)) mask|=0x000000FFu;
                if(in&(1u<<22)){ int b=bidx(A.mode); A.spsr[b]=(A.spsr[b]&~mask)|(v&mask); }
                else { if(A.mode==0x10) mask&=0xFF000000u; uint32_t cur=get_cpsr(); set_cpsr_all((cur&~mask)|(v&mask)); }
                goto done; }
        }
        uint32_t b, carry=A.C;
        if(cls==1){ uint32_t rot=((in>>8)&15)*2; b=ror32(in&255,rot); if(rot) carry=b>>31; }
        else { uint32_t rm=in&15; uint32_t v=rdr(rm); unsigned type=(in>>5)&3; unsigned amt;
            if(in&(1u<<4)){ amt=A.R[(in>>8)&15]&255; if(rm==15) v=A.R[15]+12;
                if(amt==0){ b=v; } else switch(type){
                    case 0: if(amt<32){ carry=(v>>(32-amt))&1; b=v<<amt; } else if(amt==32){ carry=v&1; b=0; } else { carry=0; b=0; } break;
                    case 1: if(amt<32){ carry=(v>>(amt-1))&1; b=v>>amt; } else if(amt==32){ carry=v>>31; b=0; } else { carry=0; b=0; } break;
                    case 2: if(amt<32){ carry=(v>>(amt-1))&1; b=(uint32_t)((int32_t)v>>amt); } else { carry=v>>31; b=(uint32_t)((int32_t)v>>31); } break;
                    default: amt&=31; if(amt==0){ carry=v>>31; b=v; } else { carry=(v>>(amt-1))&1; b=ror32(v,amt); } } }
            else { amt=(in>>7)&31;
                switch(type){
                    case 0: if(amt){ carry=(v>>(32-amt))&1; b=v<<amt; } else b=v; break;
                    case 1: if(amt){ carry=(v>>(amt-1))&1; b=v>>amt; } else { carry=v>>31; b=0; } break;
                    case 2: if(amt){ carry=(v>>(amt-1))&1; b=(uint32_t)((int32_t)v>>amt); } else { carry=v>>31; b=(uint32_t)((int32_t)v>>31); } break;
                    default: if(amt){ carry=(v>>(amt-1))&1; b=ror32(v,amt); } else { carry=v&1; b=(v>>1)|((uint32_t)A.C<<31); } } } }
        uint32_t a=(in&(1u<<4)&&cls==0&&rn==15)?A.R[15]+12:rdr(rn), r=0; int logical=0, wr=1; uint64_t t;
        switch(op){
            case 0: r=a&b; logical=1; break; case 1: r=a^b; logical=1; break;
            case 2: r=a-b; t=(uint64_t)a-b; if(s){ A.C=a>=b; A.V=(((a^b)&(a^r))>>31)&1; } break;
            case 3: r=b-a; if(s){ A.C=b>=a; A.V=(((b^a)&(b^r))>>31)&1; } break;
            case 4: r=a+b; t=(uint64_t)a+b; if(s){ A.C=t>>32; A.V=((~(a^b)&(a^r))>>31)&1; } break;
            case 5: r=a+b+A.C; t=(uint64_t)a+b+A.C; if(s){ A.C=t>>32; A.V=((~(a^b)&(a^r))>>31)&1; } break;
            case 6: r=a-b-(1-A.C); t=(uint64_t)a-b-(1-A.C); if(s){ A.C=!(t>>32); A.V=(((a^b)&(a^r))>>31)&1; } break;
            case 7: r=b-a-(1-A.C); t=(uint64_t)b-a-(1-A.C); if(s){ A.C=!(t>>32); A.V=(((b^a)&(b^r))>>31)&1; } break;
            case 8: r=a&b; logical=1; wr=0; break; case 9: r=a^b; logical=1; wr=0; break;
            case 10: r=a-b; wr=0; A.C=a>=b; A.V=(((a^b)&(a^r))>>31)&1; break;
            case 11: r=a+b; wr=0; t=(uint64_t)a+b; A.C=t>>32; A.V=((~(a^b)&(a^r))>>31)&1; break;
            case 12: r=a|b; logical=1; break; case 13: r=b; logical=1; break; case 14: r=a&~b; logical=1; break; default: r=~b; logical=1; }
        if(s){ A.N=r>>31; A.Z=r==0; if(logical) A.C=carry; }
        if(wr){ if(rd==15){ if(s&&bidx(A.mode)){ uint32_t sp=A.spsr[bidx(A.mode)]; wr_pc(r); set_cpsr_all(sp); } else wr_pc(r); } else A.R[rd]=r; }
        else if(!s) {}
        goto done; }
    if(cls==2||cls==3){   /* LDR/STR */
        int pre=(in>>24)&1,u=(in>>23)&1,byte=(in>>22)&1,w=(in>>21)&1,l=(in>>20)&1; int rn=(in>>16)&15, rd=(in>>12)&15; uint32_t off;
        if(cls==2) off=in&0xFFF; else { uint32_t v=A.R[in&15]; if((in&15)==15) v=A.R[15]+8; unsigned amt=(in>>7)&31, type=(in>>5)&3;
            switch(type){ case 0: off=v<<amt; break; case 1: off=amt?v>>amt:0; break; case 2: off=amt?(uint32_t)((int32_t)v>>amt):(uint32_t)((int32_t)v>>31); break; default: off=amt?ror32(v,amt):(v>>1)|((uint32_t)A.C<<31); } }
        uint32_t base=rdr(rn); uint32_t ad=pre?(u?base+off:base-off):base; uint32_t nb=u?base+off:base-off;
        if(l){ uint32_t v; if(byte) v=mrd(ad,1); else { v=mrd(ad,4); v=ror32(v,(ad&3)*8); }
            if(!pre||w) A.R[rn]=pre?ad:nb; if(rd==15) wr_pc(v); else A.R[rd]=v; }
        else { uint32_t v=rd==15?A.R[15]+12:A.R[rd]; if(byte) mwr(ad,v&255,1); else mwr(ad,v,4); if(!pre||w) A.R[rn]=pre?ad:nb; }
        goto done; }
    if(cls==4){   /* LDM/STM */
        int pre=(in>>24)&1,u=(in>>23)&1,s=(in>>22)&1,w=(in>>21)&1,l=(in>>20)&1; int rn=(in>>16)&15; uint32_t list=in&0xFFFF; int n=__builtin_popcount(list);
        uint32_t base=A.R[rn]; uint32_t ad, nb;
        if(u){ ad=pre?base+4:base; nb=base+4*n; } else { ad=pre?base-4*n:base-4*n+4; nb=base-4*n; }
        int usrbank=s&&!(l&&(list&0x8000)); uint32_t ad0=ad;
        for(int i=0;i<16;i++) if(list&(1u<<i)){
            if(l){ uint32_t v=mrd(ad,4); if(i==15){ wr_pc(v); if(s&&bidx(A.mode)) set_cpsr_all(A.spsr[bidx(A.mode)]); } else if(usrbank) *ureg(i)=v; else A.R[i]=v; }
            else { uint32_t v=i==15?A.R[15]+12:(usrbank?*ureg(i):A.R[i]); mwr(ad,v,4); }
            ad+=4; }
        (void)ad0; if(w && !(l&&(list>>rn&1))) A.R[rn]=nb; goto done; }
    if(cls==5){ int32_t off=(int32_t)(in<<8)>>6; uint32_t tgt=A.R[15]+8+off; if(in&(1u<<24)) A.R[14]=A.R[15]+4; wr_pc(tgt); goto done; }
    if(cls==7 && (in&(1u<<24))){ /* SWI */ take_exception(0x13,0x08,A.R[15]+4,0); pc_written=1; goto done; }
    /* coprocessador / indefinida: trata como excecao de instrucao indefinida */
    take_exception(0x1B,0x04,A.R[15]+4,0); pc_written=1;
done:
    if(!pc_written) A.R[15]=pc+4;
}
static void arm_reset(void){
    memset(&A,0,sizeof A); A.mode=0x13; A.I=1; A.F=1; A.R[15]=0; pc_written=0;
}

/* ---------------- AICA: canais ---------------- */
typedef struct {
    int active, st;                 /* st: 0 ataque, 1 decaimento 1, 2 decaimento 2, 3 liberacao */
    int32_t eg;                     /* atenuacao 0 (alto) .. 0x1FFF (mudo), em 1/256 de unidade: eg>>8 */
    uint32_t idx, frac;             /* posicao (amostras) e fracao 16 bits */
    int32_t s0,s1;
    int ad_pred, ad_step, ad_lp_pred, ad_lp_step, ad_have_lp;
    int keyon;
    int mute;
} Chan;
static Chan ch[64];
static float gain_eg[8192/4+1], gain_tl[256], gain_dis[16], gain_pan[16];
static int32_t inc_tab[32], att_tab[32];
static int tabs_ok=0;
static void init_tabs(void){
    if(tabs_ok) return; tabs_ok=1;
    for(int i=0;i<=8192/4;i++) gain_eg[i]=powf(10.f,-(i*4*96.f/8192.f)/20.f);
    for(int i=0;i<256;i++) gain_tl[i]=powf(10.f,-(i*0.375f)/20.f);
    gain_dis[0]=0; for(int i=1;i<16;i++) gain_dis[i]=powf(10.f,-((15-i)*3.f)/20.f);
    for(int i=0;i<15;i++) gain_pan[i]=powf(10.f,-(i*3.f)/20.f); gain_pan[15]=0;
    inc_tab[0]=0; att_tab[0]=0;
    for(int r=1;r<32;r++){ double T=0.0013*pow(2.0,(31-r)/2.0); double T2=T*0.35;   /* segundos para percorrer toda a faixa (premissa: aproximacao) */
        double per=8192.0*256.0/(T*44100.0); inc_tab[r]=(int32_t)(per<1?1:per); double pa=8192.0*256.0/(T2*44100.0); att_tab[r]=(int32_t)(pa<1?1:pa); }
    inc_tab[31]=8192*256; att_tab[31]=8192*256; att_tab[30]=8192*64; att_tab[29]=8192*32;
}
static inline uint32_t cr(int c,int off){ uint16_t h; memcpy(&h,rg+c*0x80+off,2); return h; }
static const int adpcm_scale[8]={230,230,230,230,307,409,512,614};
static const int adpcm_mul[16]={1,3,5,7,9,11,13,15,-1,-3,-5,-7,-9,-11,-13,-15};
static inline int16_t adpcm_dec(Chan *c,int nib){
    int v=c->ad_pred+((adpcm_mul[nib]*c->ad_step)>>3); if(v>32767) v=32767; if(v<-32768) v=-32768; c->ad_pred=v;
    int st=(c->ad_step*adpcm_scale[nib&7])>>8; if(st<127) st=127; if(st>24576) st=24576; c->ad_step=st; return (int16_t)v;
}
/* amostra de indice i do canal c (leitura sequencial no caso ADPCM) */
static int32_t fetch_sample(int ci,Chan *c,uint32_t i){
    uint32_t r0=cr(ci,0), sa=((r0&0x7F)<<16)|cr(ci,4); int fmt=(r0>>7)&3;
    if(fmt==0){ uint32_t a=(sa+i*2)&ARAM_MASK; int16_t h; memcpy(&h,aram+a,2); return h; }
    if(fmt==1){ uint32_t a=(sa+i)&ARAM_MASK; return (int32_t)(int8_t)aram[a]<<8; }
    /* ADPCM (Yamaha): nibble baixo primeiro */
    uint32_t lsa=cr(ci,8);
    if(i==lsa && !c->ad_have_lp){ c->ad_lp_pred=c->ad_pred; c->ad_lp_step=c->ad_step; c->ad_have_lp=1; }
    uint32_t a=(sa+(i>>1))&ARAM_MASK; int nib=(i&1)?(aram[a]>>4):(aram[a]&15); return adpcm_dec(c,nib);
}
static FILE*voz_f=0; static int voz_n=0;
void aica_voz_nota(const char*t){ extern unsigned ta_cur_frame; if(!voz_f) voz_f=fopen("voz.txt","a"); if(voz_f&&voz_n<500){ voz_n++; fprintf(voz_f,"f%u >>> %s\n",ta_cur_frame,t); fflush(voz_f); } }
static unsigned kf_[64]; static unsigned mute_until=0; extern unsigned ta_cur_frame;
void aica_aviso(unsigned F){ LOCK(); mute_until=F+8; for(int i=0;i<64;i++) if(ch[i].active&&ct2_free_mute&&kf_[i]+4>=F&&((cr(i,0)>>7)&3)==0&&cr(i,12)==8192) ch[i].mute=1; UNLOCK(); }
static void chan_keyon(int ci){
    Chan *c=&ch[ci]; init_tabs();
    memset(c,0,sizeof *c); c->active=1; c->st=0; c->eg=0x1FFF<<8; c->idx=0; c->frac=0; c->ad_step=127; c->ad_pred=0;
    c->s0=fetch_sample(ci,c,0); c->s1=fetch_sample(ci,c,1);
    if(aica_trace) printf("[aica] t=%u key-on canal %d fmt=%u sa=%06x lsa=%u lea=%u lp=%u oct=%u fns=%u ar=%u dl=%u disdl=%u tl=%u\n",samples_total,ci,(cr(ci,0)>>7)&3,((cr(ci,0)&0x7F)<<16)|cr(ci,4),cr(ci,8),cr(ci,12),(cr(ci,0)>>9)&1,(cr(ci,0x18)>>11)&15,cr(ci,0x18)&0x3FF,cr(ci,0x10)&31,(cr(ci,0x14)>>5)&31,(cr(ci,0x24)>>8)&15,(cr(ci,0x28)>>8)&255);
    { extern unsigned ta_cur_frame; uint32_t sa=((cr(ci,0)&0x7F)<<16)|cr(ci,4); int fm=(cr(ci,0)>>7)&3;
      if(cr(ci,12)>=1500){ static int nl=0; if(!voz_f) voz_f=fopen("voz.txt","w"); if(voz_f&&nl<12000){ nl++; fprintf(voz_f,"f%u canal %d fmt=%d sa=%06x len=%u\n",ta_cur_frame,ci,fm,sa,cr(ci,12)); fflush(voz_f); } }
      { static uint32_t mt[16]; static int nm=-1; if(nm<0){ nm=0; const char*e=getenv("CT2_MUTE"); while(e&&*e&&nm<16){ mt[nm++]=(uint32_t)strtoul(e,(char**)&e,16); if(*e==',') e++; } }
        for(int q=0;q<nm;q++) if(mt[q]==sa){ c->active=0; return; } }
      if(0&&ct2_free_mute&&(sa==0x0b28f8u||sa==0x0bb244u||sa==0x0b16c4u)){ c->active=0; return; } }
    kf_[ci]=ta_cur_frame; if(ct2_free_mute&&ta_cur_frame<=mute_until&&((cr(ci,0)>>7)&3)==0&&cr(ci,12)==8192) c->mute=1;
    n_ksc++;
}
static void kyonex(void){
    for(int i=0;i<64;i++){
        uint32_t r0=cr(i,0); int on=(r0>>14)&1;
        if(on && !ch[i].keyon){ chan_keyon(i); }
        else if(!on && ch[i].keyon && ch[i].active){ ch[i].st=3; if(aica_trace) printf("[aica] t=%u key-off canal %d\n",samples_total,i); }
        ch[i].keyon=on;
    }
}
static void chan_advance(int ci,Chan *c){
    uint32_t r0=cr(ci,0), lsa=cr(ci,8), lea=cr(ci,12); int loop=(r0>>9)&1;
    c->s0=c->s1; c->idx++;
    uint32_t ni=c->idx+1;
    if(c->idx>=lea && !loop){ c->active=0; return; }
    if(ni>=lea){ if(loop){ ni=lsa; if(((r0>>7)&3)>=2 && c->ad_have_lp){ c->ad_pred=c->ad_lp_pred; c->ad_step=c->ad_lp_step; } } else ni=lea; }
    if(c->idx>=lea && loop){ c->idx=lsa+(c->idx-lea); }
    c->s1=fetch_sample(ci,c,ni);
}

static inline uint32_t r16(uint32_t o){ uint16_t h; memcpy(&h,rg+o,2); return h; }
static inline void w16(uint32_t o,uint32_t v){ uint16_t h=(uint16_t)v; memcpy(rg+o,&h,2); }
/* ---------------- DSP (128 passos, 44,1 kHz). Estrutura conforme documentacao publica do AICA/SCSP; PACK/UNPACK = formato de ponto flutuante de 16 bits da memoria ---------------- */
static int32_t d_temp[128], d_mems[32], d_mixs[16], d_efreg[16], d_acc, d_frc, d_yreg, d_adrs, d_memval[4]; static uint32_t d_dec=0;
static int dsp_dirty=1, dsp_on=0, dsp_off=0; static int dsp_ord=0;
static inline int32_t sx24(int32_t v){ return (v<<8)>>8; }
static inline int32_t dsp_unpack(uint16_t val){
    int sign=(val>>15)&1, exponent=(val>>11)&0xF, mantissa=val&0x7FF; int32_t uval=mantissa<<11;
    if(exponent>11) exponent=11; else uval|=(sign^1)<<22;
    uval|=sign<<23; uval<<=8; uval>>=8; uval>>=exponent; return uval; }
static inline uint16_t dsp_pack(int32_t val){
    uint32_t temp; int sign=(val>>23)&1, exponent=0; temp=(val^(val<<1))&0xFFFFFF;
    for(int k=0;k<12;k++){ if(temp&0x800000) break; temp<<=1; exponent++; }
    if(exponent<12) val=(val<<exponent)&0x3FFFFF; else val<<=11;
    val>>=11; val|=sign<<15; val|=exponent<<11; return (uint16_t)val; }
static void dsp_check(void){ if(!dsp_dirty) return; dsp_dirty=0; dsp_on=0; for(int i=0;i<512;i++) if(r16(0x3400+i*4)){ dsp_on=1; break; } }
static unsigned long long dsp_calls=0; static int dsp_maxef=0; static unsigned long long dsp_sends=0;
static int dsp_maxmix=0;
static void dsp_run(void){
    for(int q=0;q<16;q++){ int v=d_mixs[q]<0?-d_mixs[q]:d_mixs[q]; if(v>dsp_maxmix) dsp_maxmix=v; }
    dsp_check(); if(dsp_off) dsp_on=0; if(!dsp_on){ return; }
    if(GENV("CT2_RINGDBG")){ static int n=0; static unsigned long long k=0; if((k++%44100)==0&&n<12){ n++; fprintf(stderr,"RING t=%llu RBP=%x RBL=%x efmax=%d\n",k,r16(0x2804)&0xFFF,(r16(0x2804)>>13)&3,dsp_maxef); } }
    { static unsigned idle=0; int any=0; for(int q=0;q<16;q++) if(d_mixs[q]){ any=1; break; }
      if(any){ idle=0; } else if(idle<200000u) idle++;
      if(idle>=200000u){ int ef=0; for(int e=0;e<16;e++) if(d_efreg[e]>64||d_efreg[e]<-64){ ef=1; break; } if(!ef){ for(int e=0;e<16;e++) d_efreg[e]=0; return; } else idle=0; } }   /* economia: sem entrada ha ~4,5 s e cauda do reverb extinta */
    uint32_t rbl=0x2000u<<((r16(0x2804)>>13)&3), rbp=(uint32_t)(r16(0x2804)&0xFFF)<<10;   /* unidades de palavra de 16 bits (RBP: blocos de 0x2000 palavras) */
    { static uint32_t last_rb=0xFFFFFFFFu; uint32_t key=(uint32_t)(r16(0x2804)&0x7FFF); if(key!=last_rb){ last_rb=key; if((rbp+rbl)*2<=0x200000u) memset(aram+rbp*2,0,rbl*2); } }   /* a memoria de reverb comeca zerada (o ARAM chega com lixo e realimenta o DSP -> estouro) */
    int32_t ACC=d_acc;
    for(int step=0;step<128;step++){
        uint32_t w0=r16(0x3400+step*16), w1=r16(0x3400+step*16+4), w2=r16(0x3400+step*16+8), w3=r16(0x3400+step*16+12);
        uint32_t TRA=(w0>>9)&0x7F, TWT=(w0>>8)&1, TWA=(w0>>1)&0x7F;
        uint32_t XSEL=(w1>>15)&1, YSEL=(w1>>13)&3, IRA=(w1>>7)&0x3F, IWT=(w1>>6)&1, IWA=(w1>>1)&0x1F;
        uint32_t TABLE=(w2>>15)&1, MWT=(w2>>14)&1, MRD=(w2>>13)&1, EWT=(w2>>12)&1, EWA=(w2>>8)&15, ADRL=(w2>>7)&1, FRCL=(w2>>6)&1, SHFT=(w2>>4)&3, YRL=(w2>>3)&1, NEGB=(w2>>2)&1, ZERO=(w2>>1)&1, BSEL=w2&1;
        uint32_t NOFL=(w3>>15)&1, MASA=(w3>>9)&0x3F, ADREB=(w3>>8)&1, NXADR=(w3>>7)&1;
        int32_t INPUTS=0;
        if(IRA<=0x1F) INPUTS=d_mems[IRA]; else if(IRA<=0x2F) INPUTS=d_mixs[IRA-0x20]<<4; else if(IRA<=0x31) INPUTS=0;
        INPUTS=sx24(INPUTS);
        if(IWT){ d_mems[IWA]=d_memval[step&3]; if(IRA==IWA) INPUTS=d_memval[step&3]; }
        int32_t B; if(ZERO) B=0; else { B=BSEL?ACC:sx24(d_temp[(TRA+d_dec)&0x7F]); if(NEGB) B=0-B; }
        int32_t X=XSEL?INPUTS:sx24(d_temp[(TRA+d_dec)&0x7F]);
        int32_t Y; uint32_t cf=r16(0x3000+step*4);
        if(YSEL==0) Y=d_frc; else if(YSEL==1) Y=(int16_t)cf>>3; else if(YSEL==2) Y=(d_yreg>>11)&0x1FFF; else Y=(d_yreg>>4)&0xFFF;
        if(YRL) d_yreg=INPUTS&0xFFFFFF;
        Y=(Y<<19)>>19;   /* 13 bits com sinal */
        int32_t SHIFTED;
        #define DO_SHIFT(accv) { int32_t s_=(accv); if(SHFT&1) s_=(int32_t)((int64_t)s_*2); if(SHFT&2){ s_=sx24(s_); } else { if(s_>0x7FFFFF) s_=0x7FFFFF; if(s_<-0x800000) s_=-0x800000; } SHIFTED=s_; }
        if(dsp_ord==0){ DO_SHIFT(ACC); }
        else { int64_t m=(((int64_t)X*(int64_t)Y)>>12)+B; ACC=(int32_t)((m<<38)>>38); DO_SHIFT(ACC); }
        if(TWT) d_temp[(TWA+d_dec)&0x7F]=SHIFTED;
        if(FRCL){ if(SHFT==3) d_frc=SHIFTED&0xFFF; else d_frc=(SHIFTED>>11)&0x1FFF; }
        if(dsp_ord==0){ int64_t m=(((int64_t)X*(int64_t)Y)>>12)+B; ACC=(int32_t)((m<<38)>>38); }
        if(MRD||MWT){
            uint32_t ADDR=r16(0x3200+MASA*4);
            if(ADREB) ADDR+=d_adrs&0xFFF;
            if(NXADR) ADDR++;
            if(!TABLE){ ADDR+=d_dec; ADDR&=rbl-1; } else ADDR&=0xFFFF;
            ADDR=(ADDR+rbp)&0xFFFFF;
            if((step&1)){
                if(MRD){ uint16_t v; memcpy(&v,aram+(ADDR<<1),2); d_memval[(step+2)&3]=NOFL?((int32_t)(int16_t)v<<8):dsp_unpack(v); }
                if(MWT){ uint16_t v=NOFL?(uint16_t)(SHIFTED>>8):dsp_pack(SHIFTED); memcpy(aram+(ADDR<<1),&v,2); } }
        }
        if(ADRL){ if(SHFT==3) d_adrs=(SHIFTED>>12)&0xFFF; else d_adrs=(INPUTS>>16); }
        if(EWT) d_efreg[EWA]=SHIFTED>>8;
    }
    d_acc=ACC; d_dec=(d_dec-1)&0xFFFF; dsp_calls++; for(int e=0;e<16;e++){ int v=d_efreg[e]<0?-d_efreg[e]:d_efreg[e]; if(v>dsp_maxef) dsp_maxef=v; }
}
static float mvol_gain=1.f;
static void mix_sample(int32_t *L,int32_t *R){
    int32_t sl=0,sr=0;
    for(int i=0;i<64;i++){ Chan *c=&ch[i]; if(!c->active) continue;
        uint32_t r10=cr(i,0x10), r14=cr(i,0x14), r18=cr(i,0x18), r24=cr(i,0x24), r28=cr(i,0x28);
        /* envelope */
        int ar=r10&31, d1r=(r10>>6)&31, d2r=(r10>>11)&31, rr=r14&31, dl=(r14>>5)&31;
        switch(c->st){
            case 0: if(ar==0){ } else { c->eg-=att_tab[ar]; if(c->eg<=0){ c->eg=0; c->st=1; } } break;
            case 1: { int32_t th=(int32_t)(dl<<8)<<8; if(c->eg>=th) c->st=2; else { c->eg+=inc_tab[d1r]; if(c->eg>=th) c->st=2; } break; }
            case 2: c->eg+=inc_tab[d2r]; break;
            default: c->eg+=inc_tab[rr?rr:1]; if(rr==0) c->eg+=inc_tab[1]; }
        if(c->eg>=(0x1FFF<<8)){ c->eg=0x1FFF<<8; if(c->st==3||c->st==2){ if(c->st==3){ c->active=0; continue; } } }
        int e13=c->eg>>8; if(e13>=0x1F00 && c->st!=0){ if(c->st==3) { c->active=0; continue; } }
        /* interpolacao linear */
        int32_t smp=c->s0+(int32_t)(((int64_t)(c->s1-c->s0)*(int64_t)c->frac)>>16);
        float g=gain_eg[e13>>2]*gain_tl[(r28>>8)&255]; if(c->mute) g=0.f;
        int disdl=(r24>>8)&15, pan=r24&31; float gd=gain_dis[disdl]*g;
        float gl=gd, gr=gd; int pv=gain_pan[pan&15]>0||(pan&15)==0?pan&15:15;
        if(pan&0x10) gl*=gain_pan[pv]; else gr*=gain_pan[pv];
        sl+=(int32_t)(smp*gl); sr+=(int32_t)(smp*gr);
        { uint32_t r20=cr(i,0x20); int imxl=(r20>>4)&15; if(imxl&&dsp_on){ dsp_sends++; d_mixs[r20&15]+=(int32_t)(smp*g*gain_dis[imxl])<<4; } }
        /* passo de leitura */
        int oct=(r18>>11)&15; if(oct>=8) oct-=16; uint32_t fns=r18&0x3FF; uint32_t step=(1024+fns)<<6;   /* 16 bits de fracao: (1024+fns)/1024*65536 */
        if(oct>=0) step<<=oct; else step>>=-oct;
        c->frac+=step; while(c->frac>=65536u){ c->frac-=65536u; chan_advance(i,c); if(!c->active) break; }
    }
    static int wet_only=-1; if(wet_only<0) wet_only=GENV("CT2_DSPONLY")!=0; if(wet_only){ sl=0; sr=0; }
    if(dsp_on||dsp_dirty){ dsp_run();
        static float dcx[16],dcy[16];
        for(int e=0;e<16;e++){ float xin=(float)d_efreg[e]; float yv=xin-dcx[e]+0.9995f*dcy[e]; dcx[e]=xin; dcy[e]=yv; uint32_t o=r16(0x2000+e*4); int sd=(o>>8)&15, pn=o&31; float ge=gain_dis[sd]; if(ge==0) continue; float gl=ge, gr=ge; int pv=pn&15; if(pn&0x10) gl*=gain_pan[pv]; else gr*=gain_pan[pv]; sl+=(int32_t)(yv*gl); sr+=(int32_t)(yv*gr); }
        memset(d_mixs,0,sizeof d_mixs); }
    { float fl=sl*mvol_gain, fr=sr*mvol_gain; const float TH=22000.f, RG=32767.f-22000.f;
      if(fl>TH) fl=TH+RG*tanhf((fl-TH)/RG); else if(fl<-TH) fl=-TH-RG*tanhf((-fl-TH)/RG);
      if(fr>TH) fr=TH+RG*tanhf((fr-TH)/RG); else if(fr<-TH) fr=-TH-RG*tanhf((-fr-TH)/RG);
      *L=(int32_t)fl; *R=(int32_t)fr; }
    { static long dumpat=-2; if(dumpat==-2){ const char*e=GENV("CT2_CHDUMP"); dumpat=e?atol(e):-1; } if(dumpat>0 && samples_total%(unsigned)dumpat==0 && samples_total>0){ int na=0; for(int i=0;i<64;i++) if(ch[i].active) na++; fprintf(stderr,"CHDUMP t=%u ativos=%d:",samples_total,na); for(int i=0;i<64;i++) if(ch[i].active) fprintf(stderr," [%d r00=%04x r18=%04x r24=%04x r20=%04x]",i,cr(i,0),cr(i,0x18),cr(i,0x24),cr(i,0x20)); fprintf(stderr,"\n"); } }
}

/* ---------------- registradores comuns, timers e interrupcoes ---------------- */
static uint32_t timer_cnt[3];
static int sh4_irq_pending=0;
static int arm_fiq_line=0; static uint32_t intreq_L=0;
static void update_irqs(void){
    uint32_t scipd=r16(0x28A0), scieb=r16(0x289C), p=scipd&scieb;
    uint32_t L=0; if(p){ int i; for(i=10;i>=0;i--) if(p&(1u<<i)) break; if(i>=0) L=((r16(0x28A8)>>i)&1)|(((r16(0x28AC)>>i)&1)<<1)|(((r16(0x28B0)>>i)&1)<<2); }
    intreq_L=L; arm_fiq_line=L!=0;
    uint32_t m=r16(0x28B8)&r16(0x28B4); sh4_irq_pending=m!=0;
}
static void write_common(uint32_t o,uint32_t v,int size,int from_arm){
    (void)from_arm;
    if(o==0x28A0||o==0x28A1){ if(size==1&&o==0x28A1) return; if(v&0x20) w16(0x28A0,r16(0x28A0)|0x20); update_irqs(); return; }   /* SCIPD: so o bit 5 (SH4 -> ARM) e gravavel */
    if(o==0x28A4){ w16(0x28A0,r16(0x28A0)&~(v&0xFFFF)); update_irqs(); return; }        /* SCIRE */
    if(o==0x28B8){ w16(0x28B8,r16(0x28B8)|(v&0xFFFF)); n_mcipd++; update_irqs(); return; }/* MCIPD: ARM -> SH4 */
    if(o==0x28BC){ w16(0x28B8,r16(0x28B8)&~(v&0xFFFF)); update_irqs(); return; }        /* MCIRE */
    if(o==0x2D04){ return; }
    if(o==0x2C00){ int hold=v&1; if(hold){ arm_run=0; } else if(!arm_run){ arm_reset(); arm_run=1; } memcpy(rg+o,&v,size>4?4:size); return; }
    if(o==0x2800){ mvol_gain=powf(10.f,-((15-(v&15))*3.f)/20.f); if((v&15)==0) mvol_gain=0; }
    if(o==0x2890||o==0x2894||o==0x2898){ int t=(o-0x2890)>>2; timer_cnt[t]=v&255; }
}
static uint32_t aica_rd_reg(uint32_t off,int size){
    off&=0xFFFF; uint32_t v=0;
    if(off>=0x2890&&off<=0x2899&&(off&3)<2){ int t=(off-0x2890)>>2; uint32_t reg=(r16(off&~3u)&~255u)|(timer_cnt[t]&255); if(size==4||size==2) return reg; return (off&1)?reg>>8:reg&255; }
    if(off==0x2D00) return intreq_L;
    if(off==0x2810||off==0x2814){ int sl=(r16(0x280C)>>8)&63; Chan *c=&ch[sl]; { static int ed=-1; static int nd; if(ed<0) ed=GENV("CT2_EGDBG")!=0; if(ed&&sl==6&&samples_total>1792000&&samples_total<1792040&&nd<30){ nd++; fprintf(stderr,"[eg] t=%u off=%x eg=%x st=%d idx=%x\n",samples_total,off,(unsigned)c->eg,c->st,(unsigned)c->idx);} } uint32_t r=off==0x2810?(((c->eg>>8)&0x1FFF)|((c->st&3)<<13)):(c->idx&0xFFFF); return size==1?(r&255):r; }
    if(off==0x2808){ return 0x0000; }
    if(size==4) memcpy(&v,rg+off,4); else if(size==2){ uint16_t h; memcpy(&h,rg+off,2); v=h; } else v=rg[off];
    return v;
}
static void aica_wr_reg(uint32_t off,uint32_t v,int size,int from_arm){
    off&=0xFFFF; if(size==4) memcpy(rg+off,&v,4); else if(size==2){ uint16_t h=(uint16_t)v; memcpy(rg+off,&h,2); } else rg[off]=(uint8_t)v;
    if(off>=0x3400&&off<0x3C00) dsp_dirty=1;
    if(off<0x2000){ if((off&0x7F)<4 && size>=2||(off&0x7F)<2){ if(((off&0x7F)==0||(off&0x7F)==1) && (r16(off&~3u)&0x8000)) { w16(off&~3u,r16(off&~3u)&~0x8000); kyonex(); } } return; }
    if(off>=0x2800 && off<0x2E00) write_common(off,v,size,from_arm);
}

/* ---------------- interface publica ---------------- */
void aica_init(uint8_t *ram2mb){ aram=ram2mb; memset(rg,0,sizeof rg); memset(ch,0,sizeof ch); init_tabs(); arm_run=0; mvol_gain=1.f; { const char*e=GENV("CT2_DSPORD"); if(e) dsp_ord=atoi(e); if(GENV("CT2_NODSP")) dsp_off=1; { FILE*ff=fopen("sem_reverb.txt","rb"); if(ff){ fclose(ff); dsp_off=1; } } } { const char*e=GENV("CT2_AICATRACE"); aica_trace=e?atoi(e):0; } }
uint32_t aica_read(uint32_t off,int size){ LOCK(); uint32_t v=aica_rd_reg(off,size); UNLOCK(); return v; }
void aica_write(uint32_t off,uint32_t v,int size){ LOCK(); aica_wr_reg(off,v,size,0); UNLOCK(); }
int aica_sh4_irq(void){ return sh4_irq_pending; }
int aica_arm_running(void){ return arm_run; }
static unsigned ipspl=256;
#ifndef _WIN32
#include <time.h>
double aica_prof_s=0;
#endif
static void aica_render_chunk(int16_t *out,int n);
void aica_render(int16_t *out,int n){
    /* v15n: blocos de 64 amostras: o spinlock era segurado ~5 ms por bloco de 1024 e travava o SH4 em cada escrita no AICA (atraso de controle) */
    while(n>0){ int c=n>64?64:n; aica_render_chunk(out,c); out+=2*c; n-=c; }
}
static void aica_render_chunk(int16_t *out,int n){
#ifndef _WIN32
    struct timespec t0,t1; clock_gettime(CLOCK_MONOTONIC,&t0);
#endif
    LOCK();
    static int inited=0; if(!inited){ inited=1; const char*e=GENV("CT2_ARMIPS"); if(e) ipspl=(unsigned)atoi(e); }
    for(int s=0;s<n;s++){
        samples_total++;
        /* timers */
        for(int t=0;t<3;t++){ uint32_t ctl=(r16(0x2890+4*t)>>8)&7; if((samples_total&((1u<<ctl)-1))==0){ timer_cnt[t]=(timer_cnt[t]+1)&255; if(timer_cnt[t]==0){ w16(0x28A0,r16(0x28A0)|(0x40u<<t)); w16(0x28B8,r16(0x28B8)|(0x40u<<t)); } } }
        if(r16(0x289C)&0x400) w16(0x28A0,r16(0x28A0)|0x400);
        update_irqs();
        if(arm_run){
            for(unsigned k=0;k<ipspl;k++){
                if(arm_fiq_line && !A.F){ take_exception(0x11,0x1C,A.R[15]+4,1); n_fiq++; }
                arm_step();
                if((k&63)==63) update_irqs();
            }
        }
        int32_t L,R; mix_sample(&L,&R);
        if(L>32767) L=32767; if(L<-32768) L=-32768; if(R>32767) R=32767; if(R<-32768) R=-32768;
        out[2*s]=(int16_t)L; out[2*s+1]=(int16_t)R;
    }
    UNLOCK();
    for(volatile int sp=0;sp<300;sp++){}   /* da chance ao outro thread de pegar o lock */
#ifndef _WIN32
    clock_gettime(CLOCK_MONOTONIC,&t1); aica_prof_s+=(t1.tv_sec-t0.tv_sec)+(t1.tv_nsec-t0.tv_nsec)*1e-9; { static unsigned long long ns=0; ns+=n; if(GENV("CT2_AUDPROF")&&ns>=441000){ fprintf(stderr,"AUDPROF %llu amostras em %.3f s de CPU\n",ns,aica_prof_s); ns=0; } }
#endif
}
void aica_dump_dsp(void){ int n=0,last=-1; for(int i=0;i<128;i++){ uint32_t w[4]; int nz=0; for(int k=0;k<4;k++){ w[k]=r16(0x3400+i*16+k*4); if(w[k]) nz=1; } if(nz){ n++; last=i; } } int nc=0; for(int i=0;i<128;i++) if(r16(0x3000+i*4)) nc++; int nm=0; for(int i=0;i<64;i++) if(r16(0x3200+i*4)) nm++; int ef=0; for(int i=0;i<16;i++) if((r16(0x2000+i*4)>>8)&15) ef++; int im=0; for(int c=0;c<64;c++) if((r16(c*0x80+0x20)>>4)&15) im++; { int fl=0,mem=0; for(int i=0;i<128;i++){ uint32_t w2=r16(0x3400+i*16+8), w3=r16(0x3400+i*16+12); if((w2>>14&1)||(w2>>13&1)){ mem++; if(!(w3>>15&1)) fl++; } } printf("DSP mem-ops=%d (com float=%d)\n",mem,fl); } printf("DSP: passos nao-nulos=%d (ultimo %d), coefs=%d, madrs=%d, saidas EFSDL ativas=%d, canais com IMXL>0=%d, RBL/RBP=%04x\n",n,last,nc,nm,ef,im,r16(0x2804)); }

void aica_dsp_selftest(void){
    static uint8_t save[0x200000]; memcpy(save,aram,sizeof save);
    for(int ord=0;ord<2;ord++){
        dsp_ord=ord; memset(d_temp,0,sizeof d_temp); memset(d_mems,0,sizeof d_mems); memset(d_mixs,0,sizeof d_mixs); memset(d_efreg,0,sizeof d_efreg); d_acc=d_frc=d_yreg=d_adrs=0; memset(d_memval,0,sizeof d_memval); d_dec=0;
        uint32_t rbl=0x2000u<<((r16(0x2804)>>13)&3), rbp=(uint32_t)(r16(0x2804)&0xFFF)<<10; for(uint32_t q=0;q<rbl&&(rbp+q)*2<0x200000;q++){ uint16_t z=0x6000; memcpy(aram+(rbp+q)*2,&z,2); }
        dsp_dirty=1; printf("DSPTEST ordem %d:",ord);
        double eacc=0; int nwin=0;
        for(int s=0;s<44100*3;s++){
            if(s==0) for(int k=0;k<16;k++) d_mixs[k]=8000<<4;
            dsp_run(); memset(d_mixs,0,sizeof d_mixs);
            if(ord==0&&(s==3||s==500||s==8000||s==30000||s==60000||s==130000)){ printf("\n  s=%d ef:",s); for(int e=0;e<16;e++) printf(" %d",d_efreg[e]); printf(" | mems0-9:"); for(int e=0;e<10;e++) printf(" %d",d_mems[e]); printf(" | temp108-127:"); for(int e=108;e<128;e+=2) printf(" %d",d_temp[(e+d_dec)&127]); printf(" acc=%d",d_acc); }
            for(int e=0;e<16;e++) eacc+=(double)d_efreg[e]*d_efreg[e];
            if((s+1)%11025==0){ printf(" %.0f",sqrt(eacc/(11025.0*16))); eacc=0; nwin++; }
        }
        printf("\n");
    }
    memcpy(aram,save,sizeof save); dsp_ord=0;
}
void aica_stats(void){ printf("DSPSTAT maxmix=%d calls=%llu maxef=%d sends=%llu on=%d off=%d\n",dsp_maxmix,dsp_calls,dsp_maxef,dsp_sends,dsp_on,dsp_off); if(GENV("CT2_DSPDUMP")){ FILE*f=fopen(GENV("CT2_DSPDUMP"),"w"); for(int i=0;i<128;i++) fprintf(f,"%d %04x %04x %04x %04x coef=%04x\n",i,r16(0x3400+i*16),r16(0x3400+i*16+4),r16(0x3400+i*16+8),r16(0x3400+i*16+12),r16(0x3000+i*4)); for(int i=0;i<64;i++) fprintf(f,"madrs %d %04x\n",i,r16(0x3200+i*4)); for(int c=0;c<64;c++) fprintf(f,"ch %d r20=%04x\n",c,r16(c*0x80+0x20)); for(int i=0;i<18;i++) fprintf(f,"out %d %04x\n",i,r16(0x2000+i*4)); fclose(f); } if(GENV("CT2_DSPTEST")) aica_dsp_selftest(); aica_dump_dsp(); printf("AICA: ARM rodando=%d, instrucoes=%llu, FIQs=%llu, key-ons=%llu, MCIPD=%llu, PC=%08x\n",arm_run,arm_instr,n_fiq,n_ksc,n_mcipd,A.R[15]); printf("ARMREG r0=%08x r8=%08x r9=%08x r10=%08x [r10]=%02x [r8]=%08x\n",A.R[0],A.R[8],A.R[9],A.R[10],mrd(A.R[10],1),mrd(A.R[8],4)); }
