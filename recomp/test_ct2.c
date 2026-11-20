/* Testes da recompilacao: oraculo = significado matematico das funcoes.  uso: test_ct2 1ST_READ.BIN [scan_list.txt] */
#include "sh4rt.h"
#include "ct2_funcs.h"
#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#ifdef _WIN32
#define ualarm(a,b) 0
#define HAVE_TIMEOUT 0
#else
#include <unistd.h>
#define HAVE_TIMEOUT 1
#endif
static CPU c; static jmp_buf jb; static int fails=0, checks=0;
#ifndef _WIN32
static void on_alarm(int s){ (void)s; if(rt_fault_jmp) longjmp(*rt_fault_jmp,2); }
#endif
static int run(uint32_t addr){            /* 0 = ok, 1 = falha de memoria/instrucao, 2 = tempo esgotado */
    int r; rt_fault_jmp=&jb; if((r=setjmp(jb))==0){ ualarm(60000,0); rt_call(&c,addr); ualarm(0,0); } rt_fault_jmp=0; ualarm(0,0); return r;
}
static void reset(void){ memset(c.r,0,sizeof c.r); c.r[15]=0x8CFFF000u; c.t=c.m=c.q=0; c.fpscr=0; c.sp=0; c.fpul=0; memset(c.fr,0,sizeof c.fr); memset(c.xf,0,sizeof c.xf); c.pr=0; }
static int near(float a,float b){ float d=fabsf(a-b), m=fmaxf(1.0f,fabsf(b)); return d<=2e-4f*m; }
#define CHECK(name,cond) do{ checks++; if(!(cond)){ fails++; printf("FALHOU  %s\n",name);} else printf("ok      %s\n",name);}while(0)
#define SCR 0x8C800000u
static void putv(uint32_t a,float x,float y,float z){ rt_wf(&c,a,x); rt_wf(&c,a+4,y); rt_wf(&c,a+8,z); }
static void getv(uint32_t a,float*v){ for(int i=0;i<3;i++) v[i]=rt_rf(&c,a+4*i); }
static int call_ok(const char*n,uint32_t a){ int r=run(a); if(r){ printf("FALHOU  %s (erro de execucao %d)\n",n,r); fails++; checks++; } return r==0; }

static void test_vec(void){
    float v[3]; 
    reset(); putv(SCR,1,2,3); putv(SCR+16,10,20,30); c.r[4]=SCR; c.r[5]=SCR+16;
    if(call_ok("vec3_add_inplace",0x0c0741d0)){ getv(SCR,v); CHECK("vec3_add_inplace (1,2,3)+(10,20,30)=(11,22,33)",near(v[0],11)&&near(v[1],22)&&near(v[2],33)); }
    reset(); putv(SCR,1,2,3); putv(SCR+16,10,20,30); c.r[4]=SCR; c.r[5]=SCR+16;
    if(call_ok("vec3_sub_inplace",0x0c074390)){ getv(SCR,v); CHECK("vec3_sub_inplace (1,2,3)-(10,20,30)=(-9,-18,-27)",near(v[0],-9)&&near(v[1],-18)&&near(v[2],-27)); }
    reset(); putv(SCR+16,1,2,3); putv(SCR+32,10,20,30); c.r[4]=SCR; c.r[5]=SCR+32; c.r[6]=SCR+16;
    if(call_ok("vec3_sub",0x0c0743b0)){ getv(SCR,v); CHECK("vec3_sub dst=a-b com a=r5,b=r6: (10,20,30)-(1,2,3)=(9,18,27)",near(v[0],9)&&near(v[1],18)&&near(v[2],27)); }
    reset(); putv(SCR+16,1,-2,3); c.r[4]=SCR; c.r[5]=SCR+16; c.fr[4]=2.5f;
    if(call_ok("vec3_scale",0x0c074340)){ getv(SCR,v); CHECK("vec3_scale (1,-2,3)*2.5=(2.5,-5,7.5)",near(v[0],2.5f)&&near(v[1],-5)&&near(v[2],7.5f)); }
    reset(); putv(SCR,1,2,3); putv(SCR+16,4,-5,6); c.r[4]=SCR; c.r[5]=SCR+16;
    if(call_ok("vec3_dot",0x0c074240)) CHECK("vec3_dot (1,2,3).(4,-5,6)=12",near(c.fr[0],12));
    reset(); putv(SCR,2,3,6); c.r[4]=SCR;
    if(call_ok("vec3_length",0x0c074360)) CHECK("vec3_length (2,3,6)=7",near(c.fr[0],7));
    reset(); putv(SCR,3,4,0); putv(SCR+16,0,0,12); c.r[4]=SCR; c.r[5]=SCR+16;
    if(call_ok("vec3_distance",0x0c074260)) CHECK("vec3_distance (3,4,0)-(0,0,12)=13",near(c.fr[0],13));
    reset(); putv(SCR,0,3,4); c.r[4]=SCR;
    if(call_ok("vec3_normalize",0x0c074510)){ getv(SCR,v); CHECK("vec3_normalize (0,3,4)=(0,0.6,0.8)",near(v[0],0)&&near(v[1],0.6f)&&near(v[2],0.8f)); }
    reset(); c.r[4]=0x4000;
    if(call_ok("sincos_angle16",0x0c072840)) CHECK("sincos_angle16(0x4000) = (sin 90 = 1, cos 90 = 0)",near(c.fr[0],1)&&near(c.fr[1],0));
    reset(); c.r[4]=0x2000;
    if(call_ok("sincos_angle16",0x0c072840)) CHECK("sincos_angle16(0x2000) = (0.7071,0.7071)",near(c.fr[0],0.70710678f)&&near(c.fr[1],0.70710678f));
}
static void xform(uint32_t fn,float x,float y,float z,float*o){ putv(SCR,x,y,z); c.r[4]=SCR; c.r[5]=SCR+16; run(fn); getv(SCR+16,o); }
static void test_mat(void){
    float o[3];
    reset(); if(!call_ok("xmtrx_load_identity",0x0c074180)) return;
    xform(0x0c072930,1,2,3,o); CHECK("identidade: transform_point (1,2,3) -> (1,2,3)",near(o[0],1)&&near(o[1],2)&&near(o[2],3));
    c.fr[4]=1; c.fr[5]=2; c.fr[6]=3; if(!call_ok("xmtrx_translate",0x0c074130)) return;
    xform(0x0c072930,0,0,0,o); CHECK("translate(1,2,3): transform_point (0,0,0) -> (1,2,3)",near(o[0],1)&&near(o[1],2)&&near(o[2],3));
    xform(0x0c072990,5,5,5,o); CHECK("translate(1,2,3): transform_dir (5,5,5) -> (5,5,5) (direcao ignora translacao)",near(o[0],5)&&near(o[1],5)&&near(o[2],5));
    c.fr[4]=10; c.fr[5]=20; c.fr[6]=30; call_ok("xmtrx_translate",0x0c074130);
    xform(0x0c072930,0,0,0,o); CHECK("translate acumula: (1,2,3)+(10,20,30) -> (11,22,33)",near(o[0],11)&&near(o[1],22)&&near(o[2],33));
    uint32_t fns[3]={0x0c073d50,0x0c073ba0,0x0c073e10}; const char*nm[3]={"rotate_a 0x0c073d50","rotate_b 0x0c073ba0","rotate_c 0x0c073e10"};
    for(int k=0;k<3;k++){
        reset(); call_ok("xmtrx_load_identity",0x0c074180); c.r[4]=0x4000; if(!call_ok(nm[k],fns[k])) continue;
        float ex[3],ey[3],ez[3]; xform(0x0c072990,1,0,0,ex); xform(0x0c072990,0,1,0,ey); xform(0x0c072990,0,0,1,ez);
        int fixed=-1; if(near(ex[0],1)&&near(ex[1],0)&&near(ex[2],0)) fixed=0; if(near(ey[0],0)&&near(ey[1],1)&&near(ey[2],0)) fixed=1; if(near(ez[0],0)&&near(ez[1],0)&&near(ez[2],1)) fixed=2;
        float det=ex[0]*(ey[1]*ez[2]-ey[2]*ez[1])-ex[1]*(ey[0]*ez[2]-ey[2]*ez[0])+ex[2]*(ey[0]*ez[1]-ey[1]*ez[0]);
        int orth=near(ex[0]*ex[0]+ex[1]*ex[1]+ex[2]*ex[2],1)&&near(ey[0]*ey[0]+ey[1]*ey[1]+ey[2]*ey[2],1)&&near(ez[0]*ez[0]+ez[1]*ez[1]+ez[2]*ez[2],1);
        printf("        %s: X->(%.2f,%.2f,%.2f) Y->(%.2f,%.2f,%.2f) Z->(%.2f,%.2f,%.2f) det=%.2f eixo fixo=%c\n",nm[k],ex[0],ex[1],ex[2],ey[0],ey[1],ey[2],ez[0],ez[1],ez[2],det,"XYZ?"[fixed<0?3:fixed]);
        char b[160]; snprintf(b,sizeof b,"%s e rotacao propria de 90 graus (base ortonormal, det=+1, um eixo fixo)",nm[k]); CHECK(b,orth&&near(det,1)&&fixed>=0);
    }
}
/* ---- identificacao automatica de funcoes inteiras/float por hipotese ---- */
static uint32_t rnd(void){ static uint64_t s=88172645463325252ull; s^=s<<13; s^=s>>7; s^=s<<17; return (uint32_t)(s>>16); }
static uint32_t pick(void){ switch(rnd()%4){ case 0: return rnd()%1000; case 1: return (uint32_t)-(int32_t)(rnd()%1000); case 2: return rnd(); default: return rnd()%100000; } }
typedef struct { const char*n; int (*f)(uint32_t,uint32_t,uint32_t*); } IH;
#define IHF(nm,expr,cond) static int nm(uint32_t a,uint32_t b,uint32_t*o){ (void)a;(void)b; if(!(cond)) return 0; *o=(expr); return 1; }
IHF(h_sdiv,(uint32_t)((int32_t)a/(int32_t)b),b!=0&&!(a==0x80000000u&&b==0xFFFFFFFFu))
IHF(h_udiv,a/b,b!=0)
IHF(h_smod,(uint32_t)((int32_t)a%(int32_t)b),b!=0&&!(a==0x80000000u&&b==0xFFFFFFFFu))
IHF(h_umod,a%b,b!=0)
IHF(h_sdiv_r,(uint32_t)((int32_t)b/(int32_t)a),a!=0&&!(b==0x80000000u&&a==0xFFFFFFFFu))
IHF(h_udiv_r,b/a,a!=0)
IHF(h_mul,a*b,1) IHF(h_abs,((int32_t)a<0)?0u-a:a,1) IHF(h_add,a+b,1) IHF(h_sub,a-b,1) IHF(h_neg,0u-a,1)
IHF(h_mins,((int32_t)a<(int32_t)b)?a:b,1) IHF(h_maxs,((int32_t)a>(int32_t)b)?a:b,1) IHF(h_minu,(a<b)?a:b,1) IHF(h_maxu,(a>b)?a:b,1)
IHF(h_sign,((int32_t)a>0)-((int32_t)a<0),1) IHF(h_not,~a,1) IHF(h_and,a&b,1) IHF(h_or,a|b,1) IHF(h_xor,a^b,1)
IHF(h_swap16,(a<<16)|(a>>16),1) IHF(h_bswap,__builtin_bswap32(a),1) IHF(h_clz,a?(uint32_t)__builtin_clz(a):32u,1)
IHF(h_isqrt,(uint32_t)sqrt((double)a),1)
static IH ih[]={{"sdiv(a/b)",h_sdiv},{"udiv(a/b)",h_udiv},{"smod(a%b)",h_smod},{"umod(a%b)",h_umod},{"sdiv(b/a)",h_sdiv_r},{"udiv(b/a)",h_udiv_r},{"mul(a*b)",h_mul},
 {"abs(a)",h_abs},{"add(a+b)",h_add},{"sub(a-b)",h_sub},{"neg(a)",h_neg},{"min_s",h_mins},{"max_s",h_maxs},{"min_u",h_minu},{"max_u",h_maxu},{"sign(a)",h_sign},{"not(a)",h_not},
 {"and",h_and},{"or",h_or},{"xor",h_xor},{"swap16(a)",h_swap16},{"bswap32(a)",h_bswap},{"clz(a)",h_clz},{"isqrt(a)",h_isqrt}};
typedef struct { const char*n; int (*f)(float,float,float*); } FH;
#define FHF(nm,expr) static int nm(float a,float b,float*o){ (void)a;(void)b; *o=(expr); return 1; }
FHF(f_abs,fabsf(a)) FHF(f_neg,-a) FHF(f_sq,a*a) FHF(f_add,a+b) FHF(f_sub,a-b) FHF(f_mul,a*b) FHF(f_min,fminf(a,b)) FHF(f_max,fmaxf(a,b))
static int f_sqrt(float a,float b,float*o){ (void)b; if(a<0) return 0; *o=sqrtf(a); return 1; }
static int f_rcp(float a,float b,float*o){ (void)b; if(a==0) return 0; *o=1.0f/a; return 1; }
static int f_div(float a,float b,float*o){ if(b==0) return 0; *o=a/b; return 1; }
static int f_rsqrt(float a,float b,float*o){ (void)b; if(a<=0) return 0; *o=1.0f/sqrtf(a); return 1; }
static int f_lerp(float a,float b,float*o){ *o=a+(b-a)*0.5f; return 1; }
static int f_clamp01(float a,float b,float*o){ (void)b; *o=a<0?0:(a>1?1:a); return 1; }
static FH fh[]={{"fabs(fr4)",f_abs},{"-fr4",f_neg},{"fr4*fr4",f_sq},{"fr4+fr5",f_add},{"fr4-fr5",f_sub},{"fr4*fr5",f_mul},{"fmin",f_min},{"fmax",f_max},{"sqrt(fr4)",f_sqrt},{"1/fr4",f_rcp},{"fr4/fr5",f_div},{"1/sqrt(fr4)",f_rsqrt},{"mid(fr4,fr5)",f_lerp},{"clamp01(fr4)",f_clamp01}};
static float pickf(void){ switch(rnd()%3){ case 0: return (float)(int32_t)(rnd()%2000)/10.0f-100.0f; case 1: return (float)(rnd()%1000)/1000.0f; default: return (float)(int32_t)(rnd()%200000)/100.0f-1000.0f; } }
static const struct { int ia, ib, out; } combos[]={{4,5,0},{1,0,1},{1,0,0},{0,1,0},{0,1,1},{4,5,1}};
static void scan(const char*list){
    if(!HAVE_TIMEOUT){ printf("scan: requer POSIX (usa temporizador); pulado\n"); return; }
    FILE*f=fopen(list,"r"); if(!f) return; unsigned a; int found=0,tested=0; int nI=sizeof ih/sizeof ih[0], nF=sizeof fh/sizeof fh[0];
    while(fscanf(f,"%x",&a)==1){
        tested++; int hit=0;
        for(unsigned cb=0; cb<sizeof combos/sizeof combos[0] && !hit; cb++){
            int pi[64]={0}, pf[64]={0}, ni[64]={0}, nf[64]={0}, ok=0, bad=0;
            for(int t=0;t<120;t++){
                uint32_t x=pick(), y=pick(); float fx=pickf(), fy=pickf();
                reset(); c.r[combos[cb].ia]=x; c.r[combos[cb].ib]=y; c.fr[4]=fx; c.fr[5]=fy;
                { int rr=run(a); if(rr==2){ ok=0; break; } if(rr){ if(++bad>=25 && ok==0) break; continue; } } ok++;
                uint32_t rv=c.r[combos[cb].out]; float f0=c.fr[0];
                for(int h=0;h<nI;h++){ uint32_t o; if(ih[h].f(x,y,&o)){ ni[h]++; if(o==rv) pi[h]++; } }
                if(cb==0) for(int h=0;h<nF;h++){ float o; if(fh[h].f(fx,fy,&o)){ nf[h]++; if(near(f0,o)&&f0==f0) pf[h]++; } }
            }
            if(ok<60) continue;
            for(int h=0;h<nI && !hit;h++) if(ni[h]>=40 && pi[h]==ni[h]) { printf("ID %08x int   %-14s args=(r%d,r%d) resultado=r%d\n",a,ih[h].n,combos[cb].ia,combos[cb].ib,combos[cb].out); found++; hit=1; }
            if(cb==0) for(int h=0;h<nF && !hit;h++) if(nf[h]>=40 && pf[h]==nf[h]) { printf("ID %08x float %-14s args=(fr4,fr5) resultado=fr0\n",a,fh[h].n); found++; hit=1; }
        }
    }
    fclose(f); printf("scan: %d funcoes testadas, %d identificadas\n",tested,found);
}
int main(int argc,char**argv){
    if(argc<2){ fprintf(stderr,"uso: %s 1ST_READ.BIN [scan_list.txt]\n",argv[0]); return 2; }
    c.ram=calloc(1,RAM_SIZE); FILE*f=fopen(argv[1],"rb"); if(!f){ perror("1ST_READ.BIN"); return 2; }
    size_t n=fread(c.ram+0x10000,1,RAM_SIZE-0x10000,f); fclose(f); printf("imagem: %zu bytes em 0x8C010000\n",n);
#ifndef _WIN32
    signal(SIGALRM,on_alarm);
#endif
    if(argc<4||strcmp(argv[3],"scan-only")){ test_vec(); test_mat(); printf("\n%d verificacoes, %d falhas\n",checks,fails); }
    if(argc>2) scan(argv[2]);
    return fails?1:0;
}
