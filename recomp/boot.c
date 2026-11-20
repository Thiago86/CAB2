#include <math.h>
/* Executa o boot recompilado: uso  boot 1ST_READ.BIN [IP.BIN] [max_log] [segundos]
   Hardware = tabela de registradores (le o ultimo valor escrito, 0 se nunca escrito). Registra tudo e para
   no primeiro salto para endereco desconhecido ou em laco de espera. Serve para descobrir O QUE o jogo exige. */
#ifndef GENV
#define GENV(n) ({ static int _gi=0; static const char *_gv=0; if(!_gi){ _gv=getenv(n); _gi=1; } _gv; })   /* v15o: getenv() em laco quente custava ms por quadro no Windows */
#endif
#include "sh4rt.h"
#include "ct2_funcs.h"
#include "ta.h"
#ifdef CT2_WINDOW
#include "win.h"
#endif
#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include "aica.h"
#ifndef _WIN32
#include <unistd.h>
#endif
static CPU c; static jmp_buf jb;
#define NREG 16384
typedef struct { uint32_t key, val, nr, nw, first_fn, order; uint8_t used, size; } Reg;
static Reg regs[NREG]; static int nused=0, maxlog=150, logged=0;
static uint32_t last_key=0; static unsigned streak=0;
static uint32_t keyof(uint32_t a){ return a>=0xE0000000u ? a : (a&0x1FFFFFFFu); }
static const char *region(uint32_t k){
    if(k>=0xFF000000u) return "chip(P4)"; if(k>=0xE0000000u) return "store-queue";
    if(k<0x00200000u) return "boot-rom"; if(k>=0x005F6800u&&k<0x005F6C00u) return "holly-sys"; if(k>=0x005F6C00u&&k<0x005F7000u) return "maple";
    if(k>=0x005F7000u&&k<0x005F7400u) return "g1-gdrom"; if(k>=0x005F7400u&&k<0x005F8000u) return "g1/g2-dma"; if(k>=0x005F8000u&&k<0x005FA000u) return "pvr-regs";
    if(k>=0x005FA000u&&k<0x005FC000u) return "pvr-palette?"; if(k>=0x00600000u&&k<0x00700000u) return "g2-ext"; if(k>=0x00700000u&&k<0x00710000u) return "aica-regs";
    if(k>=0x00710000u&&k<0x00800000u) return "aica-rtc/ext"; if(k>=0x00800000u&&k<0x00A00000u) return "aica-ram"; if(k>=0x04000000u&&k<0x08000000u) return "vram";
    if(k>=0x10000000u&&k<0x10800000u) return "ta-fifo"; if(k>=0x10800000u&&k<0x11000000u) return "ta-yuv"; return "outro";
}
/* valores de leitura de registradores somente-leitura (ate serem escritos). Fonte de cada um no comentario. */
static uint32_t preset(uint32_t k){
    switch(k){
    case 0x005F8000u: return 0x17FD11DBu;   /* PVR ID: constante comparada em f_0c1532c2 (observado no codigo) */
    case 0x005F8004u: return 0x00000011u;   /* PVR REVISION: o codigo exige >= 17 (observado); 0x11 e o valor do hardware */
    default: return 0; }
}
static uint8_t vram[16u<<20], aram[2u<<20];   /* 16 MB: o Kamui do jogo recebe um pool de texturas maior (ver patch_vram_pool) */ static unsigned long long n_vw, n_vr, n_aw, n_ar, n_taw;
static uint8_t *backing(uint32_t k, unsigned long long **cw, unsigned long long **cr){
    if(k>=0x04000000u && k<0x08000000u){ *cw=&n_vw; *cr=&n_vr; uint32_t o=k&0x7FFFFFu, hi=k&0x800000u;   /* hi: segunda janela de 8 MB (VRAM emulada de 16 MB) */
        if(k&0x01000000u){   /* area de 32 bits (0x05/0x07...): bancos intercalados a cada palavra de 32 bits (premissa: formula conhecida do hardware) */
            o=(o&3u)|((o&0x3FFFFCu)<<1)|((o&0x400000u)?4u:0u); }
        return vram+(o|hi); }   /* area de 64 bits (0x04/0x06) = linear */
    if(k>=0x11000000u && k<0x14000000u){ *cw=&n_vw; *cr=&n_vr; return vram+(k&(k<0x12000000u?0xFFFFFFu:0x7FFFFFu)); }   /* janela de textura do TA (64 bits): tratada como VRAM linear */
    if(k>=0x00800000u && k<0x00A00000u){ *cw=&n_aw; *cr=&n_ar; return aram+(k-0x00800000u); }
    return 0;
}
static Reg *find(uint32_t k){
    uint32_t h=(k*2654435761u)>>18 & (NREG-1);
    for(unsigned guard=0;regs[h].used && regs[h].key!=k;h=(h+1)&(NREG-1)) if(++guard>NREG){ rt_fail(&c,"tabela de registradores cheia",k); }
    if(!regs[h].used){ regs[h].used=1; regs[h].key=k; regs[h].order=++nused; regs[h].first_fn=c.sp>0&&c.sp<=RT_STK?c.stk[c.sp-1]:0; }
    return &regs[h];
}
uint32_t ct2_edge_acc[8]; static uint32_t lib_acc[8]; static int ct2_nosticky(void){ static int v=-1; if(v<0) v=getenv("CT2_NOSTICKY")!=0; return v; }   /* v15zm: CT2_NOSTICKY=1 devolve a borda crua (comportamento anterior a v15u) para testar o salto */
uint32_t ct2_lib_edge(unsigned p,uint32_t e){ static unsigned age[8]; if(ct2_nosticky()) return e; if(p>7) p=7; if(e){ lib_acc[p]|=e; age[p]=0; } else if(lib_acc[p]){ if(++age[p]>8){ lib_acc[p]=0; age[p]=0; } } return lib_acc[p]; } void ct2_lib_take(unsigned p){ if(p>7) p=7; lib_acc[p]=0; }
uint32_t ct2_edge_step(unsigned p,uint32_t e){ static unsigned age[8]; if(ct2_nosticky()) return e; if(p>7) p=7; if(e){ ct2_edge_acc[p]|=e; age[p]=0; } else if(ct2_edge_acc[p]){ if(++age[p]>2){ ct2_edge_acc[p]=0; age[p]=0; } } return ct2_edge_acc[p]; }   /* v15u: borda sticky: sobrevive a 2 leituras do pad antes do gerador de eventos; expira em 3 leituras */ int ct2_p3_on=0; uint16_t ct2_p3_btn=0xFFFF; uint8_t ct2_p3_lt=0, ct2_p3_rt=0;
static unsigned perf_wc[3]; static int perf_win=0, perf_nw=0; static double perf_r,perf_p,perf_w,perf_tend; static FILE*pf=0; static unsigned perf_nreads=0;
static void logacc(char rw,uint32_t a,int size,uint32_t v){
    if(logged>=maxlog) return; logged++;
    printf("  %c%d %08x = %08x  [%s]  em f_%08x\n",rw,size*8,a,v,region(keyof(a)),c.sp>0&&c.sp<=RT_STK?c.stk[c.sp-1]&0x1FFFFFFFu:0);
}
/* ---- eventos assincronos: relogio, vblank, DMA do canal 2, interrupcoes (ASIC Holly -> INTEVT 0x320) ---- */
static unsigned long long n_arm_start=0; static uint32_t istnrm=0; static int in_irq=0; static unsigned long long n_irq=0, n_dma=0, n_dmabytes=0; static unsigned ticks=0; static unsigned gd_dma_at=0, auto_clear14=0;
static uint32_t regv(uint32_t k){ Reg*r=find(k); return r->nw?r->val:0; }
static void dma_ch2(void){
    uint32_t src=regv(0xFFA00020u)&0x1FFFFFFFu, dst=regv(0x005F6800u), len=regv(0x005F6804u)&0x00FFFFFFu; n_dma++; n_dmabytes+=len;
    if(src+len<=RAM_SIZE+0x0C000000u && src>=0x0C000000u){
        const uint8_t *s=c.ram+(src-0x0C000000u);
        uint32_t d=dst&0x1FFFFFFFu;
        if(d>=0x10000000u&&d<0x10800000u){ n_taw+=len/4; ta_feed(s,len); }
        else if(d>=0x13000000u&&d<0x14000000u){ uint32_t o=d&0x7FFFFFu, hi=d&0x800000u; for(uint32_t k=0;k+4<=len;k+=4){ uint32_t q=o+k; q=((q&3)|((q&0x3FFFFCu)<<1)|((q&0x400000u)?4u:0u))|hi; if(q+4<=sizeof vram) memcpy(vram+q,s+k,4); } }
        else if(d>=0x11000000u&&d<0x13000000u){ uint32_t o=d&0xFFFFFFu; if(o+len<=sizeof vram) memcpy(vram+o,s,len); }
        else if(d>=0x0C000000u&&d+len<=0x0C000000u+RAM_SIZE) memcpy(c.ram+(d-0x0C000000u),s,len);
    }
    istnrm|=1u<<19;
}
static void deliver(CPU *cc,uint32_t evt,uint32_t handler){
    if(!handler||in_irq) return; CPU saved=*cc; in_irq=1; n_irq++;
    Reg*e=find(0xFF000028u); e->nw++; e->val=evt;
    uint32_t sr=rt_get_sr(cc); rt_set_sr(cc,sr|0xF0u|0x10000000u&0);  /* mascara 15 enquanto o tratador roda */
    cc->pr=0; cc->r[4]=evt; rt_call(cc,handler);
    if(auto_clear14&&evt==0x360u){ istnrm&=~(1u<<14); auto_clear14=0; }   /* premissa: o BIOS real confirmaria o fim do DMA */
    *cc=saved; in_irq=0;
}
static unsigned max_ticks=0;
static unsigned ct2_vper(void){ static unsigned v=0; if(!v){ const char*e=getenv("CT2_VBLK"); v=e?(unsigned)atoi(e):16u; if(v<4) v=4; if(v>256) v=256; } return v; }   /* v15zn: relogios por vblank (padrao 16) */
#ifdef CT2_WINDOW
static void audio_frame(void){ }
#else
static FILE *wavf; static uint32_t wav_n;
static void wav_close(void){ if(!wavf) return; uint32_t db=wav_n*4, rl=36+db, sr=44100, br=44100*4; uint16_t ch=2,bl=4,bp=16,fmt=1; uint32_t fl=16;
    fseek(wavf,0,SEEK_SET); fwrite("RIFF",1,4,wavf); fwrite(&rl,4,1,wavf); fwrite("WAVEfmt ",1,8,wavf); fwrite(&fl,4,1,wavf); fwrite(&fmt,2,1,wavf); fwrite(&ch,2,1,wavf); fwrite(&sr,4,1,wavf); fwrite(&br,4,1,wavf); fwrite(&bl,2,1,wavf); fwrite(&bp,2,1,wavf); fwrite("data",1,4,wavf); fwrite(&db,4,1,wavf); fclose(wavf); wavf=0; }
static void audio_frame(void){ static double acc=0; acc+=735.0/(double)ct2_vper(); unsigned n=(unsigned)acc; acc-=n; int16_t buf[64*2]; aica_render(buf,(int)n);
    if(!wavf&&GENV("CT2_WAV")){ wavf=fopen(GENV("CT2_WAV"),"wb"); if(wavf){ char z[44]; memset(z,0,44); fwrite(z,1,44,wavf); atexit(wav_close); } }
    if(wavf){ fwrite(buf,4,n,wavf); wav_n+=n; } }
#endif
static void tick(CPU *cc){
#ifdef CT2_WINDOW
    { static unsigned pm=0; if((++pm&7)==0 && !win_pump()) rt_fail(cc,"janela fechada",0); }
#endif
    ticks++; audio_frame(); if(max_ticks&&ticks>max_ticks) rt_fail(cc,"limite de relogios atingido (normal: o jogo continuava rodando)",0);
    if(gd_dma_at&&ticks>=gd_dma_at){ gd_dma_at=0; istnrm|=1u<<14; auto_clear14=1; }
    if(ticks%ct2_vper()==0){ istnrm|=0x18u; }                       /* a cada 16 relogios: vblank entrada+saida */
    if(in_irq) return;
    uint32_t im=(rt_get_sr(cc)>>4)&15;
    /* mascaras do ASIC (Holly): 0x5F6930 -> IRL9 (INTEVT 0x320), 0x5F6920 -> IRL11 (0x360): confirmado pelos valores que o jogo escreve */
    if(im<11 && (istnrm&regv(0x005F6920u))) deliver(cc,0x360u,rt_r32(cc,cc->vbr+0x200u+((0x360u-0x200u)>>3)));
    else if(im<9 && (istnrm&regv(0x005F6930u))) deliver(cc,0x320u,rt_r32(cc,cc->vbr+0x200u+((0x320u-0x200u)>>3)));
}
/* ---- TA: store queues, FIFO, DMA -> lista de parametros -> rasterizador (ta.c) ---- */
static unsigned long long n_eol[5], n_sqflush, n_sqta; static uint32_t sq[2][8], pal[1024]; static unsigned frame_no=0, saved_frames=0, last_saved=0, max_tris=0; static unsigned long long frames_total=0;
static const char *frames_dir="frames"; static unsigned frame_every=12, frame_max=12, start_at=0;

/* O jogo carrega ~4.8 MB de texturas de cenario mas o pool do Kamui (banco 1) so tem 4 MB: as que nao cabem recebem a textura branca 8x8 (predios brancos).
   Aqui o pool ganha +2 MB (a VRAM emulada tem 16 MB, o TCW enderecaria ate 16 MB). */
static void patch_vram_pool(void){
    const uint32_t base=0x0C171EE0u, EXT=0x200000u;
    #define RW(a) (*(uint32_t*)(c.ram+((a)-0x0C000000u)))
    uint32_t p=RW(base+8); int guard=0;
    while(p>=0x0C000000u&&p<0x0C000000u+RAM_SIZE-32&&guard++<64){
        uint32_t addr=RW(p+12), size=RW(p+16);
        if(addr+size==0x800000u&&size>=0x80000u&&addr>=0x400000u){ RW(p+16)=size+EXT; RW(base+16)+=EXT; if(GENV("CT2_POOLDBG")) printf("[pool] bloco livre %08x addr=%x size=%x estendido +%x\n",p,addr,size,EXT); break; }
        p=RW(p+8);
    }
    #undef RW
}
static void sq_flush(uint32_t t,const uint32_t *w){
    t&=0x1FFFFFFFu; n_sqflush++; if(t>=0x10000000u&&t<0x10800000u) n_sqta++;
    if(t>=0x10000000u&&t<0x10800000u && GENV("CT2_VSRC")){ float vv=*(float*)&w[5], tv=(float)atof(GENV("CT2_VSRC")); extern unsigned ta_cur_frame; if(fabsf(vv-tv)<0.0006f && fabsf(*(float*)&w[4])<1e-6f && *(float*)&w[3]<0.01f && *(float*)&w[3]>0.0005f){ static unsigned nz2; if(nz2++<16){ printf("[vsrc] x=%g y=%g z=%g u=%g v=%g pilha:",*(float*)&w[1],*(float*)&w[2],*(float*)&w[3],*(float*)&w[4],vv); for(int q=c.sp-1;q>=0&&q>c.sp-8;q--) printf(" %08x",c.stk[q]&0x1FFFFFFFu); printf("\n"); } } }
    if(t>=0x10000000u&&t<0x10800000u && GENV("CT2_ZSRC") && w[3]==0x40555555u){ static unsigned nz; if(nz++<12){ printf("[zsrc] vertice z=3.3333 x=%g y=%g u=%g v=%g pilha:",*(float*)&w[1],*(float*)&w[2],*(float*)&w[4],*(float*)&w[5]); for(int q=c.sp-1;q>=0&&q>c.sp-6;q--) printf(" %08x",c.stk[q]&0x1FFFFFFFu); printf("\n"); } }
    if(t>=0x10000000u&&t<0x10800000u) ta_feed((const uint8_t*)w,32);
    else if(t>=0x13000000u&&t<0x14000000u){ uint32_t o=t&0x7FFFFFu, hi=t&0x800000u; for(int k=0;k<8;k++){ uint32_t q=o+4*k; q=((q&3)|((q&0x3FFFFCu)<<1)|((q&0x400000u)?4u:0u))|hi; if(q+4<=sizeof vram) memcpy(vram+q,&w[k],4); } }   /* area de 32 bits da memoria de textura: intercalacao dos bancos */
    else if(t>=0x11000000u&&t<0x13000000u){ uint32_t o=t&0xFFFFFFu; if(o+32<=sizeof vram) memcpy(vram+o,w,32); }
    else if(t>=0x0C000000u&&t+32<=0x0C000000u+RAM_SIZE){ memcpy(c.ram+(t-0x0C000000u),w,32);
#ifdef WATCH
      { uint32_t a=t+0x80000000u; if(a+32>0x8c1be4bcu&&a<0x8c1cf424u){ static int n=0; if(n++<40) fprintf(stderr,"WATCH sq entry=%d t=%08x w0=%08x w1=%08x w2=%08x w3=%08x w4=%08x\n",(int)(a-0x8c1be4bcu)/60,a,w[0],w[1],w[2],w[3],w[4]); } }
#endif
    }
}
static void pref_cb(CPU *cc,uint32_t a){ (void)cc;
    if(a>=0xE0000000u&&a<0xE4000000u){ int q=(a>>5)&1; uint32_t qacr=regv(q?0xFF00003Cu:0xFF000038u); sq_flush(((qacr&0x1Cu)<<24)|(a&0x03FFFFE0u),sq[q]); }
}
int ct2_hoptrace_req=0; int ct2_quit_req=0;
int ct2_hop_orig(void){ static int v=-1; if(v<0){ const char*e=getenv("CT2_HOPMAX"); v=!(e&&*e&&*e!='0'); } return v; }   /* v15zt: salto original por padrao; CT2_HOPMAX=1 forca a forca maxima mesmo parado */
int rt_hop_on=0; uint32_t rt_hop_w[4]={0x0C173AB8u,0x0C173AC4u,0x0C173AA0u,0x0C173D74u}; static FILE*hop_file=0;
static void __attribute__((constructor)) whop_init(void){ const char*e=getenv("CT2_WHOP"); if(e){ rt_hop_w[0]=(uint32_t)strtoul(e,0,16)&0x1FFFFFFCu; rt_hop_w[1]=rt_hop_w[2]=rt_hop_w[3]=0xFFFFFFFFu; rt_hop_on=1; hop_file=stdout; } }   /* depuracao: vigia de escrita em um endereco (CT2_WHOP=hex) */
void rt_hop_hit(CPU*cc,uint32_t a,uint32_t v){ static unsigned n=0; if(!hop_file||n++>400) return; float f; memcpy(&f,&v,4); fprintf(hop_file,"W f%u %08x=%08x (%g) pilha:",frame_no,a&0x1FFFFFFFu|0x80000000u,v,f); for(int q=cc->sp-1;q>=0&&q>cc->sp-9;q--) fprintf(hop_file," %08x",cc->stk[q]&0x1FFFFFFFu|0x80000000u); fprintf(hop_file,"\n"); }
uint16_t pad_buttons=0xFFFF; uint8_t pad_rt=0, pad_lt=0, pad_jx=128, pad_jy=128;   /* botoes ativos em nivel baixo; preenchido pela janela (passo 3) */
static void eol_cb(int list){ if(list>=0&&list<5) n_eol[list]++; static const int bit[5]={7,8,9,10,21}; if(list>=0&&list<5) istnrm|=1u<<bit[list]; }   /* fim de lista -> ISTNRM (numeracao conforme as mascaras que o jogo programa) */
static void start_render(void){
    istnrm|=7u;   /* fim de renderizacao (video/ISP/TSP) */
    if(GENV("CT2_TICKLOG")){ static unsigned lt=0; static unsigned hist[40]; static unsigned nf=0; unsigned d=ticks-lt; lt=ticks; if(d>39) d=39; hist[d]++; if(++nf%600==0){ printf("[ticklog] f%u relogios/quadro:",frame_no); for(int i=0;i<40;i++) if(hist[i]) printf(" %d:%u",i,hist[i]); printf("\n"); memset(hist,0,sizeof hist); } }
    /* v15zp: gravacao do salto (F10): arma; no 1o quadro com Y apertado grava, em hop_trace.txt, as palavras de 32 bits da RAM que mudaram em cada um dos 45 quadros seguintes (endereco, valor antigo, valor novo) */
    { extern int ct2_hoptrace_req; static int st=0; static uint8_t*prev=0; static unsigned rec=0; static FILE*hf=0; const size_t RS=16u<<20;
      { const char*e=GENV("CT2_HOPARM"); if(e&&frame_no+1==(unsigned)atoi(e)) ct2_hoptrace_req=1; }
      if(ct2_hoptrace_req){ ct2_hoptrace_req=0; if(st==0){ if(!prev) prev=(uint8_t*)malloc(RS); if(prev){ memcpy(prev,c.ram,RS); st=1; } } }
      if(st==1){ if(!(pad_buttons&(1u<<9))){ hf=fopen("hop_trace.txt","w"); if(hf){ fprintf(hf,"# palavras de 32 bits da RAM que mudaram por quadro (addr antigo novo); Y apertado no quadro %u\n",frame_no+1); rec=0; st=2; hop_file=hf; rt_hop_on=1; } else st=0; } else memcpy(prev,c.ram,RS); }
      if(st==2){ unsigned n=0; fprintf(hf,"F %u rel=%u pad=%04x lt=%u rt=%u\n",frame_no+1,rec,(unsigned)pad_buttons,(unsigned)pad_lt,(unsigned)pad_rt);
        for(size_t i=0;i<RS;i+=4){ uint32_t x,y; memcpy(&x,c.ram+i,4); memcpy(&y,prev+i,4); if(x!=y&&n<40000){ fprintf(hf,"%08x %08x %08x\n",0x8C000000u+(unsigned)i,y,x); n++; } }
        memcpy(prev,c.ram,RS); if(++rec>=45){ rt_hop_on=0; hop_file=0; fclose(hf); hf=0; st=0; printf("[hoptrace] gravado hop_trace.txt\n"); } }
    }
    frame_no++; frames_total++; patch_vram_pool();
    { extern void ct2_cull_apply(CPU*); ct2_cull_apply(&c); }   /* v15zzi */
    if(GENV("CT2_WWATCH")){ unsigned wa=(unsigned)strtoul(GENV("CT2_WWATCH"),0,16); static uint32_t lw=0xDEADBEEF; uint32_t v; memcpy(&v,c.ram+wa,4); if(v!=lw){ lw=v; printf("[ww] f%u %06x=%08x\n",frame_no,wa,v); } }
    { extern int ct2_cam_mode,ct2_cam_sub; { extern int ct2_cam_force; if(ct2_cam_force>=0) c.ram[0x175e58u]=(uint8_t)ct2_cam_force; else if(GENV("CT2_CAMFORCE")&&frame_no>=2700) c.ram[0x175e58u]=(uint8_t)atoi(GENV("CT2_CAMFORCE")); } ct2_cam_mode=c.ram[0x175e58u]; ct2_cam_sub=c.ram[0x2ac916u]; { static FILE*cl=0; static int lm0=-99; static unsigned lh=0; if(!cl){ cl=fopen("camera.log","w"); if(cl) fprintf(cl,"# modo de camera lido da RAM do jogo (0x8c175e58): 0 normal, 9 ANGLE, 1 DASHBOARD, 2 BACK, 10 PERSPECTIVE/SPEEDOMETER; 8 e outros = menus\n"); } if(cl&&(ct2_cam_mode!=lm0||frame_no-lh>=600)){ lm0=ct2_cam_mode; lh=frame_no; fprintf(cl,"quadro %u modo=%d\n",frame_no,lm0); fflush(cl); } }
    if(GENV("CT2_CAMLOG")){ static int lm=-1,ls=-1; if(ct2_cam_mode!=lm){ lm=ct2_cam_mode; printf("[cam] f%u modo=%d sub=%d\n",frame_no,lm,ct2_cam_sub); } if(ct2_cam_sub!=ls){ ls=ct2_cam_sub; printf("[cam] f%u sub=%d\n",frame_no,ls); } } }   /* modo de camera do jogo (0x8c175e58) */
#ifdef FNTRACE
    { extern void fntrace_frame(unsigned); fntrace_frame(frame_no); }
#endif
    { static int init=0; static unsigned st=0, every=12, maxsave=12; if(!init){ init=1; const char*e=GENV("CT2_START_FRAME"); if(e) st=atoi(e); e=GENV("CT2_EVERY"); if(e) every=atoi(e); e=GENV("CT2_MAXSAVE"); if(e) maxsave=atoi(e); frame_every=every; frame_max=maxsave; start_at=st; }
      pad_buttons=0xFFFF; if(start_at&&frame_no>=start_at&&frame_no<start_at+30) pad_buttons&=~(1u<<3);
      if(GENV("CT2_AUTOPLAY")&&start_at&&frame_no>=start_at+60){
        if(atoi(GENV("CT2_AUTOPLAY"))>=2){ static const int seq[]={3,2,2,5,5,2,3,4,2,6,7,1,2,3,10,9,2,5,2,3,3,2,2,7,7,2,3,1,1,3}; unsigned n=(frame_no-start_at)/40; unsigned k=(frame_no-start_at)%40; if(k<6) pad_buttons&=~(1u<<seq[n%(sizeof seq/sizeof seq[0])]); pad_jx=(n%11<4)?255:128; }
        else { unsigned k=(frame_no-start_at)%90; if(k<6) pad_buttons&=~(1u<<(((frame_no-start_at)/90)&1?2:3)); } } }   /* autoplay (descoberta de codigo): pulsos alternados de START e A */   /* simula apertar START por 30 quadros */ char path[300]; snprintf(path,sizeof path,"%s/f%05u.ppm",frames_dir,frame_no); unsigned nt=0;
    { static int init=0, n=0; static struct { unsigned a,b; uint16_t m; uint8_t t; } sc[256];   /* CT2_PAD="a-b:UDLRABXYS,..." : botoes pressionados entre os quadros a e b */
      if(!init){ init=1; const char*e=GENV("CT2_PAD"); while(e&&*e&&n<256){ unsigned a=0,bb=0; int k=0; if(sscanf(e,"%u-%u:%n",&a,&bb,&k)<2||!k) break; e+=k; uint16_t m=0; int trg=0; while(*e&&*e!=','){ switch(*e){ case 'U':m|=1u<<4;break; case 'D':m|=1u<<5;break; case 'L':m|=1u<<6;break; case 'R':m|=1u<<7;break; case 'A':m|=1u<<2;break; case 'B':m|=1u<<1;break; case 'X':m|=1u<<10;break; case 'Y':m|=1u<<9;break; case 'S':m|=1u<<3;break; case 'r':trg|=1;break; case 'l':trg|=2;break; } e++; } if(*e==',') e++; sc[n].a=a; sc[n].b=bb; sc[n].m=m; sc[n].t=(uint8_t)trg; n++; } }
      pad_rt=0; pad_lt=0; for(int i=0;i<n;i++) if(frame_no>=sc[i].a&&frame_no<=sc[i].b){ pad_buttons&=~sc[i].m; if(sc[i].t&1) pad_rt=255; if(sc[i].t&2) pad_lt=255; } }
    { extern void menux_nav(uint16_t*); if(GENV("CT2_NAV")||GENV("CT2_NAVGAME")) menux_nav(&pad_buttons); }
    { const char*e=GENV("CT2_P3"); if(e){ /* teste: CT2_P3="quadro:letras,quadro:letras" segura as letras (B Y X A S L) no controle 3 por 6 quadros; so vale depois do menu principal */ extern int ct2_p3_on; static unsigned bf3=0; extern int ta_menu_screen(void); if(!bf3&&ta_menu_screen()==1) bf3=frame_no; ct2_p3_on=1; ct2_p3_btn=0xFFFF; ct2_p3_lt=0; const char*q=e; while(*q&&bf3){ unsigned f=(unsigned)strtoul(q,(char**)&q,10); if(*q==':') q++; char l[8]; int n=0; while(*q&&*q!=','&&n<7) l[n++]=*q++; l[n]=0; if(*q==',') q++; if(frame_no>=bf3+f&&frame_no<bf3+f+6){ for(int i=0;i<n;i++){ if(l[i]=='B') ct2_p3_btn&=~(1u<<1); if(l[i]=='A') ct2_p3_btn&=~(1u<<2); if(l[i]=='S') ct2_p3_btn&=~(1u<<3); if(l[i]=='Y') ct2_p3_btn&=~(1u<<9); if(l[i]=='X') ct2_p3_btn&=~(1u<<10); if(l[i]=='L') ct2_p3_lt=255; if(l[i]=='R') ct2_p3_rt=255; } } } } }
    if(GENV("CT2_NOTIMER")&&frame_no>=1500) ct2_cheat_notimer=1;
    if(GENV("CT2_UNLOCK")) ct2_cheat_unlock=1;
    { static uint32_t tl=0; uint32_t v=rt_r32(&c,0x8c1eadb0u);   /* reforco do cheat Timer (independe do patch em ct2_funcs_00.c): o contador so pode subir */
      if(GENV("CT2_TIMERDBG")&&frame_no%120==0) printf("[timer] f%u var=%u notimer=%d\n",frame_no,v,ct2_cheat_notimer);
      if(ct2_cheat_notimer&&v<tl&&tl-v<=120) rt_w32(&c,0x8c1eadb0u,tl); else tl=v; }
    if(ct2_cheat_unlock) rt_w8(&c,0x8c28ce98u,0xFF);   /* cheat Unlock All (GameShark 0028CE98 000000FF) */   /* teste do cronometro fixo (Linux) */
    if(GENV("CT2_DRIVE")&&frame_no>=(unsigned)atoi(GENV("CT2_DRIVE"))){ pad_rt=255; pad_jx=(((frame_no/300)%3)==1)?40:(((frame_no/300)%3)==2)?215:128; }   /* teste: acelera e vira */
extern unsigned ta_cur_frame; extern uint32_t ta_reg_backgnd_t,ta_reg_param_base,ta_reg_backgnd_d;
#ifdef CT2_WINDOW
    { unsigned nt2=0; ta_cur_frame=frame_no; ta_reg_backgnd_t=regv(0x005F808Cu); ta_reg_param_base=regv(0x005F8020u); ta_reg_backgnd_d=regv(0x005F8088u); static int skipped=0; extern int ct2_ff; extern int win_behind(void); extern double win_now_ms(void); extern void win_perf(double,double,double,int); const uint32_t *fbp=0;
      static double pt_last=0, pt_r=0, pt_p=0; static int pn=0, pr_n=0, ps_n=0;
      double tq0=win_now_ms();
      { /* v15s: diagnostico em perf.txt (tempo da logica do jogo, desenho, tela, espera; entradas e quando o jogo as consumiu) */
        static int pfn=0; static double g_sum=0,g_max=0,r_sum=0,r_max=0,p_sum=0,p_max=0,w_sum=0,w_max=0; static int pc=0; static uint16_t lastb=0xFFFF; static unsigned ev_f=0;
        if(!pf&&pfn==0){ pf=fopen("perf.txt","w"); pfn=1; if(pf) fprintf(pf,"# logica = tempo do jogo entre quadros; desenho; tela; espera (media/max em ms)\n"); }
        if(pf){ if(perf_tend>0){ double g=tq0-perf_tend; g_sum+=g; if(g>g_max) g_max=g; r_sum+=perf_r; if(perf_r>r_max) r_max=perf_r; p_sum+=perf_p; if(perf_p>p_max) p_max=perf_p; w_sum+=perf_w; if(perf_w>w_max) w_max=perf_w; pc++; }
          uint32_t ev=rt_r32(&c,0x8c2a2770u);
          (void)lastb;
          if(ev&&ev_f!=frame_no){ fprintf(pf,"f%u jogo_evento=%x\n",frame_no,ev); ev_f=frame_no; }
          if(perf_win>0){ perf_win--; fprintf(pf,"  f%u chamadas: atualiza_pad=%u driver=%u gerador=%u | borda_lib=%x borda_driver=%x evento=%x\n",frame_no,perf_wc[0],perf_wc[1],perf_wc[2],rt_r32(&c,0x8c2cef98u),rt_r32(&c,0x8c1ea85cu),ev); }
          perf_wc[0]=perf_wc[1]=perf_wc[2]=0;
          if(pc>=60){ fprintf(pf,"f%u logica %.1f/%.1f desenho %.1f/%.1f tela %.1f/%.1f espera %.1f/%.1f\n",frame_no,g_sum/pc,g_max,r_sum/pc,r_max,p_sum/pc,p_max,w_sum/pc,w_max); fflush(pf); pc=0; g_sum=g_max=r_sum=r_max=p_sum=p_max=w_sum=w_max=0; } }
      }
      if(0){}
      else if(win_behind()&&!skipped&&frame_no>300){ skipped=1; }   /* v15o: atrasado -> pula o desenho deste quadro (o jogo continua na velocidade certa; a imagem cai para 30 fps no pior caso) */
      else { skipped=0; fbp=ta_render_frame(vram,pal,regv(0x005F8108u)&3,&nt2); }
      double tq1=win_now_ms();
      ta_reset();
      if(fbp){ if(!win_present(fbp)) rt_fail(&c,"janela fechada",0); }
      double tq2=win_now_ms();
      { double tw0=win_now_ms(); win_throttle(); perf_w=win_now_ms()-tw0; perf_r=tq1-tq0; perf_p=tq2-tq1; perf_tend=win_now_ms(); }
      pn++; if(fbp){ pr_n++; pt_r+=tq1-tq0; pt_p+=tq2-tq1; } else ps_n++;
      if(pn>=60){ double now=win_now_ms(); if(pt_last>0&&pr_n>0) win_perf(pn*1000.0/(now-pt_last),pt_r/pr_n,pt_p/pr_n,ps_n); pt_last=now; pn=pr_n=ps_n=0; pt_r=pt_p=0; }
      win_pad(&pad_buttons,&pad_rt,&pad_lt,&pad_jx,&pad_jy);
      return; }
#endif
    if(GENV("CT2_ADXDBG")&&frame_no>=2040&&frame_no%60==0){ printf("[adx] f%u obj:",frame_no); for(int q=0;q<14;q++) printf(" %08x",rt_r32(&c,0x8c171924u+4*q)); printf("\n"); }
    if(GENV("CT2_REGDBG")&&frame_no%20==0) printf("FBW f%u CTRL=%08x STRIDE=%08x SOF1=%08x SOF2=%08x R_CTRL=%08x R_SOF1=%08x R_SOF2=%08x XCLIP=%08x\n",frame_no,regv(0x005F8048u),regv(0x005F804Cu),regv(0x005F8060u),regv(0x005F8064u),regv(0x005F8044u),regv(0x005F8050u),regv(0x005F8054u),regv(0x005F8068u));
    if(0&&GENV("CT2_REGDBG")) printf("REGS f%u T=%08x PB=%08x D=%08x REGION=%08x ISP_FEED=%08x FB_R_SOF1=%08x TA_ISP_BASE=%08x TA_OL=%08x\n",frame_no,regv(0x005F8128u),regv(0x005F8020u),regv(0x005F8088u),regv(0x005F802Cu),regv(0x005F8098u),regv(0x005F8050u),regv(0x005F8128u),regv(0x005F8124u));
    ta_cur_frame=frame_no; ta_reg_backgnd_t=regv(0x005F808Cu); ta_reg_param_base=regv(0x005F8020u); ta_reg_backgnd_d=regv(0x005F8088u);
    if(GENV("CT2_ARAMBIN")&&frame_no%(unsigned)atoi(GENV("CT2_ARAMBIN"))==0&&frame_no>=300){ char pp[64]; snprintf(pp,sizeof pp,"/tmp/aram_%u.bin",frame_no); FILE*f=fopen(pp,"wb"); fwrite(aram,1,sizeof aram,f); fclose(f); }
    { const char*e=GENV("CT2_WORDS"); if(e&&frame_no>=(unsigned)atoi(GENV("CT2_WORDS_A")?GENV("CT2_WORDS_A"):"0")&&frame_no<=(unsigned)atoi(GENV("CT2_WORDS_B")?GENV("CT2_WORDS_B"):"0")){ printf("[w] f%u",frame_no); const char*p=e; while(*p){ unsigned a=(unsigned)strtoul(p,(char**)&p,16); printf(" %x",rt_r32(&c,a|0x80000000u)); if(*p==',') p++; } printf("\n"); } }
    { const char*e=GENV("CT2_RAMWIN"); if(e){ unsigned wa=0,wb=0,ws=1; sscanf(e,"%u,%u,%u",&wa,&wb,&ws); if(frame_no>=wa&&frame_no<=wb&&ws&&(frame_no-wa)%ws==0){ char pp[64]; snprintf(pp,sizeof pp,"/tmp/rw_%s_%u.bin",GENV("CT2_RAMTAG")?GENV("CT2_RAMTAG"):"x",frame_no); FILE*f=fopen(pp,"wb"); if(f){ fwrite(c.ram,1,16u<<20,f); fclose(f); } } } }
    if(GENV("CT2_RAMBIN")&&frame_no%(unsigned)atoi(GENV("CT2_RAMBIN"))==0&&frame_no>=1200){ char pp[64]; snprintf(pp,sizeof pp,"/tmp/ram_%u.bin",frame_no); FILE*f=fopen(pp,"wb"); fwrite(c.ram,1,16u<<20,f); fclose(f); }
    int want = (frame_no-last_saved>=frame_every && saved_frames<frame_max);
    if(want&&GENV("CT2_BIASSET")&&frame_no>=(unsigned)atoi(GENV("CT2_BIASSET"))&&frame_no<(unsigned)atoi(GENV("CT2_BIASSET"))+40){ extern float ta_bias_mip,ta_bias_nomip; static const float bs[3][2]={{0,0},{0.5f,0.5f},{-1.f,-0.6f}}; for(int q=0;q<3;q++){ ta_bias_mip=bs[q][0]; ta_bias_nomip=bs[q][1]; char p2[320]; snprintf(p2,sizeof p2,"%s.b%d.ppm",path,q); unsigned n2=0; ta_render(p2,vram,pal,regv(0x005F8108u)&3,&n2); } ta_bias_mip=-1.f; ta_bias_nomip=-0.6f; }
    if(want){ if(ta_render(path,vram,pal,regv(0x005F8108u)&3,&nt)){ saved_frames++; last_saved=frame_no; printf("  quadro %u: %u triangulos -> %s\n",frame_no,nt,path); } if(nt>max_tris) max_tris=nt; }
    else if(GENV("CT2_RENDERALL")||(GENV("CT2_WB")&&start_at&&frame_no>=start_at)){ if(ta_render("/dev/null",vram,pal,regv(0x005F8108u)&3,&nt)){} }
    if(want||(GENV("CT2_WB")&&start_at&&frame_no>=start_at)) (void)0;
    menux_filter(&pad_buttons,&pad_rt,&pad_lt);
    { extern int ct2_quit_req;
#ifdef _WIN32
      if(ct2_quit_req){ extern void win_request_quit(void); ct2_quit_req=0; win_request_quit(); }   /* v15zz: no Windows este codigo roda dentro de uma fibra; o longjmp do rt_fail saia dela e nao fechava. Agora pede o fechamento da janela */
#else
      if(ct2_quit_req) rt_fail(&c,"EXIT do menu principal (encerrado pelo jogador)",0);
#endif
    }
    ta_reset();
}
/* ---- Maple (controles): DMA de listas de quadros. Protocolo conforme documentacao publica (premissa: nao verificado neste projeto) ----
   Descritor: [0]=n palavras do quadro (bits 0-7), porta (bits 16-17), ultimo (bit 31); [1]=endereco de resposta; depois o quadro:
   cabecalho = cmd | destino<<8 | origem<<16 | n<<24, seguido de n palavras. Resposta: cmd 5 = info do dispositivo, 8 = dados. */
unsigned ct2_vib_pow=0, ct2_vib_stamp=0;
static int ct2_rumble(void){ static int v=-1; if(v<0){ const char*e=getenv("CT2_RUMBLE"); v=(e&&*e&&*e!='0'); } return v; }   /* v15zu: Jump Pack emulado vem DESLIGADO por padrao; CT2_RUMBLE=1 liga */
#ifndef VIBSLOT
#define VIBSLOT 2u
#endif
static unsigned long long n_maple=0; static int maple_log=0;
/* v15x: controle 3 (porta C) virtual: o jogo le funcoes de camera nele (B painel, Y angulo, X velocimetro, A reinicia, L cor da seta, START+Y panoramica); so e 'conectado' quando o menu de pausa o usa */

static void w32(uint32_t a,uint32_t v){ rt_w32(&c,a,v); }

/* ---- Memory card (VMU) na porta A, slot 1: 128 KB (256 blocos de 512 bytes) guardados em "vmu_a1.bin" (v15zu).
   Protocolo conforme documentacao publica do Maple/VMU (premissa: nao verificado contra o hardware nem contra o jogo ate agora). ---- */
static inline uint32_t vmu_bs(uint32_t x){ return (x>>24)|((x>>8)&0xFF00u)|((x<<8)&0xFF0000u)|(x<<24); }   /* palavras da documentacao (big-endian) -> memoria little-endian */
static uint8_t vmu_fl[131072]; static int vmu_init_done=0, vmu_dirty=0, vmu_on=1;
static void vmu_flush(void){ if(!vmu_dirty) return; FILE*f=fopen("vmu_a1.bin","wb"); if(f){ fwrite(vmu_fl,1,sizeof vmu_fl,f); fclose(f); vmu_dirty=0; } }
static void vmu_format(void){
    memset(vmu_fl,0,sizeof vmu_fl); uint8_t*r=vmu_fl+255*512; memset(r,0x55,16);
    r[0x30]=0x20; r[0x31]=0x26; r[0x32]=0x01; r[0x33]=0x01; r[0x34]=0; r[0x35]=0; r[0x36]=0; r[0x37]=0;   /* carimbo BCD (fixo) */
    r[0x46]=254; r[0x47]=0; r[0x48]=1; r[0x49]=0; r[0x4A]=253; r[0x4B]=0; r[0x4C]=13; r[0x4D]=0; r[0x4E]=0; r[0x4F]=0; r[0x50]=200; r[0x51]=0;
    uint16_t*fat=(uint16_t*)(vmu_fl+254*512); for(int i=0;i<256;i++) fat[i]=0xFFFC;
    for(int b=241;b<=253;b++) fat[b]=(uint16_t)(b==241?0xFFFA:b-1); fat[254]=0xFFFA; fat[255]=0xFFFA;
    vmu_dirty=1; vmu_flush();
}
static void vmu_load(void){
    if(vmu_init_done) return; vmu_init_done=1; if(GENV("CT2_NOVMU")) vmu_on=0;
    FILE*f=fopen("vmu_a1.bin","rb"); size_t n=0; if(f){ n=fread(vmu_fl,1,sizeof vmu_fl,f); fclose(f); }
    int ok=(n==sizeof vmu_fl); for(int i=0;ok&&i<16;i++) if(vmu_fl[255*512+i]!=0x55) ok=0;
    if(!ok){ vmu_format(); printf("[vmu] cartao novo formatado: vmu_a1.bin (128 KB)\n"); } else printf("[vmu] cartao carregado: vmu_a1.bin\n");
    atexit(vmu_flush);
}
#ifdef RDWATCH
extern int in_maple_now_guard; extern uint32_t rt_rd_lo, rt_rd_hi;
#endif
static void maple_dma(void){
#ifdef RDWATCH
    in_maple_now_guard=1;
#endif
    uint32_t p=regv(0x005F6C04u)|0x80000000u; p&=0x9FFFFFFFu; int guard=0; n_maple++;
    if(GENV("CT2_MAPSTK")){ static int nk=0; extern unsigned ta_cur_frame; if(nk++<25){ printf("[mapkick %d] f%u p=%08x stk:",nk,frame_no,p); for(int q=c.sp-1;q>=0&&q>c.sp-10;q--) printf(" %08x",c.stk[q]&0x1FFFFFFFu); printf(" |"); uint32_t pp=p; for(int g=0;g<8;g++){ uint32_t d0=rt_r32(&c,pp); uint32_t h=rt_r32(&c,pp+8); printf(" [p%u c%u d%02x n%u]",(d0>>16)&3,h&0xFF,(h>>8)&0xFF,d0&0xFF); if(d0&0x80000000u) break; pp+=8+4*((d0&0xFF)+1); } printf("\n"); } }
    { static int nr=0; if(GENV("CT2_MAPLERAW")&&nr++<8){ printf("RAW%d:",nr); for(int k=0;k<30;k++) printf(" %08x",rt_r32(&c,p+4*k)); printf("\n"); } }
    for(;;){
        uint32_t d0=rt_r32(&c,p), rx=rt_r32(&c,p+4);
        if((rx&0x1C000000u)!=0x0C000000u){ if(++guard>96) break; p+=4; continue; }   /* v15zf: palavras de enchimento entre descritores (ex. 0x700): ressincroniza */
        uint32_t n=d0&0xFF, port=(d0>>16)&3;
        uint32_t h=rt_r32(&c,p+8); uint32_t cmd=h&0xFF, dst=(h>>8)&0xFF, src=(h>>16)&0xFF; uint32_t fn=n?rt_r32(&c,p+12):0;
        if(dst!=0x20&&dst!=0x60&&dst!=0xa0&&dst!=0xe0){ static int ns=0; if(GENV("CT2_VIBLOG")&&ns++<40) printf("  MAPLE-OUTRO porta %u cmd=%u dest=%02x n=%u\n",port,cmd,dst,n); }
        if(maple_log<30){ maple_log++; printf("  MAPLE porta %u cmd=%u dest=%02x src=%02x n=%u func=%08x rx=%08x\n",port,cmd,dst,src,n,fn,rx); }
        { static FILE*vl=0; static uint32_t seen[256][4]; static int ns=0; if(!vl&&ns==0){ vl=fopen("vmu.log","w"); if(vl) fprintf(vl,"# memory card: comandos Maple distintos vistos (quadro porta comando destino n funcao)\n"); } int k; for(k=0;k<ns;k++) if(seen[k][0]==port&&seen[k][1]==cmd&&seen[k][2]==dst&&seen[k][3]==fn) break; if(k==ns&&ns<256&&vl){ seen[ns][0]=port; seen[ns][1]=cmd; seen[ns][2]=dst; seen[ns][3]=fn; ns++; fprintf(vl,"f%u porta %u cmd=%u dst=%02x n=%u func=%08x\n",frame_no,port,cmd,dst,n,fn); fflush(vl); } }
        uint32_t main_dev=0x20|(port<<6);
        if((port==0||(port==2&&ct2_p3_on)) && dst==main_dev){
            if(cmd==1||cmd==2){   /* pedido de info do dispositivo -> controle padrao */
                w32(rx,5u|(src<<8)|((dst|(port==0&&vmu_on?0x01u:0u)|(port==0&&ct2_rumble()?VIBSLOT:0u))<<16)|(28u<<24)); w32(rx+4,0x01000000u); w32(rx+8,0xFE060F00u); w32(rx+12,0); w32(rx+16,0);
                uint8_t blk[2+30+60+4]; memset(blk,' ',sizeof blk); blk[0]=0xFF; blk[1]=0; memcpy(blk+2,"Dreamcast Controller",20); memcpy(blk+32,"Produced By or Under License From SEGA ENTERPRISES,LTD.",55);
                blk[92]=0xAE; blk[93]=0x01; blk[94]=0xF4; blk[95]=0x01;
                for(int i=0;i<24;i++){ uint32_t v; memcpy(&v,blk+4*i,4); w32(rx+20+4*i,v); }
            } else if(cmd==9){   /* ler estado do controle */
                if(GENV("CT2_MAPLEDBG")){ extern unsigned ta_cur_frame; printf("[maple] f%u pad=%04x\n",ta_cur_frame,(unsigned)pad_buttons); }
                w32(rx,8u|(src<<8)|((dst|(port==0&&vmu_on?0x01u:0u)|(port==0&&ct2_rumble()?VIBSLOT:0u))<<16)|(3u<<24)); w32(rx+4,0x01000000u);
                if(pf){ static uint16_t lr=0xFFFF; static unsigned nl=0; if(pad_buttons!=lr&&nl<6000){ nl++; fprintf(pf,"f%u jogo_leu botoes=%04x (leitura %u)\n",frame_no,pad_buttons,perf_nreads); lr=pad_buttons; perf_win=10; } perf_nreads++; }
                if(port==2&&GENV("CT2_P3")){ static unsigned n3=0; static uint16_t l3=0xFFFF; if(n3<40&&(ct2_p3_btn!=l3||n3<3)){ n3++; l3=ct2_p3_btn; extern unsigned ta_cur_frame; printf("[p3] f%u cmd9 porta C btn=%04x lt=%u\n",frame_no,(unsigned)ct2_p3_btn,(unsigned)ct2_p3_lt); } }
                if(port==2){ w32(rx+8,(uint32_t)ct2_p3_btn|((uint32_t)ct2_p3_rt<<16)|((uint32_t)ct2_p3_lt<<24)); w32(rx+12,128u|(128u<<8)|(128u<<16)|(128u<<24)); }
                else { extern int ta_sens_apply(int); w32(rx+8,(uint32_t)pad_buttons|((uint32_t)pad_rt<<16)|((uint32_t)pad_lt<<24)); w32(rx+12,(uint32_t)ta_sens_apply(pad_jx)|((uint32_t)pad_jy<<8)|(128u<<16)|(128u<<24)); }
            } else { w32(rx,0xFEu|(src<<8)|(dst<<16)); }   /* comando desconhecido */
        } else if(port==0 && dst==VIBSLOT && ct2_rumble()){   /* v15zf: sub-dispositivo 1 da porta A = Jump Pack (funcao de vibracao 0x00010000) */
            if(maple_log<60){ maple_log++; printf("  MAPLE-SUB cmd=%u n=%u w0=%08x w1=%08x\n",cmd,n,fn,n>1?rt_r32(&c,p+16):0); }
            if(cmd==1||cmd==2){
                w32(rx,5u|(src<<8)|(dst<<16)|(28u<<24)); w32(rx+4,0x00010000u); w32(rx+8,0x000F4000u); w32(rx+12,0); w32(rx+16,0);
                uint8_t blk[2+30+60+4]; memset(blk,' ',sizeof blk); blk[0]=0xFF; blk[1]=0; memcpy(blk+2,"Puru Puru Pack",14); memcpy(blk+32,"Produced By or Under License From SEGA ENTERPRISES,LTD.",55);
                blk[92]=0xC8; blk[93]=0x00; blk[94]=0x3E; blk[95]=0x01;
                for(int i=0;i<24;i++){ uint32_t v; memcpy(&v,blk+4*i,4); w32(rx+20+4*i,v); }
            } else if(cmd==3){ w32(rx,8u|(src<<8)|(dst<<16)|(2u<<24)); w32(rx+4,0x00010000u); w32(rx+8,0x00000000u); }
            else if(cmd==11){ w32(rx,8u|(src<<8)|(dst<<16)|(2u<<24)); w32(rx+4,0x00010000u); w32(rx+8,0x00000000u); }
            else {   /* 12 = escrita em bloco, 14 = definir condicao: efeito de vibracao (duracao | efeito2 | efeito1 | especial) */
                if((cmd==12||cmd==14)&&n>=2){ uint32_t e=rt_r32(&c,p+16); unsigned e1=(e>>8)&255, pw=e1&7, pv=(e1>>4)&7; if(pv>pw) pw=pv;   /* bytes: [controle, potencia, frequencia, incremento] (premissa: ordem da documentacao publica do Puru Puru) */ extern unsigned ct2_vib_pow, ct2_vib_stamp; ct2_vib_pow=pw; ct2_vib_stamp=frame_no; { static uint32_t le=0xFFFFFFFF; if((e!=le||cmd==12)&&GENV("CT2_VIBLOG")){ le=e; printf("  VIB f%u cmd=%u efeito=%08x forca=%u\n",frame_no,cmd,e,pw); } } }
                w32(rx,7u|(src<<8)|(dst<<16)); }
        } else if(port==0 && dst==0x01 && vmu_on){   /* v15zu: slot 1 da porta A = VMU (armazenamento 0x02000000 | LCD 0x04000000 | relogio 0x08000000) */
            vmu_load(); uint32_t w1=n>1?rt_r32(&c,p+16):0; unsigned ph=(w1>>8)&255; unsigned blk=(((w1>>16)&255)<<8)|((w1>>24)&255); int stor=(fn==0x02000000u); if(stor&&GENV("CT2_VMUTRACE")){ static int nt=0; if(nt++<400){ extern unsigned ta_cur_frame; printf("[vmu] f%u cmd=%u bloco=%u fase=%u n=%u\n",ta_cur_frame,cmd,blk,ph,n); } }
            if(GENV("CT2_VMULOG")){ static int nl=0; static uint32_t lc=0xFFFF,lf=0xFFFF; if((cmd!=lc||fn!=lf)&&nl++<400){ lc=cmd; lf=fn; printf("  VMU f%u cmd=%u n=%u func=%08x w1=%08x\n",frame_no,cmd,n,fn,w1); } }
            if(cmd==1||cmd==2){
#ifdef RDWATCH
                if(!rt_rd_hi){ rt_rd_lo=rx&0x1FFFFFFFu; rt_rd_hi=rt_rd_lo+0x80; if(getenv("CT2_RDLO")){ rt_rd_lo=strtoul(getenv("CT2_RDLO"),0,16); rt_rd_hi=rt_rd_lo+strtoul(getenv("CT2_RDLEN"),0,16); } printf("[rd] vigiando %08x\n",rt_rd_lo); }
#endif
                w32(rx,5u|(src<<8)|(dst<<16)|(28u<<24)); w32(rx+4,GENV("CT2_VMUFUNC")?(uint32_t)strtoul(GENV("CT2_VMUFUNC"),0,16):0x0E000000u); { uint32_t dd[3]={0x403f7e7eu,0x00100500u,0x00410f00u}; if(GENV("CT2_VMUMEM")) dd[2]=(uint32_t)strtoul(GENV("CT2_VMUMEM"),0,16); int o=GENV("CT2_VMUORD")?atoi(GENV("CT2_VMUORD")):0; static const int perm[6][3]={{0,1,2},{2,1,0},{1,0,2},{2,0,1},{0,2,1},{1,2,0}}; int sw=GENV("CT2_VMUSW")?atoi(GENV("CT2_VMUSW")):0; for(int k=0;k<3;k++){ uint32_t v=dd[perm[o][k]]; w32(rx+8+4*k,(sw==1||(sw==2&&perm[o][k]==2)||(sw==3&&perm[o][k]!=2))?vmu_bs(v):v); } }
                uint8_t blk2[2+30+60+4]; memset(blk2,' ',sizeof blk2); blk2[0]=0xFF; blk2[1]=0; memcpy(blk2+2,"Visual Memory",13); memcpy(blk2+32,"Produced By or Under License From SEGA ENTERPRISES,LTD.",55);
                blk2[92]=0x7C; blk2[93]=0x00; blk2[94]=0x82; blk2[95]=0x00;
                for(int i=0;i<24;i++){ uint32_t v; memcpy(&v,blk2+4*i,4); w32(rx+20+4*i,v); }
            } else if(cmd==10&&stor){   /* informacao da midia */
                w32(rx,8u|(src<<8)|(dst<<16)|(7u<<24)); w32(rx+4,0x02000000u);
                if(GENV("CT2_VMUBS")){ w32(rx+8,vmu_bs((255u<<16)|0u)); w32(rx+12,vmu_bs((255u<<16)|254u)); w32(rx+16,vmu_bs((1u<<16)|253u)); w32(rx+20,vmu_bs((13u<<16)|0u)); w32(rx+24,vmu_bs((0u<<16)|200u)); w32(rx+28,0); }
                else {   /* campos de 16 bits em little-endian (como o hardware): total-1, particao, bloco raiz, FAT, tam FAT, diretorio, tam dir, icone, area de dados (inicio, tamanho) */
                    w32(rx+8,255u|(0u<<16)); w32(rx+12,255u|(254u<<16)); w32(rx+16,1u|(253u<<16)); w32(rx+20,13u|(0u<<16)); w32(rx+24,199u|(200u<<16)); w32(rx+28,0); }
            } else if(cmd==11&&stor){   /* leitura de bloco: 1 fase de 512 bytes */
                if(blk>255) blk=255; w32(rx,8u|(src<<8)|(dst<<16)|(130u<<24)); w32(rx+4,0x02000000u); w32(rx+8,w1&0xFFFFFF00u);
                for(int i=0;i<128;i++){ uint32_t v; memcpy(&v,vmu_fl+blk*512+4*i,4); w32(rx+12+4*i,v); }
            } else if(cmd==12&&stor){   /* escrita de bloco: 4 fases de 128 bytes; a ultima grava no arquivo */
                if(blk>255) blk=255; if(ph<4&&n>=34){ for(int i=0;i<32;i++){ uint32_t v=rt_r32(&c,p+20+4*i); memcpy(vmu_fl+blk*512+ph*128+4*i,&v,4); } vmu_dirty=1; if(ph==3) vmu_flush(); }
                w32(rx,7u|(src<<8)|(dst<<16));
            } else if(cmd==13){ vmu_flush(); w32(rx,7u|(src<<8)|(dst<<16)); }   /* ultimo erro / confirmacao da escrita */
            else if(cmd==9){ w32(rx,8u|(src<<8)|(dst<<16)|(2u<<24)); w32(rx+4,fn); w32(rx+8,0); }
            else if(cmd==12||cmd==14||cmd==3||cmd==11){ w32(rx,7u|(src<<8)|(dst<<16)); }   /* LCD / buzzer / relogio / reset: so confirmo */
            else w32(rx,0xFEu|(src<<8)|(dst<<16));
        } else w32(rx,0xFFFFFFFFu);   /* ninguem nessa porta/subdispositivo */
        if(d0&0x80000000u||++guard>96) break;
        p+=8+4*(n+1);
    }
#ifdef RDWATCH
    in_maple_now_guard=0;
#endif
    if(!GENV("CT2_NOMAPLEIRQ")) istnrm|=1u<<12;   /* teste: fim do DMA Maple (ISTNRM bit 12) */
    /* fim do DMA Maple: nao levanto ISTNRM bit 12 (o jogo nao o mascara; a conclusao aparece em SB_MDST=0) */
}
static uint32_t hw_r(CPU *cc,uint32_t a,int size){
    if(a>=0xE0000000u&&a<0xE4000000u) return sq[(a>>5)&1][(a>>2)&7];
    if(keyof(a)==0x005F6900u){ return istnrm; }
    if(keyof(a)==0xFFD8000Cu){ static unsigned rd=0; rd++; static double mul=-1; if(mul<0){ const char*e=GENV("CT2_TMU"); mul=e?atof(e):1.0; } return 0xFFFFFFFFu-(uint32_t)(ticks*20000.0*mul)-rd*50u; }   /* TMU0 TCNT: conta para baixo; ritmo inventado (premissa) */
    if(keyof(a)==0x005F810Cu){ unsigned vp=ct2_vper(); unsigned ph=(ticks%vp)*16u/vp; return 0x3FFu&(ticks*7) | (ph<2?0x2000u:0) | (ph<2?0x800u:0); }   /* SPG_STATUS: linha, vsync, blank (modelo grosseiro) */
    { uint32_t kk=keyof(a); if(kk>=0x00700000u&&kk<0x00708000u){ return aica_read(kk-0x00700000u,size); } }
    uint32_t k=keyof(a); { unsigned long long *cw,*cr; uint8_t *p=backing(k,&cw,&cr); if(p){ (*cr)++; uint32_t v=0; memcpy(&v,p,size); return v; } } Reg*r=find(k); r->nr++; r->size=size;
    if(k==last_key){ if(++streak>300000){ char b[120]; snprintf(b,sizeof b,"laco de espera: leitura repetida de %s",region(k)); rt_fail(cc,b,a); } } else { last_key=k; streak=1; }
    uint32_t v=r->nw?r->val:preset(k); if(size==1) v&=0xFF; else if(size==2) v&=0xFFFF; logacc('R',a,size,v); return v;
}
int rt_watch_n=0; uint32_t rt_watch[16];
/* depuracao: CT2_RANGE="lo,hi,quadro" imprime a 1a chamada de cada funcao em [lo,hi) a partir do quadro dado (com a pilha curta) */
int rt_rng_on=0; static uint32_t rng_lo,rng_hi; static unsigned rng_from; static uint8_t rng_seen[0x80000];
void rt_rng_hit(struct CPU*cc,uint32_t a){ uint32_t pa=a&0x1FFFFFFFu; extern unsigned ta_cur_frame; if(pa<rng_lo||pa>=rng_hi||ta_cur_frame<rng_from) return; uint32_t o=(pa-0x0C010000u)>>1; if(o>=0x80000||rng_seen[o]) return; rng_seen[o]=1;
    printf("[rng] f%u f_%08x <-",ta_cur_frame,pa); for(int q=cc->sp-1;q>=0&&q>cc->sp-5;q--) printf(" %08x",cc->stk[q]&0x1FFFFFFFu); printf("\n"); }
static void __attribute__((constructor)) rng_init(void){ const char*e=getenv("CT2_RANGE"); if(e){ unsigned lo,hi,f=0; if(sscanf(e,"%x,%x,%u",&lo,&hi,&f)>=2){ rng_lo=lo; rng_hi=hi; rng_from=f; rt_rng_on=1; rt_watch_n=1; rt_watch[0]=0xFFFFFFFFu; } } }
void rt_watch_hit(CPU*cc,uint32_t a,int i){ static unsigned n[16]; if(i<perf_nw){ perf_wc[i]++; return; } if(n[i]++<20||((n[i]&(n[i]-1))==0)) printf("[watch] f%u chamada de f_%08x (r4=%08x r5=%08x r6=%08x) n=%u\n",frame_no,a&0x1FFFFFFFu,cc->r[4],cc->r[5],cc->r[6],n[i]); }
static void hw_w(CPU *cc,uint32_t a,uint32_t v,int size){ (void)cc;
    if(a>=0xE0000000u&&a<0xE4000000u){ sq[(a>>5)&1][(a>>2)&7]=v; return; }
    { static int en=-1; static int nl; if(en<0) en=GENV("CT2_ARAMWR")?atoi(GENV("CT2_ARAMWR")):0; uint32_t k=keyof(a); if(en==4&&frame_no>2000&&k>=0x00800000u&&k<0x00A00000u){ static unsigned long long cn[64][4]; static unsigned lastf; cn[((k&0x1FFFFF)>>16)&31][0]++; if(frame_no>=lastf+500){ lastf=frame_no; printf("[aramwr4] f%u:",frame_no); for(int q=0;q<32;q++) if(cn[q][0]) printf(" %x0000:%llu",q,cn[q][0]); printf("  ult f_%08x\n",c.sp>0&&c.sp<=RT_STK?c.stk[c.sp-1]&0x1FFFFFFFu:0); } }
    if(en==5&&frame_no>2000&&k>=0x009f0000u&&k<0x00a00000u&&nl<40){ nl++; printf("[aramwr3] frame %u %08x=%08x sz%d f_%08x\n",frame_no,k,v,size,c.sp>0&&c.sp<=RT_STK?c.stk[c.sp-1]&0x1FFFFFFFu:0); }
    if(en&&en!=3&&k>=0x00800000u&&k<0x00A00000u&&((k&0x1FFFFF)>=0xd800&&(k&0x1FFFFF)<0xe800||en>1)&&frame_no>20&&(k&0x1FFFFF)<0x20000&&nl<100000){ nl++; printf("[aramwr] frame %u %08x=%08x sz%d em f_%08x",frame_no,k,v,size,c.sp>0&&c.sp<=RT_STK?c.stk[c.sp-1]&0x1FFFFFFFu:0); if((k&0x1FFFFF)>=0x400&&(k&0x1FFFFF)<0x500){ printf(" pilha:"); for(int q=c.sp-1;q>=0&&q>c.sp-8;q--) printf(" %08x",c.stk[q]&0x1FFFFFFFu); } printf("\n"); } }
    { static int en2=-1; if(en2<0) en2=GENV("CT2_AICAWR")!=0; uint32_t k=keyof(a); if(en2&&k>=0x00700000u&&k<0x00710000u&&frame_no>20){ static unsigned cnt[0x4000]; static int nn; unsigned i=(k-0x00700000u)>>2; if(cnt[i]++<2&&nn<200){ nn++; printf("[aicawr] frame %u %08x=%08x sz%d em f_%08x\n",frame_no,k,v,size,c.sp>0&&c.sp<=RT_STK?c.stk[c.sp-1]&0x1FFFFFFFu:0); } } }
    { static int en3=-1; if(en3<0) en3=GENV("CT2_G2LOG")!=0; uint32_t k=keyof(a); if(en3&&k>=0x005F7800u&&k<0x005F7900u) printf("[g2] frame %u %08x=%08x em f_%08x\n",frame_no,k,v,c.sp>0&&c.sp<=RT_STK?c.stk[c.sp-1]&0x1FFFFFFFu:0); }
    if(keyof(a)==0x005F7818u&&(v&1)&&(regv(0x005F7814u)&1)){   /* G2 DMA canal 0 (AICA): RAM do SH4 <-> RAM de som */
        uint32_t ad=regv(0x005F7800u)&0x1FFFFFFFu, sh=regv(0x005F7804u)&0x00FFFFFFu, len=regv(0x005F7808u)&0x7FFFFFFFu, dir=regv(0x005F780Cu)&1;
        uint32_t ao=ad-0x00800000u; if(ad>=0x00800000u&&ao<sizeof aram&&len<=0x200000u&&sh+len<=(16u<<20)){ uint32_t n=len; if(ao+n>sizeof aram) n=(uint32_t)sizeof aram-ao;
            if(dir==0) memcpy(aram+ao,c.ram+sh,n); else memcpy(c.ram+sh,aram+ao,n);
            if(GENV("CT2_G2NZ")){ unsigned nz=0; int mx=0; for(uint32_t q=0;q+1<n;q+=2){ int16_t s; memcpy(&s,aram+ao+q,2); if(s) nz++; if(abs(s)>mx) mx=abs(s);} printf("[g2nz] f%u aram+%x n=%x nonzero=%u max=%d\n",frame_no,ao,n,nz,mx); }
            if(GENV("CT2_G2DUMP")){ for(uint32_t q=0;q<n&&q<0x80;q+=16){ printf("[g2data] +%03x:",q); for(int z=0;z<16;z++) printf(" %02x",aram[ao+q+z]); printf("\n"); } }
            static int ng=0; if(GENV("CT2_G2LOG")&&ng++<200) printf("[g2dma] f%u %s aram+%x <-> ram+%x len=%x\n",frame_no,dir?"A2S":"S2A",ao,sh,n); }
        { Reg*r=find(0x005F7818u); r->nw++; r->val=0; } { Reg*r=find(0x005F7814u); r->nw++; r->val=0; } istnrm|=1u<<15;   /* fim do G2 DMA (AICA) */
        return; }
    if(keyof(a)==0x005F8144u){ ta_reset(); }
    if(keyof(a)==0x005F6C18u&&(v&1)){ Reg*r=find(0x005F6C18u); r->nw++; r->val=0; maple_dma(); return; }
    if(keyof(a)>=0x005F9000u&&keyof(a)<0x005FA000u) pal[((keyof(a)-0x005F9000u)>>2)&1023]=v;
    if(keyof(a)==0x005F8014u){ Reg*r=find(0x005F8014u); r->nw++; r->val=v; start_render(); return; }
    if(keyof(a)>=0x10000000u&&keyof(a)<0x10800000u&&size==4){ ta_feed((const uint8_t*)&v,4); n_taw++; return; }
    if(keyof(a)==0x005F6900u){ istnrm&=~v; return; }
    if(keyof(a)>=0x00700000u&&keyof(a)<0x00708000u){ if(keyof(a)==0x00702C00u&&!(v&1)) n_arm_start++; aica_write(keyof(a)-0x00700000u,v,size); return; }   /* ARM7 liberado: premissa - o driver de som escreveria 1 em aica_ram+0x5C (o jogo espera nonzero ai) */
    if(keyof(a)==0x005F6808u&&(v&1)){ Reg*r=find(0x005F6808u); r->nw++; r->val=v; dma_ch2(); return; }
    uint32_t k=keyof(a); { unsigned long long *cw,*cr; uint8_t *p=backing(k,&cw,&cr); if(p){ (*cw)++; memcpy(p,&v,size); return; } if(k>=0x10000000u&&k<0x11000000u){ n_taw++; return; } } Reg*r=find(k); r->nw++; r->size=size; r->val=v; streak=0; last_key=0; logacc('W',a,size,v);
}
#ifndef _WIN32
static void on_alarm(int s){ (void)s; if(rt_fault_jmp) longjmp(*rt_fault_jmp,2); }
#endif
/* ---- BIOS por HLE: o BIOS real preenche estes vetores na RAM; aqui apontamos para funcoes nativas ---- */
static int nlog=0, maxbios=80; static uint32_t last_sig=0; static unsigned nstreak=0;
static void bios(CPU*cc,const char*nm){
    uint32_t sig=cc->r[4]*31u^cc->r[5]*131u^cc->r[6]*1031u^cc->r[7]^(uint32_t)nm[4]*7919u;
    if(sig==last_sig){ if(++nstreak>100000){ char m[100]; snprintf(m,sizeof m,"laco de espera chamando BIOS %s",nm); rt_fail(cc,m,cc->r[7]); } } else { last_sig=sig; nstreak=1; }
    if(nlog<maxbios){ nlog++; printf("  BIOS %-9s r4=%08x r5=%08x r6=%08x r7=%08x  em f_%08x\n",nm,cc->r[4],cc->r[5],cc->r[6],cc->r[7],cc->sp>0&&cc->sp<=RT_STK?cc->stk[cc->sp-1]&0x1FFFFFFFu:0); }
    cc->r[0]=0;
}
static void b_sysinfo(CPU*cc){ bios(cc,"sysinfo"); }  static void b_romfont(CPU*cc){ bios(cc,"romfont"); }
/* ---- flashrom por HLE (r7 = funcao: 0 info(part,&{off,tam}), 1 leitura(off,dest,n)). Imagem de flash INVENTADA: 128 KB de 0xFF, com cabecalho
   "KATANA_FLASH____" + id da particao + versao 0 no inicio de cada particao (o jogo confere esse cabecalho). Layout das particoes: premissa. ---- */
static uint8_t flash[0x20000]; static int flash_init=0;
static const uint32_t part_off[5]={0x00000,0x02000,0x04000,0x08000,0x10000}, part_sz[5]={0x2000,0x2000,0x4000,0x8000,0x10000};
static void b_flash(CPU*cc){
    if(!flash_init){ memset(flash,0xFF,sizeof flash); for(int i=0;i<5;i++){ memcpy(flash+part_off[i],"KATANA_FLASH____",16); flash[part_off[i]+16]=(uint8_t)i; flash[part_off[i]+17]=0; }
        /* bloco de configuracao do sistema (id 0x05) na particao 2: idioma ingles, estereo, sem autostart. Layout e CRC conforme o formato publicado do flash do Dreamcast (premissa) */
        { uint8_t blk[64]; memset(blk,0,sizeof blk); blk[0]=5; blk[1]=0; blk[5]=0; blk[6]=0; blk[7]=1; blk[8]=1; blk[9]=0;   /* data(2..5), desconhecido(6), idioma(7)=ingles, audio(8)=estereo, autostart(9)=0 */
          int n=0xFFFF; for(int i=0;i<62;i++){ n^=blk[i]<<8; for(int k=0;k<8;k++) n=(n&0x8000)?((n<<1)^4129):(n<<1); } n=(~n)&0xFFFF; blk[62]=(uint8_t)n; blk[63]=(uint8_t)(n>>8);
          for(int k=1;k<=16;k++) memcpy(flash+0x4000+64*k,blk,64); flash[0x7fc0]=0x00; flash[0x7fc1]=0x00; }   /* o jogo conta os bits 0 iniciais do mapa = blocos alocados (observado em f_0c15e804) */
        flash_init=1; }
    uint32_t fn=cc->r[7], a=cc->r[4], p=cc->r[5], n=cc->r[6], ret=0;
    if(nlog<maxbios){ nlog++; printf("  BIOS flashrom fn=%u r4=%08x r5=%08x r6=%08x\n",fn,a,p,n); }
    if(GENV("CT2_FLASHSTK")){ static int ns=0; if(ns++<4){ printf("  [pilha flash]"); for(int i=cc->sp-1;i>=0&&i>cc->sp-7;i--) printf(" f_%08x",cc->stk[i]&0x1FFFFFFFu); printf("\n"); } }
    if(fn==0){ if(a<5){ rt_w32(cc,p,part_off[a]); rt_w32(cc,p+4,part_sz[a]); } else ret=(uint32_t)-1; }
    else if(fn==1){ if(a+(uint64_t)n<=sizeof flash){ for(uint32_t i=0;i<n;i++) rt_w8(cc,p+i,flash[a+i]); } else ret=(uint32_t)-1; }
    else ret=(uint32_t)-1;
    cc->r[0]=ret;
}   /* ---- GD-ROM por HLE (convencao do BIOS: r7 = funcao; premissa baseada em documentacao de memoria, nao verificada aqui):
   0 req_cmd(cmd,param)->id | 1 cmd_stat(id,status[4]) | 2 exec_server | 3 init | 4 drive_stat(param[2]) | 8 abort | 9 reset | 10 set_data_type */
static FILE *trk=0; static unsigned gd_id=0, gd_pending=0; static uint32_t gd_bytes=0, gd_err=0; static unsigned long long gd_reads=0, gd_sectors=0;
static int gd_log=0;
static void gd_read(uint32_t lba,uint32_t n,uint32_t buf){
    uint32_t phys=buf&0x1FFFFFFFu; gd_bytes=0; gd_err=0; gd_reads++;
    if(!trk||phys<0x0C000000u||phys+n*2048ull>0x0C000000u+RAM_SIZE){ gd_err=1; if(GENV("CT2_GDLOG")) printf("GDREAD FALHA faixa lba=%u n=%u buf=%08x\n",lba,n,buf); return; }
    for(uint32_t i=0;i<n;i++){
        long long off=((long long)lba+i-45150)*2352+16;   /* FAD = LBA do GDI + 150 */ uint8_t *d=c.ram+(phys-0x0C000000u)+i*2048u;
        if(off<0||fseek(trk,(long)off,SEEK_SET)||fread(d,1,2048,trk)!=2048){ gd_err=1; if(GENV("CT2_GDLOG")) printf("GDREAD FALHA leitura lba=%u (+%u) off=%lld\n",lba,i,off); return; }
        gd_bytes+=2048; gd_sectors++;
    }
    if(GENV("CT2_GDLOG")) printf("GDREAD f%u lba=%u n=%u buf=%08x err=%u\n",frame_no,lba,n,buf,gd_err);
}
static void b_gdrom(CPU*cc){
    uint32_t fn=cc->r[7], a=cc->r[4], p=cc->r[5], ret=0;
    if(gd_log<60){ gd_log++; printf("  GDROM fn=%u r4=%08x r5=%08x\n",fn,a,p); }
    { static int gl=-1; static unsigned gn=0; if(gl<0) gl=GENV("CT2_GDCMD")!=0; if(gl&&frame_no>=2040&&(fn==0||frame_no<2043)&&gn++<6000){ printf("[gdcmd] f%u fn=%u r4=%08x r5=%08x",frame_no,fn,a,p); if(fn==0&&p){ printf(" p0=%08x p1=%08x p2=%08x p3=%08x",rt_r32(cc,p),rt_r32(cc,p+4),rt_r32(cc,p+8),rt_r32(cc,p+12)); } printf("\n"); } }
    switch(fn){
    case 0: { gd_id++; gd_pending=gd_id; ret=gd_id;
        if(a==16||a==17){ uint32_t lba=rt_r32(cc,p),n=rt_r32(cc,p+4),buf=rt_r32(cc,p+8); gd_read(lba,n,buf); if(a==17&&!gd_err) gd_dma_at=ticks+2;   /* fim do DMA do GD-ROM (ISTNRM bit 14), 2 relogios depois do pedido */
            if(gd_log<80) printf("    leitura LBA %u, %u setores -> %08x %s\n",lba,n,buf,gd_err?"FALHOU":"ok"); }
        else if(a==19||a==18){ uint32_t buf=rt_r32(cc,p+4); gd_bytes=408; gd_err=0;
            for(int i=0;i<99;i++) rt_w32(cc,buf+4*i,0xFFFFFFFFu);
            rt_w32(cc,buf+4*2,0x41000000u|45150u);            /* faixa 3: dados, FAD 45150 (premissa: area de alta densidade) */
            rt_w32(cc,buf+4*99,0x41030000u); rt_w32(cc,buf+4*100,0x41030000u); rt_w32(cc,buf+4*101,0x41000000u|549300u);
            if(gd_log<80) printf("    TOC (area %u) escrito em %08x\n",rt_r32(cc,p),buf); }
        else { gd_bytes=0; gd_err=0; if(gd_log<80) printf("    comando GD %u (param %08x) aceito sem efeito\n",a,p); }
        break; }
    case 1: { uint32_t s=p; if(s){ rt_w32(cc,s,gd_err); rt_w32(cc,s+4,0); rt_w32(cc,s+8,gd_bytes); rt_w32(cc,s+12,0); }
        ret = gd_pending? (gd_err?(uint32_t)-1:2u) : 0u; gd_pending=0; break; }
    case 4: if(a){ rt_w32(cc,a,1); rt_w32(cc,a+4,0x80); } ret=0; break;   /* status 1 = pausado/pronto, tipo 0x80 = GD-ROM */
    default: ret=0; }
    cc->r[0]=ret;
}
static void b_misc(CPU*cc){ bios(cc,"misc"); }
/* v15zz: acessores de 16 bits / copia de palavras da biblioteca de backup (objeto em 0c2c6f74, tabela de funcoes por ordem de bytes). Ficam em ponteiros de funcao no objeto, entao
   o recompilador nao os achou (o jogo caia em "salto sem funcao recompilada" e lia 0 em toda leitura da resposta do cartao). Implementados aqui. */
static void n_rd16sw(CPU*cc){ uint32_t v=rt_r32(cc,cc->r[4]+cc->r[5])&0xFFFFu; (void)v; uint32_t a=cc->r[4]+cc->r[5]; uint32_t x=(rt_r8(cc,a)&0xFFu)|((rt_r8(cc,a+1)&0xFFu)<<8); cc->r[1]=x; cc->r[0]=((x&0xFFu)<<8)|(x>>8); }
static void n_rd16(CPU*cc){ uint32_t a=cc->r[4]+cc->r[5]; uint32_t x=(rt_r8(cc,a)&0xFFu)|((rt_r8(cc,a+1)&0xFFu)<<8); cc->r[1]=x; cc->r[0]=x; }
static void n_wr16sw(CPU*cc){ uint32_t a=cc->r[4]+cc->r[5], v=cc->r[6]; v=((v&0xFFu)<<8)|((v>>8)&0xFFu); rt_w8(cc,a,v&0xFF); rt_w8(cc,a+1,(v>>8)&0xFF); cc->r[6]=v; }
static void n_wr16(CPU*cc){ uint32_t a=cc->r[4]+cc->r[5], v=cc->r[6]; rt_w8(cc,a,v&0xFF); rt_w8(cc,a+1,(v>>8)&0xFF); }
static void n_cpw(CPU*cc){ uint32_t n=cc->r[6]>>2; uint32_t d=cc->r[4], s=cc->r[5]; for(uint32_t i=0;i<n;i++){ rt_w32(cc,d,rt_r32(cc,s)); d+=4; s+=4; } cc->r[4]=d; cc->r[5]=s; cc->r[6]=0; }
static void setup_bios(void){
    rt_native_register(0x0c16857cu,n_rd16sw); rt_native_register(0x0c168586u,n_rd16); rt_native_register(0x0c16858eu,n_wr16sw); rt_native_register(0x0c168596u,n_wr16); rt_native_register(0x0c16856cu,n_cpw);
    static const struct { uint32_t vec, fake; RtNative f; } v[]={{0x8C0000B0u,0x00001010u,b_sysinfo},{0x8C0000B4u,0x00001020u,b_romfont},{0x8C0000B8u,0x00001030u,b_flash},{0x8C0000BCu,0x00001040u,b_gdrom},{0x8C0000E0u,0x00001050u,b_misc}};
    for(unsigned i=0;i<5;i++){ rt_w32(&c,v[i].vec,v[i].fake); rt_native_register(v[i].fake,v[i].f); }
}
static int cmpreg(const void*x,const void*y){ const Reg*a=x,*b=y; return a->order<b->order?-1:a->order>b->order; }
int main(int argc,char**argv){
    if(argc<2){ fprintf(stderr,"uso: %s 1ST_READ.BIN [IP.BIN] [max_log] [segundos] [track03.bin] [max_relogios]\n",argv[0]); return 2; }
    int secs=argc>4?atoi(argv[4]):5; if(argc>3) maxlog=atoi(argv[3]);
    c.ram=calloc(1,RAM_SIZE);
    FILE*f=fopen(argv[1],"rb"); if(!f){ perror(argv[1]); return 2; } size_t n=fread(c.ram+0x10000,1,RAM_SIZE-0x10000,f); fclose(f);
    printf("1ST_READ.BIN: %zu bytes em 0x8C010000\n",n);
    if(argc>2 && strcmp(argv[2],"-")){ f=fopen(argv[2],"rb"); if(f){ size_t m=fread(c.ram+0x8000,1,0x8000,f); fclose(f); printf("IP.BIN: %zu bytes em 0x8C008000 (premissa: o BIOS o carregaria ai)\n",m);} }
    if(argc>5){ trk=fopen(argv[5],"rb");
#ifdef CT2_WINDOW
        /* v15zzn: caminho com acento/Unicode: o argv do console vira ANSI e perde caracteres; reabre pela linha de comando ampla (win.c) */
        if(!trk){ extern FILE *ct2_open_arg_w(int idx); trk=ct2_open_arg_w(5); }
        if(!trk){ extern FILE *ct2_open_ini_track(void); trk=ct2_open_ini_track(); }
#endif
        if(!trk){ fprintf(stderr,"ERRO: nao consegui abrir a trilha de dados do disco: %s\n(o jogo precisa dela para ler os arquivos; confira o caminho do track03.bin)\n",argv[5]); printf("ERRO: nao consegui abrir a trilha de dados do disco: %s\n",argv[5]); return 2; } }
    if(argc>6) max_ticks=(unsigned)atoi(argv[6]);
    aica_init(aram);
    setup_bios();
    memset(c.ram+0x7C,0xFF,4);   /* cache de configuracao do SDK (idioma/audio em 0x8C00007D/7E): 0xFF = ainda nao lido do flash (premissa; com 0 o jogo assume japones) */
    if(!GENV("CT2_WATCH")){ rt_watch[0]=0x0c149a1eu; rt_watch[1]=0x0c01a1d4u; rt_watch[2]=0x0c033b1eu; rt_watch_n=3; perf_nw=3; }
    { const char*w=GENV("CT2_WATCH"); while(w&&*w&&rt_watch_n<16){ rt_watch[rt_watch_n++]=(uint32_t)strtoul(w,(char**)&w,16); if(*w==',') w++; } }
    c.r[15]=0x8C00F400u; c.fpscr=0; c.vbr=0x8C000000u; ta_eol_cb=eol_cb; rt_pref_cb=pref_cb; { const char*e=GENV("CT2_FRAMES"); if(e) frames_dir=e; } rt_tick_cb=tick; rt_hw_read_cb=hw_r; rt_hw_write_cb=hw_w;
#ifndef _WIN32
    signal(SIGALRM,on_alarm);
#endif
    printf("\n--- acessos a hardware (primeiros %d) e chamadas ao BIOS ---\n",maxlog);
    int rc;
    rt_fault_jmp=&jb;
#ifdef CT2_WINDOW
    win_audio_start(aica_render);
    { extern int win_splash_all(void); if(!getenv("CT2_NOSPLASH")) win_splash_all(); }   /* v15zz: NOW LOADING + distribuidora (intro_*.ppm) */
#endif
    if((rc=setjmp(jb))==0){
#ifndef _WIN32
        if(secs>0) alarm(secs);
#endif
        rt_call(&c,0x8C010000u);
        printf("\nO boot RETORNOU normalmente (inesperado).\n");
    } else {
        printf("\n=== PAROU: %s ===\n", rc==2?"tempo esgotado (laco sem acesso a hardware)":rt_fail_msg);
        if(rc!=2) printf("endereco: 0x%08x\n",rt_fail_addr);
    }
    rt_fault_jmp=0;
    printf("pilha de chamadas (da funcao mais interna para fora):\n");
    for(int i=c.sp-1,k=0;i>=0&&k<10;i--,k++) printf("  f_%08x\n",i<RT_STK?c.stk[i]&0x1FFFFFFFu:0);
    printf("VBR=0x%08x ; ponteiros nao nulos em [VBR+0x100, VBR+0x600):\n",c.vbr);
    for(uint32_t o=0x100;o<0x600;o+=4){ uint32_t v=rt_r32(&c,c.vbr+o); if(v) printf("  VBR+0x%03x = 0x%08x\n",o,v); }
    aica_stats();
    printf("\nSQ: %llu descargas (%llu para o TA) | fins de lista: %llu/%llu/%llu/%llu/%llu\n",n_sqflush,n_sqta,n_eol[0],n_eol[1],n_eol[2],n_eol[3],n_eol[4]);
    printf("\nMaple: %llu transferencias DMA\n",n_maple);
    printf("\nQuadros (STARTRENDER): %llu, salvos: %u, maximo de triangulos num quadro salvo: %u\n",frames_total,saved_frames,max_tris);
    printf("\nGD-ROM: %llu leituras, %llu setores\n",gd_reads,gd_sectors);
    printf("\ninterrupcoes entregues: %llu | DMA canal 2: %llu transferencias, %llu bytes | relogios: %u | ISTNRM pendente: %08x\n",n_irq,n_dma,n_dmabytes,ticks,istnrm);
    printf("\nVRAM: %llu escritas, %llu leituras | RAM de audio: %llu escritas, %llu leituras | palavras enviadas ao TA: %llu\n",n_vw,n_vr,n_aw,n_ar,n_taw);
    printf("\n--- registradores de hardware tocados: %d ---\n",nused);
    static Reg out[NREG]; int m=0; for(int i=0;i<NREG;i++) if(regs[i].used) out[m++]=regs[i];
    qsort(out,m,sizeof(Reg),cmpreg);
    for(int i=0;i<m;i++) printf("  %08x  %-13s  lido %6ux  escrito %6ux  ultimo=%08x  1a vez em f_%08x\n",out[i].key,region(out[i].key),out[i].nr,out[i].nw,out[i].val,out[i].first_fn&0x1FFFFFFFu);
    return 0;
}

#ifdef MEMWATCH
uint32_t rt_mw[8]; int rt_mw_n=0;
void rt_mw_hit(CPU*cc,uint32_t a,uint32_t v){ extern unsigned ta_cur_frame; static unsigned n=0; if(n++>60) return; float f; memcpy(&f,&v,4); printf("[mw] f%u w[%08x]=%08x (%g) pc-stack:",ta_cur_frame,a,v,f); for(int q=cc->sp-1;q>=0&&q>cc->sp-7;q--) printf(" %08x",cc->stk[q]&0x1FFFFFFFu); printf("\n"); }
__attribute__((constructor)) static void mw_init(void){ const char*e=getenv("CT2_MW"); while(e&&*e&&rt_mw_n<8){ rt_mw[rt_mw_n++]=(uint32_t)strtoul(e,(char**)&e,16); if(*e==',') e++; } }
#endif

#ifdef FNTRACE
uint32_t rt_mw_a=0xFFFFFFFFu; void rt_mw_hit(CPU*c,uint32_t a,uint32_t v){ static unsigned n=0; if((getenv("CT2_MWALL")||(frame_no>=4398&&frame_no<=4420))&&n++<80){ printf("[mw] f%u w32 %08x=%08x stk:",frame_no,a,v); for(int q=c->sp-1;q>=0&&q>c->sp-8;q--) printf(" %08x",c->stk[q]&0x1FFFFFFFu); printf("\n"); fflush(stdout);} }
uint32_t rt_trace_stk=0xFFFFFFFFu; static void __attribute__((constructor)) stk_init(void){ const char*e=getenv("CT2_FNSTK"); if(e) rt_trace_stk=(uint32_t)strtoul(e,0,16); e=getenv("CT2_MW1"); if(e) rt_mw_a=(uint32_t)strtoul(e,0,16)&0x1FFFFFFCu; }
int rt_trace_on=0; unsigned char rt_trace_map[0x80000];
void fntrace_frame(unsigned f){ static int init=0,a=0,bb=0; if(!init){ init=1; const char*e=getenv("CT2_FNTRACE"); if(e) sscanf(e,"%d-%d",&a,&bb); }
  if(!bb) return; if((int)f==a) rt_trace_on=1; if((int)f==bb+1&&rt_trace_on){ rt_trace_on=0; FILE*o=fopen(getenv("CT2_FNOUT")?getenv("CT2_FNOUT"):"/tmp/fntrace.txt","w"); for(unsigned i=0;i<0x80000;i++) if(rt_trace_map[i]) fprintf(o,"%08x\n",0x0C010000u+i*2); fclose(o); } }
#endif
int ct2_cam_mode=0,ct2_cam_sub=0;
#ifdef RDWATCH
int in_maple_now_guard=0;
#endif

int ct2_cam_force=-1;

#ifdef RDWATCH
uint32_t rt_rd_lo=0xFFFFFFFFu, rt_rd_hi=0;
void rt_rd_hit(CPU*cc,uint32_t a,int w){ extern unsigned ta_cur_frame; static uint32_t seen[512][3]; static int ns=0; if(in_maple_now_guard) return; uint32_t top=cc->sp>0?(cc->stk[cc->sp-1]&0x1FFFFFFFu):0; for(int i=0;i<ns;i++) if(seen[i][0]==a&&seen[i][1]==top) return; if(ns>=512) return; seen[ns][0]=a; seen[ns][1]=top; ns++; printf("[rd] f%u read%d %08x stk:",frame_no,w,a); for(int q=cc->sp-1;q>=0&&q>cc->sp-8;q--) printf(" %08x",cc->stk[q]&0x1FFFFFFFu); printf("\n"); fflush(stdout); }
static void __attribute__((constructor)) rd_init(void){ const char*e=getenv("CT2_RDLO"); if(e){ rt_rd_lo=(uint32_t)strtoul(e,0,16); rt_rd_hi=rt_rd_lo+(uint32_t)strtoul(getenv("CT2_RDLEN")?getenv("CT2_RDLEN"):"4",0,16); } }
#endif

/* v15zzi: widescreen. O jogo descarta objetos inteiros cujo centro projeta alem de uma extensao horizontal (0x8c2bde50 = 717 px em jogo, lida pela rotina de desenho 0c080e80; fora disso predios/pedestres sumiam nas bordas do widescreen mesmo com o 3D desenhado alem dos 640). Aqui essa extensao e multiplicada em widescreen (k = 1+6*(largura/640-1), no maximo 8; 16:9 = 3x, testado no harness: diferenca para "sem descarte" cai de ~87% para ~2%). A rotina de camera (0c0747b0 / 0c074a30) regrava o valor quando muda a cena: detectamos pelo valor e partimos do original de novo. */
float ta_wide_cull=1.0f;
void ct2_cull_apply(CPU*c){ static float mine=0, orig=0; float k=ta_wide_cull; if(GENV("CT2_NOCULLFIX")) k=1.f; float v; memcpy(&v,c->ram+0x2bde50,4); if(!(v>50.f&&v<1e5f)) return; if(!(mine!=0&&v==mine)) orig=v; float nv=orig*k; memcpy(c->ram+0x2bde50,&nv,4); mine=nv;
    if(GENV("CT2_CULLDBG")){ static float lk=0; static int q=0; if(k!=lk&&q++<30){ lk=k; printf("[cull] f%u k=%.3f ext %.1f -> %.1f\n",frame_no,k,orig,nv); } } }
