/* Runtime minimo para o codigo SH-4 recompilado. Modelo: CPU em uma struct, RAM plana de 16 MB
   em 0x0C000000 (e espelhos). Acessos fora da RAM vao para rt_hw_* (a ser ligado ao hardware emulado). */
#ifndef SH4RT_H
#define SH4RT_H
#include <stdint.h>
#include <string.h>
#include <math.h>
#include <setjmp.h>
#define RT_STK 512
extern int ct2_cheat_unlock;   /* 1 = desbloqueia tudo (escreve 0xFF em 0x8C28CE98 a cada quadro) */
extern int ct2_cheat_notimer;   /* 1 = cronometro nao desce (patch NOP em 0x8C01DD40) */
int ct2_hop_orig(void);   /* v15zr: CT2_HOPMAX=1 forca o salto maximo mesmo parado */
#define RAM_SIZE (16u*1024*1024)
#define FPSCR_SZ (1u<<20)
#define FPSCR_PR (1u<<19)
#define FPSCR_FR (1u<<21)
typedef struct CPU {
    uint32_t r[16], pr, mach, macl, fpul, fpscr, gbr, vbr;
    uint8_t  t, m, q;
    uint32_t sr_other;                   /* SR sem T/Q/M (mascara de interrupcao, MD/RB/BL ... apenas guardados) */
    float    fr[16], xf[16];
    uint8_t *ram;
    uint64_t insn_unsupported;
    uint32_t stk[RT_STK]; int sp;          /* pilha de chamadas sombra (depuracao) */
} CPU;
extern jmp_buf *rt_fault_jmp;
extern char rt_fail_msg[200]; extern uint32_t rt_fail_addr;
typedef uint32_t (*RtHwRead)(struct CPU*,uint32_t,int);
typedef void (*RtHwWrite)(struct CPU*,uint32_t,uint32_t,int);
extern RtHwRead rt_hw_read_cb; extern RtHwWrite rt_hw_write_cb;     /* hardware emulado (opcional) */
static inline void rt_enter(struct CPU*c,uint32_t a);
extern void (*rt_tick_cb)(struct CPU*); extern unsigned rt_tick_n;   /* relogio do hardware emulado: chamado a cada 2048 entradas de funcao e a cada escrita em SR */
static inline void rt_leave(struct CPU*c);           /* se != NULL, falhas viram longjmp (usado nos testes) */
void rt_call(CPU *c, uint32_t addr);
typedef void (*RtNative)(CPU *c);
void rt_native_register(uint32_t addr, RtNative f);   /* funcao implementada em C (ex.: chamadas do BIOS) */
int  rt_native_call(CPU *c, uint32_t addr);           /* 1 se havia uma */
void rt_fail(CPU *c, const char *what, uint32_t a);
void rt_missing(CPU *c, uint32_t addr);
void rt_unsupported(CPU *c, uint32_t pc, uint32_t op);
uint32_t rt_hw_r(CPU *c, uint32_t a, int size);
void     rt_hw_w(CPU *c, uint32_t a, uint32_t v, int size);
void rt_pref(CPU *c, uint32_t a);
extern void (*rt_pref_cb)(CPU*,uint32_t);
void rt_set_fpscr(CPU *c, uint32_t v);
void rt_frchg(CPU *c);
extern uint32_t ct2_edge_acc[8]; uint32_t ct2_edge_step(unsigned p,uint32_t e); uint32_t ct2_lib_edge(unsigned p,uint32_t e); void ct2_lib_take(unsigned p);
static inline uint32_t rt_get_sr(CPU *c){ return c->sr_other | c->t | ((uint32_t)c->q<<8) | ((uint32_t)c->m<<9); }
static inline void rt_set_sr(CPU *c, uint32_t v){ c->t=v&1; c->q=(v>>8)&1; c->m=(v>>9)&1; c->sr_other=v&~0x301u; if(rt_tick_cb) rt_tick_n+=256u; }
static inline void rt_tick(CPU*c){ if(rt_tick_cb && ++rt_tick_n>=2048u){ rt_tick_n=0; rt_tick_cb(c); } }
extern int rt_rng_on; extern void rt_rng_hit(struct CPU*c,uint32_t a);
extern int rt_watch_n; extern uint32_t rt_watch[16]; extern void rt_watch_hit(struct CPU*c,uint32_t a,int i);
#ifdef FNTRACE
extern int rt_trace_on; extern unsigned char rt_trace_map[0x80000];
#endif
static inline void rt_enter(CPU*c,uint32_t a){
#ifdef FNTRACE
 if(rt_trace_on){ uint32_t o=((a&0x1FFFFFFFu)-0x0C010000u)>>1; if(o<0x80000) rt_trace_map[o]=1; { extern uint32_t rt_trace_stk; if((a&0x1FFFFFFFu)==rt_trace_stk){ static int n=0; if(n++<3){ printf("[stk] %08x <-",a); for(int q=c->sp-1;q>=0&&q>c->sp-14;q--) printf(" %08x",c->stk[q]&0x1FFFFFFFu); printf("\n"); fflush(stdout);} } } }
#endif
 if(rt_watch_n){ for(int i=0;i<rt_watch_n;i++) if((a&0x1FFFFFFFu)==rt_watch[i]) rt_watch_hit(c,a,i); if(rt_rng_on) rt_rng_hit(c,a); } rt_tick(c); if(c->sp>=0&&c->sp<RT_STK) c->stk[c->sp]=a; c->sp++; }
static inline void rt_leave(CPU*c){ c->sp--; }
static inline uint8_t *rt_ptr(CPU *c, uint32_t a) {
    a &= 0x1FFFFFFFu;
    if (a >= 0x0C000000u && a < 0x10000000u) return c->ram + (a & (RAM_SIZE-1));
    return 0;
}
#ifdef RDWATCH
extern uint32_t rt_rd_lo, rt_rd_hi; extern void rt_rd_hit(CPU*,uint32_t,int);
#define RDCHK(c,a,w) do{ uint32_t _a=(a)&0x1FFFFFFFu; if(_a>=rt_rd_lo&&_a<rt_rd_hi) rt_rd_hit(c,_a,w); }while(0)
#else
#define RDCHK(c,a,w) do{}while(0)
#endif
static inline uint32_t rt_r8(CPU *c, uint32_t a){ RDCHK(c,a,1); uint8_t *p=rt_ptr(c,a); return p? (uint32_t)(int32_t)(int8_t)*p : (uint32_t)(int32_t)(int8_t)rt_hw_r(c,a,1); }
static inline uint32_t rt_r16(CPU *c, uint32_t a){ RDCHK(c,a,2); uint8_t *p=rt_ptr(c,a); if(!p) return (uint32_t)(int32_t)(int16_t)rt_hw_r(c,a,2); int16_t v; memcpy(&v,p,2); return (uint32_t)(int32_t)v; }
static inline uint32_t rt_r32(CPU *c, uint32_t a){ RDCHK(c,a,4); uint8_t *p=rt_ptr(c,a); if(!p) return rt_hw_r(c,a,4); uint32_t v; memcpy(&v,p,4); return v; }
static inline void rt_w8(CPU *c, uint32_t a, uint32_t v){ uint8_t *p=rt_ptr(c,a); if(p) *p=(uint8_t)v; else rt_hw_w(c,a,v,1); }
static inline void rt_w16(CPU *c, uint32_t a, uint32_t v){ uint8_t *p=rt_ptr(c,a); if(p){uint16_t x=(uint16_t)v; memcpy(p,&x,2);} else rt_hw_w(c,a,v,2); }
#ifdef FNTRACE
extern uint32_t rt_mw_a; extern void rt_mw_hit(CPU*,uint32_t,uint32_t);
#endif
extern int rt_hop_on; extern uint32_t rt_hop_w[4]; extern void rt_hop_hit(CPU*,uint32_t,uint32_t);   /* v15zq: vigia de escrita ativo so durante a gravacao do salto (F10) */
static inline void rt_w32(CPU *c, uint32_t a, uint32_t v){
 if(rt_hop_on){ uint32_t k_=a&0x1FFFFFFCu; if(k_==rt_hop_w[0]||k_==rt_hop_w[1]||k_==rt_hop_w[2]||k_==rt_hop_w[3]) rt_hop_hit(c,a,v); }
#ifdef FNTRACE
 if((a&0x1FFFFFFCu)==rt_mw_a) rt_mw_hit(c,a,v);
#endif
 uint8_t *p=rt_ptr(c,a); if(p) memcpy(p,&v,4); else rt_hw_w(c,a,v,4); }
static inline float rt_rf(CPU *c, uint32_t a){ uint32_t v=rt_r32(c,a); float f; memcpy(&f,&v,4); return f; }
static inline void  rt_wf(CPU *c, uint32_t a, float f){ uint32_t v; memcpy(&v,&f,4); rt_w32(c,a,v); }
/* pares de 64 bits (SZ=1): n par -> banco FR, n impar -> banco XF; addr+0 -> registrador par, addr+4 -> impar */
static inline float *rt_pair(CPU *c, int n){ return (n&1) ? &c->xf[n&~1] : &c->fr[n&~1]; }
static inline void rt_ld64(CPU *c, int n, uint32_t a){ float *p=rt_pair(c,n); p[0]=rt_rf(c,a); p[1]=rt_rf(c,a+4); }
static inline void rt_st64(CPU *c, int m, uint32_t a){ float *p=rt_pair(c,m); rt_wf(c,a,p[0]); rt_wf(c,a+4,p[1]); }
static inline void rt_mv64(CPU *c, int m, int n){ float *s=rt_pair(c,m),*d=rt_pair(c,n); float a=s[0],b=s[1]; d[0]=a; d[1]=b; }
static inline void rt_ftrv(CPU *c, int n){ float *v=&c->fr[(n&3)*4], o[4]; for(int i=0;i<4;i++) o[i]=c->xf[i]*v[0]+c->xf[4+i]*v[1]+c->xf[8+i]*v[2]+c->xf[12+i]*v[3]; memcpy(v,o,16); }
static inline void rt_fipr(CPU *c, int m, int n){ float *a=&c->fr[m*4],*b=&c->fr[n*4]; b[3]=a[0]*b[0]+a[1]*b[1]+a[2]*b[2]+a[3]*b[3]; }
static inline void rt_fsca(CPU *c, int n){ double a=(double)(c->fpul&0xFFFFu)*(6.283185307179586476925286766559/65536.0); c->fr[n]=(float)sin(a); c->fr[n+1]=(float)cos(a); }
static inline uint32_t rt_ftrc(float f){ if(f!=f) return 0x80000000u; if(f>=2147483648.0f) return 0x7FFFFFFFu; if(f<=-2147483648.0f) return 0x80000000u; return (uint32_t)(int32_t)f; }
#define NEED_SP() do{ if(c->fpscr & FPSCR_PR) rt_fail(c,"FPU em precisao dupla (PR=1)",0); }while(0)
#endif
