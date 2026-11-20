#include "sh4rt.h"
#include <stdio.h>
#include <stdlib.h>
jmp_buf *rt_fault_jmp = 0;
char rt_fail_msg[200]; uint32_t rt_fail_addr;
void (*rt_tick_cb)(struct CPU*) = 0; unsigned rt_tick_n = 0;
RtHwRead rt_hw_read_cb = 0; RtHwWrite rt_hw_write_cb = 0;
static void die(CPU *c, const char *what, uint32_t a){
    snprintf(rt_fail_msg,sizeof rt_fail_msg,"%s",what); rt_fail_addr=a;
    if (rt_fault_jmp) longjmp(*rt_fault_jmp, 1);
    fprintf(stderr, "[sh4rt] %s @0x%08x (r0=%08x r4=%08x r15=%08x)\n", what, a, c->r[0], c->r[4], c->r[15]); abort();
}
void rt_fail(CPU *c, const char *what, uint32_t a){ die(c, what, a); }
void rt_unsupported(CPU *c, uint32_t pc, uint32_t op){ c->insn_unsupported++; char b[64]; snprintf(b,sizeof b,"instrucao nao suportada %04x",op); die(c,b,pc); }
uint32_t rt_hw_r(CPU *c, uint32_t a, int size){ if(rt_hw_read_cb) return rt_hw_read_cb(c,a,size); die(c,"leitura fora da RAM",a); return 0; }
void rt_hw_w(CPU *c, uint32_t a, uint32_t v, int size){ if(rt_hw_write_cb){ rt_hw_write_cb(c,a,v,size); return; } die(c,"escrita fora da RAM",a); }
void (*rt_pref_cb)(CPU*,uint32_t) = 0;
void rt_pref(CPU *c, uint32_t a){ if(rt_pref_cb) rt_pref_cb(c,a); /* pref em 0xE0000000: descarga de store queue -> TA */ }
void rt_frchg(CPU *c){ float t[16]; memcpy(t,c->fr,64); memcpy(c->fr,c->xf,64); memcpy(c->xf,t,64); c->fpscr ^= FPSCR_FR; }
void rt_set_fpscr(CPU *c, uint32_t v){ v &= 0x003FFFFFu; if ((c->fpscr ^ v) & FPSCR_FR) { float t[16]; memcpy(t,c->fr,64); memcpy(c->fr,c->xf,64); memcpy(c->xf,t,64); } c->fpscr = v; }

static struct { uint32_t a; RtNative f; } nat[64]; static int nnat=0;
void rt_native_register(uint32_t addr, RtNative f){ if(nnat<64){ nat[nnat].a=addr&0x1FFFFFFFu; nat[nnat].f=f; nnat++; } }
int rt_native_call(CPU *c, uint32_t addr){ uint32_t a=addr&0x1FFFFFFFu; for(int i=0;i<nnat;i++) if(nat[i].a==a){ nat[i].f(c); return 1; } return 0; }

int ct2_cheat_notimer=0;
int ct2_cheat_unlock=0;

/* v15q: salto para codigo da imagem sem funcao recompilada (ponteiro de funcao calculado em tempo de execucao): em vez de parar o jogo,
   avisa e retorna 0 ao chamador; fora da imagem continua sendo erro fatal. O alvo vai para faltantes.txt (para virar extra_entries.txt). */
void rt_missing(CPU *c, uint32_t addr){
    uint32_t pa=addr&0x1FFFFFFFu;
    if(pa>=0x0C010000u && pa<0x0C172A74u && !(pa&1)){
        static int n=0; if(n++<20){ printf("[aviso] salto para 0x%08x sem funcao recompilada: ignorado (retorna 0)\n",addr); fflush(stdout);
            FILE *f=fopen("faltantes.txt","a"); if(f){ fprintf(f,"%08x\n",pa); fclose(f); } }
        c->r[0]=0; return; }
    rt_fail(c,"salto para endereco sem funcao recompilada",addr);
}

/* ---------------------------------------------------------------------------------------------------------------------
   v15zz: THREADS DA BIBLIOTECA DE BACKUP (VMU). O Sega "bup" usa setjmp/longjmp proprios (0c095ccc salva o contexto inteiro
   num buffer; 0c095c58 restaura) para trocar entre o agendador (chamado pela interrupcao de fim do DMA Maple) e threads com pilha
   propria. Codigo recompilado usa a pilha do hospedeiro, entao um longjmp "de verdade" nao tem como retomar um quadro suspenso.
   Solucao: cada contexto do jogo que faz setjmp e depois sai por longjmp vira uma COROTINA do hospedeiro (ucontext / Fibers):
     setjmp(buf) : salva como o original e anota "buf pertence a corotina atual";
     longjmp(buf): restaura os registradores como o original e troca para a corotina dona de buf; se buf nao foi gerado por
                   setjmp (a biblioteca monta o contexto de uma thread nova na mao), cria uma corotina que comeca em buf.pr.
   A corotina que chamou longjmp fica suspensa DENTRO dessa chamada; quando alguem fizer longjmp para o buf dela, a chamada
   retorna - e nos dois pontos de uso da biblioteca isso equivale ao segundo retorno do setjmp. CT2_NOCORO=1 desliga.
   --------------------------------------------------------------------------------------------------------------------- */
#ifdef _WIN32
#include <windows.h>
#else
#include <ucontext.h>
#endif
extern void f_0c095ccc(CPU *c); extern void f_0c095c58(CPU *c);
typedef struct Coro { 
#ifdef _WIN32
    void *fib;
#else
    ucontext_t uc; void *stk;
#endif
    int sp_saved; int used; } Coro;
static Coro co_main, co_pool[24]; static Coro *co_cur=0; static CPU *co_cpu=0; static int co_on=-1;
typedef struct { uint32_t buf, r15, pr; Coro *owner; int valid; } JBE;
static JBE jbt[96]; static unsigned jb_next=0;
static JBE *jb_find(uint32_t buf){ for(int i=0;i<96;i++) if(jbt[i].valid&&jbt[i].buf==buf) return &jbt[i]; return 0; }
static JBE *jb_slot(uint32_t buf){ JBE *j=jb_find(buf); if(j) return j; j=&jbt[jb_next++%96u]; j->valid=0; return j; }
static int co_enabled(void){ if(co_on<0) co_on=getenv("CT2_NOCORO")?0:1; return co_on; }
static void co_init(CPU *c){ if(co_cur) return; co_cpu=c; co_cur=&co_main;
#ifdef _WIN32
    co_main.fib=ConvertThreadToFiber(0);
#endif
}
static void co_body(void){
    CPU *c=co_cpu; uint32_t t=c->pr; unsigned long long guard=0;
    for(;;){ rt_call(c,t); t=c->pr; if(++guard>2000000ull) rt_fail(c,"thread do backup girando sem ceder",t); }   /* rts para pr: a thread reinicia a funcao */
}
#ifdef _WIN32
static void CALLBACK co_fiber(void *p){ co_body(); }
#else
static void co_ucfn(void){ co_body(); }
#endif
static Coro *co_new(void){
    for(int i=0;i<24;i++) if(!co_pool[i].used){ Coro *n=&co_pool[i]; n->used=1; n->sp_saved=0;
#ifdef _WIN32
        n->fib=CreateFiber(1u<<20,co_fiber,0);
#else
        n->stk=malloc(1u<<20); getcontext(&n->uc); n->uc.uc_stack.ss_sp=n->stk; n->uc.uc_stack.ss_size=1u<<20; n->uc.uc_link=0; makecontext(&n->uc,co_ucfn,0);
#endif
        return n; }
    return 0;
}
static void co_switch(CPU *c, Coro *to){
    Coro *from=co_cur; from->sp_saved=c->sp; co_cur=to;
#ifdef _WIN32
    SwitchToFiber(to->fib);
#else
    swapcontext(&from->uc,&to->uc);
#endif
    c->sp=from->sp_saved; co_cur=from;      /* voltamos: alguem fez longjmp para um buffer nosso */
}
void hle_setjmp(CPU *c){
    uint32_t buf=c->r[4]; f_0c095ccc(c);
    if(!co_enabled()) return;
    co_init(c); JBE *j=jb_slot(buf); j->buf=buf; j->r15=rt_r32(c,buf); j->pr=rt_r32(c,buf+8); j->owner=co_cur; j->valid=1;
}
void hle_longjmp(CPU *c){
    uint32_t buf=c->r[4]; JBE *j=co_enabled()?jb_find(buf):0;
    if(co_enabled()) co_init(c);
    f_0c095c58(c);                                   /* restaura todos os registradores do buffer (r0=valor!=0, pr, r15...) */
    if(!co_enabled()) return;
    Coro *to=0;
    if(j && j->owner && j->owner!=co_cur && rt_r32(c,buf)==j->r15 && rt_r32(c,buf+8)==j->pr) to=j->owner;   /* contexto salvo por setjmp e intacto */
    if(!to){ to=co_new(); if(!to) rt_fail(c,"threads do backup: corotinas esgotadas",buf); if(j) j->valid=0; }  /* contexto montado a mao: nova thread */
    co_switch(c,to);
}
