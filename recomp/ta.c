/* Decodificador da lista de comandos do TA (Tile Accelerator) + rasterizador por software, so para ENXERGAR o que o jogo desenha.
   Formatos conforme o conhecimento publico do PVR2; NAO verificado contra hardware aqui: onde ha premissa, esta comentado.
   Nao e fiel ao PVR (sem tiles, sem ordenacao de translucidos, filtros ignorados): serve para validar a engenharia reversa. */
#ifndef GENV
#define GENV(n) ({ static int _gi=0; static const char *_gv=0; if(!_gi){ _gv=getenv(n); _gi=1; } _gv; })   /* v15o: getenv() em laco quente custava ms por quadro no Windows */
#endif
#include "ta.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#ifdef _WIN32
#define COBJMACROS
#include <windows.h>
#else
#include <pthread.h>
#include <unistd.h>
#endif
#define W 640
#define H 480
/* v15x: resolucao interna variavel (UPSCALE) e tela larga: o quadro interno tem RW x RH pixels */
#define FBMAX (3840*2160)
static int cap_w=1707, cap_h=960;   /* limite do quadro interno: software 1707x960; GPU ate 4K */
static int OW=640, OH=480; static int RW=640, RH=480; static float RS=1.f, RBX=640.f;   /* RS = escala interna; RBX = largura base (640 ou 853 no modo tela larga) */
/* v15zz: duas resolucoes: o 3D usa a escala do UPSCALE; menus/HUD sempre na maior escala do renderizador por software (nitidos como a 200%) */
static int all_ui_f=0; static int dual_on=0, DFW=640, DFH=480, aa_skip=0; static float SOUT=1.f;
int ta_upscale=100, ta_aspect=0, ta_res_i=2, ta_aa_mode=2, ta_fb_w=640, ta_fb_h=480, ta_disp_gen=1;
const int ta_res_tab[10][2]={{640,480},{800,600},{1024,768},{1280,720},{1280,960},{1600,900},{1600,1200},{1920,1080},{2560,1440},{3840,2160}};
static uint8_t *buf; static size_t blen=0, bcap=0;
static size_t scan_o=0; static int scan_sprite=0, scan_list=-1; void (*ta_eol_cb)(int list)=0;
static int dumped=0; static uint32_t U32(size_t o);
void ta_reset(void){
    if(!dumped && blen>3000){ dumped=1; printf("--- amostra da lista do TA (%zu bytes) ---\n",blen); size_t o=0; for(int k=0;k<70&&o+32<=blen;k++){ uint32_t pcw=U32(o); int ty=(pcw>>29)&7; printf("  +%05zx pcw=%08x tipo=%d lista=%d  [%08x %08x %08x %08x %08x %08x %08x]\n",o,pcw,ty,(pcw>>24)&7,U32(o+4),U32(o+8),U32(o+12),U32(o+16),U32(o+20),U32(o+24),U32(o+28)); o+=32; } }
    blen=0; scan_o=0; scan_sprite=0; scan_list=-1; }
static uint32_t U32(size_t o){ uint32_t v; memcpy(&v,buf+o,4); return v; }
static float F32(size_t o){ float v; memcpy(&v,buf+o,4); return v; }
/* varredura incremental: so para detectar fim de lista (gera a interrupcao "EoT lista") */
static void scan(void){
    while(blen-scan_o>=32){
        uint32_t pcw=U32(scan_o); int type=(pcw>>29)&7; size_t sz=32;
        if(type==4){ int col=(pcw>>4)&3, offs=(pcw>>2)&1; scan_sprite=0; scan_list=(pcw>>24)&7; if(col==2&&offs) sz=64; }
        else if(type==5){ scan_sprite=1; scan_list=(pcw>>24)&7; }
        else if(type==7 && scan_sprite) sz=64;
        if(blen-scan_o<sz) break;
        scan_o+=sz;
        if(type==0){ int l=scan_list>=0?scan_list:(int)((pcw>>24)&7); scan_list=-1; if(ta_eol_cb) ta_eol_cb(l); }   /* o EOL nao carrega o tipo de lista: vale a lista aberta pelo ultimo cabecalho */
    }
}
void ta_feed(const uint8_t *p, size_t n){
    if(blen+n>bcap){ bcap=(blen+n)*2+65536; buf=realloc(buf,bcap); }
    memcpy(buf+blen,p,n); blen+=n; scan();
}
typedef struct { float x,y,z,u,v,r,g,b,a; } Vtx;
typedef struct { Vtx v[3]; int list; uint32_t tsp,tex; int textured,uv16; int col; const void *tc; uint32_t isp; int snap; int clampuv; int shadow; float cu0,cu1,cv0,cv1; double zc,zx,zy,uc,ux,uy,vc,vx,vy; } Tri;
static uint32_t lastscene[FBMAX]; static int ls_w=640, ls_h=480; static int have_scene=0, snap_used=0;
static uint32_t cur_isp=0xA0000000u;
static int cur_col=0; static int cur_shadow=0;
typedef struct { double x[3],y[3],z[3]; int mesh, last; } MV; static int cur_mesh=0; static MV *mvs; static size_t nmv=0, capmv=0;   /* v15zf: volumes modificadores (sombras) */
static uint8_t shb[FBMAX+64], par[FBMAX+64], acc[FBMAX+64]; float ta_shad=0.5f, ta_shtol=0.0001f;
static Tri *tris; static size_t ntri=0, captri=0;
static void addtri(const Vtx*a,const Vtx*b,const Vtx*c,int list,uint32_t tsp,uint32_t tex,int textured,int uv16){
    if(ntri>=captri){ captri=captri*2+4096; tris=realloc(tris,captri*sizeof *tris); }
    Tri*t=&tris[ntri++]; t->v[0]=*a; t->v[1]=*b; t->v[2]=*c; t->list=list; t->tsp=tsp; t->tex=tex; t->textured=textured; t->uv16=uv16; t->col=cur_col; t->isp=cur_isp; t->snap=0; t->shadow=cur_shadow;
}
extern unsigned ta_cur_frame;
#include "menux.inc"
static void unpack(uint32_t c,Vtx*v){ v->a=(c>>24)/255.f; v->r=((c>>16)&255)/255.f; v->g=((c>>8)&255)/255.f; v->b=(c&255)/255.f; }
static float half2f(uint32_t h16){ uint32_t b=h16<<16; float f; memcpy(&f,&b,4); return f; }   /* UV de 16 bits = metade alta de um float */
/* --- texturas --- */
static int twswap=-1;
static uint32_t twid(uint32_t x,uint32_t y){ if(twswap<0){ const char*e=GENV("CT2_TWID"); twswap=e?atoi(e):0; } if(twswap){ uint32_t t=x; x=y; y=t; } uint32_t r=0; for(int i=0;i<10;i++) r|=(((y>>i)&1)<<(2*i))|(((x>>i)&1)<<(2*i+1)); return r; }   /* premissa: y nos bits pares, x nos impares */
/* twiddle de textura retangular: Morton dentro de blocos min x min; os bits altos da dimensao maior ficam lineares (premissa: comportamento do hardware) */
static uint32_t twid2(uint32_t x,uint32_t y,int sw,int sh){
    if(sw==sh) return twid(x,y);
    if(sw>sh){ uint32_t m=(uint32_t)sh; return (x/m)*m*m+twid(x%m,y); }
    uint32_t m=(uint32_t)sw; return (y/m)*m*m+twid(x,y%m);
}
static uint32_t px16(uint16_t p,int fmt){
    uint32_t a,r,g,b;
    if(fmt==0){ a=(p>>15)?255:0; r=((p>>10)&31)*255/31; g=((p>>5)&31)*255/31; b=(p&31)*255/31; }
    else if(fmt==1){ a=255; r=((p>>11)&31)*255/31; g=((p>>5)&63)*255/63; b=(p&31)*255/31; }
    else { a=((p>>12)&15)*17; r=((p>>8)&15)*17; g=((p>>4)&15)*17; b=(p&15)*17; }
    return a<<24|r<<16|g<<8|b;
}
static uint32_t texel_slow(const uint8_t *vram,const uint32_t*pal,uint32_t palctl,uint32_t tex,uint32_t tsp,float u,float v){
    int fmt=(tex>>27)&7, vq=(tex>>30)&1, mip=(tex>>31)&1, nontw=(tex>>26)&1; if(fmt>=5) nontw=0;   /* paletizadas: sempre twiddled; o bit 26 vira seletor de paleta */
    int sw=8<<((tsp>>3)&7), sh=8<<(tsp&7);   /* U = bits 5-3, V = bits 2-0 */
    int x=(int)floorf(u*sw), y=(int)floorf(v*sh); int clampv=(tsp>>15)&1, clampu=(tsp>>16)&1, flipv=(tsp>>17)&1, flipu=(tsp>>18)&1;   /* TSP: ClampV=15 ClampU=16 FlipV=17 FlipU=18 */
    if(clampu){ if(x<0)x=0; if(x>=sw)x=sw-1; } else if(flipu){ x&=2*sw-1; if(x>=sw) x=2*sw-1-x; } else x&=sw-1;
    if(clampv){ if(y<0)y=0; if(y>=sh)y=sh-1; } else if(flipv){ y&=2*sh-1; if(y>=sh) y=2*sh-1-y; } else y&=sh-1;
    uint32_t base=(tex&0x1FFFFF)*8; size_t off=0;
    int bpp=(fmt==5)?4:(fmt==6)?8:16;
    if(mip && !vq && sw==sh){ off=6; for(int s=1;s<sw;s<<=1) off+=s*s*2; if(bpp==8) off/=2; else if(bpp==4) off/=4; }   /* paletizadas: metade / um quarto do deslocamento de 16 bpp */
    if(mip && vq && sw==sh){ off=0; for(int s=2;s<sw;s<<=1) off+=(s/2)*(s/2); off+=1; off+=2048*0; }   /* premissa grosseira para VQ+mipmap */
    { size_t need=vq?(size_t)2048+(size_t)sw*sh/4:(fmt==3?(size_t)sw*sh*2:(size_t)sw*sh*bpp/8); if((size_t)base+off+need>(16u<<20)) return 0xFFFF00FF; }
    const uint8_t *d=vram+base;
    if(vq){ const uint8_t *cb=d; const uint8_t *idx=d+2048+off; uint32_t bi=twid2(x>>1,y>>1,sw>>1,sh>>1); uint8_t ci=idx[bi]; uint16_t p; memcpy(&p,cb+ci*8+(((x&1)?2:0)+((y&1)?1:0))*2,2); return px16(p,fmt); }
    if(fmt==3){   /* YUV422: 4 bytes = [U Y0 V Y1] para 2 pixels (premissa: ordem conforme emuladores conhecidos) */
        uint32_t i=nontw?(uint32_t)(y*(sw/2)+(x>>1)):twid2(x>>1,y,sw/2,sh); uint32_t w; memcpy(&w,d+off+i*4,4);
        int U=(int)(w&255)-128, Y=(int)((w>>((x&1)?24:8))&255), V=(int)((w>>16)&255)-128;
        int r=(int)(Y+1.402f*V), g=(int)(Y-0.344f*U-0.714f*V), b=(int)(Y+1.772f*U); if(r<0)r=0; if(r>255)r=255; if(g<0)g=0; if(g>255)g=255; if(b<0)b=0; if(b>255)b=255;
        return 0xFF000000u|((uint32_t)r<<16)|((uint32_t)g<<8)|(uint32_t)b; }
    if(bpp==16){ uint32_t i=nontw?(uint32_t)(y*sw+x):twid2(x,y,sw,sh); uint16_t p; memcpy(&p,d+off+i*2,2); if(fmt==4) return 0xFF808080; return px16(p,fmt); }
    if(bpp==8){ uint32_t i=nontw?(uint32_t)(y*sw+x):twid2(x,y,sw,sh); uint8_t q=d[off+i]; uint32_t pi=(((tex>>21)&0x30)<<4)+q; /* premissa: seletor de paleta bits 26-25 */ uint32_t e=pal[pi&1023]; return palctl==3?e:px16((uint16_t)e,palctl==0?0:palctl==1?1:2); }
    { uint32_t i=nontw?(uint32_t)(y*sw+x):twid2(x,y,sw,sh); uint8_t q=d[off+(i>>1)]; q=(i&1)?(q>>4):(q&15); uint32_t pi=((tex>>21)&0x3F)*16+q; uint32_t e=pal[pi&1023]; return palctl==3?e:px16((uint16_t)e,palctl==0?0:palctl==1?1:2); }
}
/* cache de texturas decodificadas, persistente entre quadros; cada uso valida uma assinatura barata do conteudo na VRAM */
typedef struct { uint32_t tex,sz,pc,sig,seen; uint32_t *d; int sw,sh; uint32_t *mm; uint32_t *mp[12]; int nl; size_t mmcap; void *gtex,*gsrv; uint32_t gen,ggen; int gw,gh; } TC;
#define NTC 2048
static TC tcache[NTC]; static uint32_t tframe=1;
static size_t tc_bytes(uint32_t tex,uint32_t tsp){ int sw=8<<((tsp>>3)&7), sh=8<<(tsp&7); int fmt=(tex>>27)&7, vq=(tex>>30)&1, mip=(tex>>31)&1; size_t n=(size_t)sw*sh*((fmt==5)?1:(fmt==6)?1:2); if(fmt==5) n=(size_t)sw*sh/2; if(vq) n=2048+(size_t)sw*sh/4; if(mip) n+=n/3+64; return n; }
static uint32_t tc_sig(const uint8_t*vram,uint32_t tex,uint32_t tsp,uint32_t palsig){
    size_t base=(size_t)(tex&0x1FFFFF)*8, n=tc_bytes(tex,tsp); if(base+n>(16u<<20)) n=(16u<<20)-base;
    uint32_t h=2166136261u^palsig; size_t step=n>4096?(n/4096)&~3u:4; if(step<4) step=4;
    for(size_t i=0;i<n;i+=step){ uint32_t w; memcpy(&w,vram+base+i,4); h=(h^w)*16777619u; } h=(h^(uint32_t)n)*16777619u; return h; }
static uint32_t pal_sig(const uint32_t*pal,uint32_t tex,uint32_t palctl){ int fmt=(tex>>27)&7; if(fmt<5) return palctl*131u; uint32_t h=palctl*7919u; int first=(fmt==5)?((tex>>21)&0x3F)*16:(((tex>>21)&0x30)<<4), cnt=(fmt==5)?16:256; for(int i=0;i<cnt;i++) h=(h^pal[(first+i)&1023])*16777619u; return h; }
static const TC *tc_get(const uint8_t*vram,const uint32_t*pal,uint32_t palctl,uint32_t tex,uint32_t tsp){
    int sw=8<<((tsp>>3)&7), sh=8<<(tsp&7); uint32_t sz=tsp&0x3F;
    uint32_t h=((tex*2654435761u)^(sz*40503u))>>21&(NTC-1); TC*e=0;
    for(int k=0;k<16;k++){ TC*c=&tcache[(h+k)&(NTC-1)]; if(c->d&&c->tex==tex&&c->sz==sz){ e=c; break; } if(!c->d&&!e) e=c; }
    if(!e) e=&tcache[h];
    uint32_t psig=pal_sig(pal,tex,palctl), sig;
    static int tcoff=-1; if(tcoff<0) tcoff=GENV("CT2_TCOFF")!=0;
    if(!tcoff&&e->d&&e->tex==tex&&e->sz==sz&&e->seen==tframe) return e;      /* ja validada neste quadro */
    sig=tc_sig(vram,tex,tsp,psig);
    if(!tcoff&&e->d&&e->tex==tex&&e->sz==sz&&e->sig==sig&&e->pc==palctl){ e->seen=tframe; return e; }
    if(!e->d||e->sw*e->sh<sw*sh){ free(e->d); e->d=(uint32_t*)malloc((size_t)sw*sh*4); if(!e->d) return 0; }
    e->tex=tex; e->sz=sz; e->pc=palctl; e->sig=sig; e->sw=sw; e->sh=sh; e->seen=tframe; e->gen++;
    uint32_t tsp0=tsp&~(1u<<15)&~(1u<<16)&~(1u<<17)&~(1u<<18);
    for(int y=0;y<sh;y++) for(int x=0;x<sw;x++) e->d[y*sw+x]=texel_slow(vram,pal,palctl,tex,tsp0,(x+.5f)/sw,(y+.5f)/sh);
    /* texels transparentes (alfa 0) ficam com a cor dos vizinhos opacos: a filtragem/mip nao escurece as bordas de faixas e letreiros */
    { int na=0,nt=0; for(int i=0;i<sw*sh;i++){ uint32_t a=e->d[i]>>24; if(a==0) nt++; else na++; }
      if(nt&&na){ for(int pass=0;pass<3&&nt>0;pass++){ uint32_t *tmp=(uint32_t*)malloc((size_t)sw*sh*4); if(!tmp) break; memcpy(tmp,e->d,(size_t)sw*sh*4); nt=0;
          for(int y=0;y<sh;y++) for(int x=0;x<sw;x++){ if(e->d[y*sw+x]>>24) continue; unsigned r=0,g=0,b=0,n=0;
            for(int dy=-1;dy<=1;dy++) for(int dx=-1;dx<=1;dx++){ int xx=x+dx,yy=y+dy; if(xx<0||yy<0||xx>=sw||yy>=sh) continue; uint32_t p=e->d[yy*sw+xx]; if(!(p>>24)) continue; r+=(p>>16)&255; g+=(p>>8)&255; b+=p&255; n++; }
            if(n) tmp[y*sw+x]=((r/n)<<16)|((g/n)<<8)|(b/n); else nt++; }
          memcpy(e->d,tmp,(size_t)sw*sh*4); free(tmp); } } }
    /* cadeia de mipmaps (filtro de caixa 2x2), usada quando o TCW pede mipmap */
    { size_t need=0; int w=sw,h=sh,nl=0; while((w>1||h>1)&&nl<11){ w=w>1?w/2:1; h=h>1?h/2:1; need+=(size_t)w*h; nl++; }
      if(need>e->mmcap){ free(e->mm); e->mm=(uint32_t*)malloc(need*4+16); e->mmcap=e->mm?need:0; }
      e->nl=0; if(e->mm){ uint32_t *dst=e->mm; const uint32_t *src=e->d; int pw=sw,ph=sh; w=sw; h=sh;
        for(int l=1;l<=nl;l++){ w=w>1?w/2:1; h=h>1?h/2:1; e->mp[l]=dst;
          for(int y=0;y<h;y++) for(int x=0;x<w;x++){ int x0=x*2<pw?x*2:pw-1, x1=x*2+1<pw?x*2+1:pw-1, y0=y*2<ph?y*2:ph-1, y1=y*2+1<ph?y*2+1:ph-1;
            uint32_t p0=src[y0*pw+x0],p1=src[y0*pw+x1],p2=src[y1*pw+x0],p3=src[y1*pw+x1]; uint32_t o=0;
            for(int sft=0;sft<32;sft+=8) o|=((((p0>>sft)&255)+((p1>>sft)&255)+((p2>>sft)&255)+((p3>>sft)&255)+2)>>2)<<sft;
            dst[y*w+x]=o; }
          src=dst; pw=w; ph=h; dst+=(size_t)w*h; }
        e->nl=nl; } }
    return e;
}
static inline uint32_t tc_fetch(const uint32_t*d,int sw,int sh,int x,int y,uint32_t tsp){
    if((tsp>>16)&1){ if(x<0)x=0; if(x>=sw)x=sw-1; } else if((tsp>>18)&1){ x&=2*sw-1; if(x>=sw) x=2*sw-1-x; } else x&=sw-1;
    if((tsp>>15)&1){ if(y<0)y=0; if(y>=sh)y=sh-1; } else if((tsp>>17)&1){ y&=2*sh-1; if(y>=sh) y=2*sh-1-y; } else y&=sh-1;
    return d[y*sw+x];
}
/* lod<0 => nivel 0. Filtro (TSP bits 14-13): 0 ponto, >=1 bilinear; com LOD > 0 mistura dois niveis de mip (trilinear) */
static inline uint32_t lerp8(uint32_t a,uint32_t b,uint32_t t){   /* mistura dois pixels RGBA (2 canais por vez), t em 0..256 */
    uint32_t rb=(((a&0x00FF00FFu)*(256u-t)+(b&0x00FF00FFu)*t)>>8)&0x00FF00FFu, ag=((((a>>8)&0x00FF00FFu)*(256u-t)+((b>>8)&0x00FF00FFu)*t)>>8)&0x00FF00FFu;
    return rb|(ag<<8);
}
static inline uint32_t bilin(const uint32_t*d,int sw,int sh,float u,float v,uint32_t tsp){
    float fx=u*sw-0.5f, fy=v*sh-0.5f; float flx=floorf(fx), fly=floorf(fy); int x0=(int)flx, y0=(int)fly; uint32_t tx=(uint32_t)((fx-flx)*256.f), ty=(uint32_t)((fy-fly)*256.f);
    uint32_t p00,p10,p01,p11;
    if(x0>=0 && y0>=0 && x0+1<sw && y0+1<sh){ const uint32_t*q=d+y0*sw+x0; p00=q[0]; p10=q[1]; p01=q[sw]; p11=q[sw+1]; }
    else { p00=tc_fetch(d,sw,sh,x0,y0,tsp); p10=tc_fetch(d,sw,sh,x0+1,y0,tsp); p01=tc_fetch(d,sw,sh,x0,y0+1,tsp); p11=tc_fetch(d,sw,sh,x0+1,y0+1,tsp); }
    return lerp8(lerp8(p00,p10,tx),lerp8(p01,p11,tx),ty);
}
static inline float fast_log2(float x){ union{float f;uint32_t i;} u; u.f=x; float e=(float)((int)(u.i>>23)-127); u.i=(u.i&0x7FFFFFu)|0x3F800000u; float m=u.f; return e+(-0.34484843f*m+2.02466578f)*m-0.67487759f; }
static inline uint32_t sample_tc(const TC*e,uint32_t tsp,float u,float v,float lod){
    int sw=e->sw, sh=e->sh;
    if(((tsp>>13)&3)==0) return tc_fetch(e->d,sw,sh,(int)floorf(u*sw),(int)floorf(v*sh),tsp);
    if(lod<=0.f || e->nl<=0) return bilin(e->d,sw,sh,u,v,tsp);
    if(lod>=(float)e->nl) lod=(float)e->nl;
    int l0=(int)lod; float fr=lod-(float)l0; if(l0>=e->nl){ l0=e->nl; fr=0.f; }
    const uint32_t *d0=l0?e->mp[l0]:e->d; int w0=sw>>l0, h0=sh>>l0; if(w0<1)w0=1; if(h0<1)h0=1;
    uint32_t a=bilin(d0,w0,h0,u,v,tsp); if(fr<0.03f) return a;
    int l1=l0+1; const uint32_t *d1=e->mp[l1]; int w1=sw>>l1, h1=sh>>l1; if(w1<1)w1=1; if(h1<1)h1=1;
    uint32_t b=bilin(d1,w1,h1,u,v,tsp); int k=(int)(fr*256.f), o=0;
    for(int sft=0;sft<32;sft+=8) o|=(int)((((a>>sft)&255)*(256-k)+((b>>sft)&255)*k+128)>>8)<<sft;
    return (uint32_t)o;
}
/* --- varredura da lista de parametros --- */
static unsigned long long n_param[8];
static void parse(void){
    size_t o=0; ntri=0; nmv=0; cur_mesh=0; cur_shadow=0; int list=0,spr=0; uint32_t tsp=0,tex=0; int col=0,textured=0,uv16=0,offs=0,skip=0; Vtx facec={0,0,0,0,0,1,1,1,1}; Vtx strip[4096]; int ns=0; uint32_t sph_col=0xFFFFFFFF;
    while(o+32<=blen){
        uint32_t pcw=U32(o); int type=(pcw>>29)&7; n_param[type]++;
        if(type==0||type==1||type==2){ o+=32; ns=0; continue; }
        if(type==4 && GENV("CT2_PCWHIST") && blen>150000 && blen<200000){ static uint32_t hp[512],hc[512]; static int hn=0; static int pr=0; int k; uint32_t key=pcw&0x0700FFFF; for(k=0;k<hn;k++) if(hp[k]==key) break; if(k==hn&&hn<512){ hp[hn]=key; hc[hn]=0; hn++; { static int pn=0; if(pn++<6){ printf("RAWHDR @%zx:",o); for(int k=0;k<24;k++) printf(" %08x",U32(o+4*k)); printf("\n"); } } printf("HDRNEW pcw=%08x isp=%08x tsp=%08x tcw=%08x w4=%08x w5=%08x w6=%08x w7=%08x\n",pcw,U32(o+4),U32(o+8),U32(o+12),U32(o+16),U32(o+20),U32(o+24),U32(o+28)); } }
        if(type==4){
            spr=0; list=(pcw>>24)&7; col=(pcw>>4)&3; cur_col=col; textured=(pcw>>3)&1; offs=(pcw>>2)&1; uv16=pcw&1;
            skip=(list==1||list==3); cur_shadow=(pcw>>7)&1; cur_isp=U32(o+4); tsp=U32(o+8); tex=U32(o+12); ns=0;
            if(GENV("CT2_HDRDBG2")&&tsp==0x200c2440u){ static int nd2=0; if(nd2++<4){ printf("  H2 pcw=%08x:",pcw); for(int k=1;k<16&&o+4*k<blen;k++) printf(" %08x",U32(o+4*k)); printf("\n"); } }
            if(GENV("CT2_HDRDBG")){ static int nd=0; if(nd<8 && (col>=2)){ nd++; printf("  HDR col=%d offs=%d pcw=%08x:",col,offs,pcw); for(int k=1;k<8;k++) printf(" %08x",U32(o+4*k)); if(col==2&&offs) { printf(" |"); for(int k=8;k<16;k++) printf(" %08x",U32(o+4*k)); } printf("\n"); if(o+64<=blen){ printf("    prox:"); for(int k=0;k<8;k++) printf(" %08x",U32(o+((col==2&&offs)?64:32)+4*k)); printf("\n"); } } }
            if(col==2){ size_t fo=offs?o+32:o+16; facec.a=F32(fo); facec.r=F32(fo+4); facec.g=F32(fo+8); facec.b=F32(fo+12); }   /* cor da face: palavras 8-11 se o cabecalho tem 64 bytes (offset), senao 4-7; col==3 reaproveita a anterior */
            o+=(col==2&&offs)?64:32; continue;   /* premissa: cabecalho de 64 bytes quando intensidade + offset */
        }
        if(type==5){ spr=1; list=(pcw>>24)&7; skip=(list==1||list==3); cur_shadow=(pcw>>7)&1; cur_isp=U32(o+4); cur_col=0; tsp=U32(o+8); tex=U32(o+12); sph_col=U32(o+16); textured=(pcw>>3)&1; o+=32; ns=0; continue; }
        if(type==7 && spr){
            if(o+64>blen) break;
            float ax=F32(o+4),ay=F32(o+8),az=F32(o+12),bx=F32(o+16),by=F32(o+20),bz=F32(o+24),cx=F32(o+28),cy=F32(o+32),cz=F32(o+36),dx=F32(o+40),dy=F32(o+44),dz=az+cz-bz;
            Vtx q[4]; memset(q,0,sizeof q); q[0].x=ax;q[0].y=ay;q[0].z=az; q[1].x=bx;q[1].y=by;q[1].z=bz; q[2].x=cx;q[2].y=cy;q[2].z=cz; q[3].x=dx;q[3].y=dy;q[3].z=dz;
            for(int i=0;i<4;i++){ unpack(sph_col,&q[i]); }
            if(textured){ uint32_t ua=U32(o+52),ub=U32(o+56),uc=U32(o+60); q[0].u=half2f(ua>>16);q[0].v=half2f(ua&0xFFFF); q[1].u=half2f(ub>>16);q[1].v=half2f(ub&0xFFFF); q[2].u=half2f(uc>>16);q[2].v=half2f(uc&0xFFFF); q[3].u=q[0].u+q[2].u-q[1].u; q[3].v=q[0].v+q[2].v-q[1].v; }
            if(!skip){ addtri(&q[0],&q[1],&q[2],list,tsp,tex,textured,1); addtri(&q[0],&q[2],&q[3],list,tsp,tex,textured,1); }
            o+=64; continue;
        }
        if(type==7 && skip){   /* triangulo de volume modificador: 64 bytes (pcw, 3 vertices xyz, enchimento) */
            if(o+64>blen) break;
            if(nmv>=capmv){ capmv=capmv*2+256; mvs=realloc(mvs,capmv*sizeof *mvs); }
            MV*m=&mvs[nmv++]; for(int k=0;k<3;k++){ m->x[k]=F32(o+4+12*k); m->y[k]=F32(o+8+12*k); m->z[k]=F32(o+12+12*k); }
            m->mesh=cur_mesh; m->last=(cur_isp>>29)&7; if(m->last) cur_mesh++;   /* o triangulo com instrucao de volume (dentro/fora do ultimo poligono) fecha a malha */
            o+=64; continue;
        }
        if(type==7){
            if(o+32>blen) break;
            Vtx v; memset(&v,0,sizeof v); v.x=F32(o+4); v.y=F32(o+8); v.z=F32(o+12); v.r=v.g=v.b=v.a=1;
            if(col==0){ if(textured){ if(uv16){ uint32_t t=U32(o+16); v.u=half2f(t>>16); v.v=half2f(t&0xFFFF); } else { v.u=F32(o+16); v.v=F32(o+20); } } unpack(U32(o+24),&v); }
            else if(col==1){ if(textured){ v.u=F32(o+16); v.v=F32(o+20); } else { v.a=F32(o+16); v.r=F32(o+20); v.g=F32(o+24); v.b=F32(o+28); } }
            else { if(textured){ if(uv16){ uint32_t t=U32(o+16); v.u=half2f(t>>16); v.v=half2f(t&0xFFFF); } else { v.u=F32(o+16); v.v=F32(o+20); } } float it=F32(o+24); v.r=facec.r*it; v.g=facec.g*it; v.b=facec.b*it; v.a=facec.a; }
            o+=32;
            if(ns<4096) strip[ns++]=v;
            if((pcw>>28)&1){ if(!skip) for(int i=2;i<ns;i++){ if(i&1) addtri(&strip[i-1],&strip[i-2],&strip[i],list,tsp,tex,textured,uv16); else addtri(&strip[i-2],&strip[i-1],&strip[i],list,tsp,tex,textured,uv16); } ns=0; }
            continue;
        }
        o+=32;   /* tipo desconhecido (3, 6): pula */
    }
    (void)sph_col;
}
/* --- rasterizacao --- */
static uint32_t fb[FBMAX]; static float zb[FBMAX];
static int save_live=0; int ta_dump_req=0; unsigned ta_cur_frame=0; static int ta_bgframes=0; uint32_t ta_reg_backgnd_t=0, ta_reg_param_base=0, ta_reg_backgnd_d=0;

static void setplanes(Tri*t){ const Vtx *a=&t->v[0],*b=&t->v[1],*c=&t->v[2];
    double den=((double)b->y-c->y)*((double)a->x-c->x)+((double)c->x-b->x)*((double)a->y-c->y);
    if(fabs(den)<1e-9){ t->zc=c->z; t->zx=t->zy=t->ux=t->uy=t->vx=t->vy=0; t->uc=(double)c->u*c->z; t->vc=(double)c->v*c->z; return; }
    double id=1.0/den;
    #define PLANE(FA,FB,FC,OUTC,OUTX,OUTY) { double fa=(FA),fb=(FB),fc=(FC); OUTC=fc; OUTX=((fa-fc)*((double)b->y-c->y)+(fb-fc)*((double)c->y-a->y))*id; OUTY=((fa-fc)*((double)c->x-b->x)+(fb-fc)*((double)a->x-c->x))*id; }
    PLANE(a->z,b->z,c->z,t->zc,t->zx,t->zy)
    PLANE((double)a->u*a->z,(double)b->u*b->z,(double)c->u*c->z,t->uc,t->ux,t->uy)
    PLANE((double)a->v*a->z,(double)b->v*b->z,(double)c->v*c->z,t->vc,t->vx,t->vy)
    #undef PLANE
}
int ta_dbg[8]; /* F5 lod off, F6 sem cull, F7 esconde tris z>3.3, F8 so vertices z<3.3 sem uv */
static int ZT=0; static unsigned zt_n,zt_w,zt_k,zt_k2; static unsigned zh_t[5][8],zh_tie[5][8],zh_tiefail[5][8];
float ta_ztol=2e-5f;
float ta_bias_mip=-1.0f, ta_bias_nomip=-0.6f;
static void shade(Tri *t,int px,int py,float w0,float w1,float w2,const uint8_t*vram,const uint32_t*pal,uint32_t palctl,float *lc,int lcr){
    Vtx *a=&t->v[0],*b=&t->v[1],*c=&t->v[2];
    double ddx=px+.5-c->x, ddy=py+.5-c->y; float z=(float)(t->zc+t->zx*ddx+t->zy*ddy); int i=py*RW+px;
    { float zo=zb[i]; int dm=(t->isp>>29)&7, pass; if(dm==0&&t->list==2) dm=6; /* translucido auto-sort: o modo ISP vem como 0 (nunca) no jogo */   /* modos ISP: 0 nunca 1 menor 2 igual 3 menor-ou-igual 4 maior 5 diferente 6 maior-ou-igual 7 sempre (z=1/w) */
      if(ZT&&t->list<5){ zh_t[t->list][dm]++; if(zo>0&&fabsf(z-zo)<3e-5f*zo){ zh_tie[t->list][dm]++; int pp=(dm==1)?z<zo:(dm==3)?z<=zo:(dm==4)?z>zo:(dm==6)?z>=zo:1; if(!pp) zh_tiefail[t->list][dm]++; } }
      switch(dm){ case 0:pass=0;break; case 1:pass=z<zo;break; case 2:pass=fabsf(z-zo)<1e-9f;break; case 3:pass=z<=zo;break; case 4:pass=z>zo*(1.f-ta_ztol);break; case 5:pass=z!=zo;break; case 6:pass=z>=zo*(1.f-ta_ztol);break; default:pass=1; }
      if(!pass){ if(ZT&&dm==4&&t->list==0&&z<=zo&&(zo-z)<2e-5f*zo&&zo>0){ zt_n++; if(zt_k<40){ zt_k++; printf("ZTIE-LOSE px=%d,%d z=%g zo=%g tcw=%08x tsp=%08x isp=%08x\n",px,py,z,zo,t->tex,t->tsp,t->isp); } } return; }
      if(ZT&&dm==4&&t->list==0&&zo>0&&(z-zo)<2e-5f*zo){ zt_w++; if(zt_k2<40){ zt_k2++; printf("ZTIE-WIN px=%d,%d z=%g zo=%g tcw=%08x tsp=%08x isp=%08x\n",px,py,z,zo,t->tex,t->tsp,t->isp); } } }
    { static int pc=-1; if(pc<0) pc=GENV("CT2_PCOL")!=0; if(pc){ double q0=w0*a->z,q1=w1*b->z,q2=w2*c->z,qs=q0+q1+q2; if(qs>1e-12){ w0=(float)(q0/qs); w1=(float)(q1/qs); w2=(float)(q2/qs); } } }
    float r=w0*a->r+w1*b->r+w2*c->r, g=w0*a->g+w1*b->g+w2*c->g, bl=w0*a->b+w1*b->b+w2*c->b, al=w0*a->a+w1*b->a+w2*c->a;
    if(t->textured){
        /* correcao de perspectiva: u/w interpolado, dividido por 1/w */
        double zd=t->zc+t->zx*ddx+t->zy*ddy; if(zd<1e-12) zd=1e-12; double izd=1.0/zd; double uzp=t->uc+t->ux*ddx+t->uy*ddy, vzp=t->vc+t->vx*ddx+t->vy*ddy; float u=(float)(uzp*izd), v=(float)(vzp*izd); float lod=-1.f; if(t->clampuv){ if(u<t->cu0) u=t->cu0; else if(u>t->cu1) u=t->cu1; if(v<t->cv0) v=t->cv0; else if(v>t->cv1) v=t->cv1; }   /* v15x: sprites de interface em escala nao-inteira nao puxam o texel vizinho do atlas */
        if(((t->tex>>31)&1) || ((t->tsp>>13)&3)){
            if(lcr||*lc<-1.5f){   /* v15n: o LOD so e recalculado a cada 4 pixels da linha (era por pixel, com 4 divisoes em dupla precisao) */
                double uu=uzp*izd, vv=vzp*izd; double dudx=(t->ux-uu*t->zx)*izd, dudy=(t->uy-uu*t->zy)*izd, dvdx=(t->vx-vv*t->zx)*izd, dvdy=(t->vy-vv*t->zy)*izd;
                int tw=8<<((t->tsp>>3)&7), th=8<<(t->tsp&7); double px2=(dudx*tw)*(dudx*tw)+(dvdx*th)*(dvdx*th), py2=(dudy*tw)*(dudy*tw)+(dvdy*th)*(dvdy*th); double r2=px2>py2?px2:py2;
                lod=-1.f; if(r2>1e-12){ static float ak=-1.f; if(ak<-0.5f){ const char*e=GENV("CT2_ANISO"); ak=e?(float)atof(e):0.f; } double r2n=px2<py2?px2:py2; float an=0.f; if(ak>0.f&&r2n>1e-12) an=ak*0.5f*fast_log2((float)(r2/r2n)); lod=0.5f*fast_log2((float)r2) + an + (float)(((int)((t->tsp>>8)&15)-4))*0.25f; lod+=((t->tex>>31)&1)?ta_bias_mip:ta_bias_nomip; }
                *lc=lod; } else lod=*lc; }
        { static int l0=-1; if(l0<0) l0=GENV("CT2_LOD0")!=0; if(l0||ta_dbg[0]) lod=-1.f; }
        uint32_t tx; if(t->snap){ int sx=(int)(u*640.f), sy=(int)(v*512.f); if(sx<0)sx=0; if(sx>639)sx=639; if(sy<0)sy=0; if(sy>479)sy=479; sx=sx*ls_w/640; sy=sy*ls_h/480; tx=lastscene[sy*ls_w+sx]|0xFF000000u; } else tx=t->tc?sample_tc((const TC*)t->tc,t->tsp,u,v,lod):texel_slow(vram,pal,palctl,t->tex,t->tsp,u,v); float ta=(tx>>24)/255.f, tr=((tx>>16)&255)/255.f, tg=((tx>>8)&255)/255.f, tb=(tx&255)/255.f;
        int shading=(t->tsp>>6)&3; if(t->list==4 && ta<0.5f) return;   /* punch-through: recorta pelo alfa da textura */
        if((t->tsp>>19)&1) ta=1.f;
        float vr=r,vg=g,vb=bl,va=((t->tsp>>20)&1)?al:1.f;   /* UseAlpha=0: o alfa do VERTICE conta como 1 (o alfa da textura continua valendo) */
        if(shading==0){ r=tr; g=tg; bl=tb; al=ta; } else if(shading==1){ r=vr*tr; g=vg*tg; bl=vb*tb; al=ta; } else if(shading==2){ r=vr*(1-ta)+tr*ta; g=vg*(1-ta)+tg*ta; bl=vb*(1-ta)+tb*ta; al=va; } else { r=vr*tr; g=vg*tg; bl=vb*tb; al=va*ta; }
    }
    if(!t->textured && !((t->tsp>>20)&1)) al=1.f;   /* UseAlpha=0: alfa tratado como 1 */
    if(t->list==4 && al<0.5f) return;      /* punch-through: teste de alfa (premissa: limiar 0.5) */
    if(r>1)r=1; if(g>1)g=1; if(bl>1)bl=1; if(al>1)al=1; if(al<0)al=0; if(r<0)r=0; if(g<0)g=0; if(bl<0)bl=0;
    { static int ic=-1; if(ic<0) ic=GENV("CT2_IDCOL")!=0; if(ic&&t->list<2){ uint32_t h=(uint32_t)(t-tris)*2654435761u; r=((h>>8)&255)/255.f; g=((h>>16)&255)/255.f; bl=((h>>24)&255)/255.f; al=1.f; } }
    uint32_t old=fb[i]; float orr=((old>>16)&255)/255.f, og=((old>>8)&255)/255.f, ob=(old&255)/255.f;
    if(t->list==2){ int sf=(t->tsp>>29)&7, df=(t->tsp>>26)&7; float sr[3]={r,g,bl}, dr[3]={orr,og,ob}, o[3];
        for(int k=0;k<3;k++){ float fs, fd;
            switch(sf){ case 0:fs=0;break; case 1:fs=1;break; case 2:fs=dr[k];break; case 3:fs=1-dr[k];break; case 4:fs=al;break; case 5:fs=1-al;break; case 6:fs=1;break; default:fs=0;break; }
            switch(df){ case 0:fd=0;break; case 1:fd=1;break; case 2:fd=sr[k];break; case 3:fd=1-sr[k];break; case 4:fd=al;break; case 5:fd=1-al;break; case 6:fd=1;break; default:fd=0;break; }
            o[k]=sr[k]*fs+dr[k]*fd; if(o[k]>1)o[k]=1; }
        r=o[0]; g=o[1]; bl=o[2]; if(al>=0.99f && !t->snap && !((t->isp>>26)&1)) zb[i]=z; /* translucido 100% opaco (corpo dos carros) tambem grava Z: evita faixas/sombras desenhadas por cima */ } else if(!((t->isp>>26)&1)){ zb[i]=z; shb[i]=(uint8_t)t->shadow; }
    fb[i]=0xFF000000u|((uint32_t)(r*255+.5f)<<16)|((uint32_t)(g*255+.5f)<<8)|(uint32_t)(bl*255+.5f);
}
static void raster(Tri *t,const uint8_t*vram,const uint32_t*pal,uint32_t palctl,int by0,int by1){
    Vtx *a=&t->v[0],*b=&t->v[1],*c=&t->v[2];
    float minx=fminf(a->x,fminf(b->x,c->x)), maxx=fmaxf(a->x,fmaxf(b->x,c->x)), miny=fminf(a->y,fminf(b->y,c->y)), maxy=fmaxf(a->y,fmaxf(b->y,c->y));
    int x0=(int)fmaxf(0,floorf(minx)), x1=(int)fminf((float)(RW-1),ceilf(maxx)), y0=(int)fmaxf((float)by0,floorf(miny)), y1=(int)fminf((float)(by1-1),ceilf(maxy));
    if(y0>y1||x0>x1) return;
    double den=((double)b->y-c->y)*((double)a->x-c->x)+((double)c->x-b->x)*((double)a->y-c->y); if(fabs(den)<1e-6) return;
    { int cm=(t->isp>>27)&3; if(cm>=2 && !ta_dbg[1] && !GENV("CT2_NOCULL")){ double area=den; if((cm==2&&area<0)||(cm==3&&area>0)) return; } }   /* culling ISP: 2 = negativo, 3 = positivo (premissa de sinal) */
    /* v15m: bordas em dupla precisao (triangulos cortados no plano proximo tem coordenadas enormes; em float as cunhas/falhas apareciam) */
    double inv=1.0/den;
    double A0=((double)b->y-c->y)*inv, B0=((double)c->x-b->x)*inv, A1=((double)c->y-a->y)*inv, B1=((double)a->x-c->x)*inv;
    /* v15zl: tolerancia de borda em fracao de pixel (1e-3 px) proporcional ao gradiente de cada borda; o eps fixo 1e-4 equivalia a >100 px em triangulos gigantes e a calcada invadia a faixa de asfalto */
    const double Bs[3]={B0,B1,-(B0+B1)};
    for(int y=y0;y<=y1;y++){
        double py=y+.5, dy=py-c->y; double w0b=A0*(x0+.5-c->x)+B0*dy, w1b=A1*(x0+.5-c->x)+B1*dy;   /* valores em x=x0 */
        double A2=-(A0+A1), w2b=1.0-w0b-w1b;
        double lo=0, hi=(double)(x1-x0);
        const double ws[3]={w0b,w1b,w2b}, As[3]={A0,A1,A2};
        for(int k=0;k<3;k++){
            double ek=1e-3*fmax(fabs(As[k]),fabs(Bs[k])); if(ek<1e-12) ek=1e-12;
            if(As[k]>1e-12){ double xe=(-ek-ws[k])/As[k]; if(xe>lo) lo=xe; }
            else if(As[k]<-1e-12){ double xe=(-ek-ws[k])/As[k]; if(xe<hi) hi=xe; }
            else if(ws[k]<-ek){ lo=1e9; }
        }
        if(lo>1e9) lo=1e9; if(hi<-1e9) hi=-1e9; if(lo<-1e9) lo=-1e9; if(hi>1e9) hi=1e9;   /* v15zk: (int) de valor enorme e indefinido: triangulos gigantes (corte no plano proximo) desenhavam linhas inteiras onde nao deviam */
        int xa=(int)ceil(lo), xb=(int)floor(hi); if(xa<0) xa=0; if(xb>x1-x0) xb=x1-x0;
        float lc=-2.f;
        for(int xi=xa;xi<=xb;xi++){
            float w0=(float)(w0b+A0*xi), w1=(float)(w1b+A1*xi), w2=1.f-w0-w1;
            { static int pl=-1,plx=0,ply=0; if(pl<0){ const char*e=GENV("CT2_PIXLOG"); pl=e?1:0; if(e) sscanf(e,"%d,%d",&plx,&ply); } if(pl&&x0+xi==plx&&y==ply){ uint32_t before=fb[y*RW+x0+xi]; shade(t,x0+xi,y,w0,w1,w2,vram,pal,palctl,&lc,((xi-xa)&3)==0); fprintf(stderr,"[pix] f%u tex=%08x tsp=%08x list=%d tex?%d clamp=%d rgba=%.2f,%.2f,%.2f,%.2f w=%.3f,%.3f,%.3f x=%.2f..%.2f before=%08x after=%08x\n",ta_cur_frame,t->tex,t->tsp,t->list,t->textured,t->clampuv,t->v[0].r,t->v[0].g,t->v[0].b,t->v[0].a,w0,w1,w2,fminf(t->v[0].x,fminf(t->v[1].x,t->v[2].x)),fmaxf(t->v[0].x,fmaxf(t->v[1].x,t->v[2].x)),before,fb[y*RW+x0+xi]); continue; } }
            shade(t,x0+xi,y,w0,w1,w2,vram,pal,palctl,&lc,((xi-xa)&3)==0);
        }
    }
}
typedef struct { const uint8_t*vram; const uint32_t*pal; uint32_t palctl; const int*order; int y0,y1; } Band;
static int mvmode=-1;
static void shadow_pass(const Band*b){
    if(!nmv) return;
    if(mvmode<0){ const char*e=getenv("CT2_MVMODE"); mvmode=e?atoi(e):2; }
    for(int y=b->y0;y<b->y1;y++){ memset(par+y*RW,0,RW); memset(acc+y*RW,0,RW); }
    { static int once=0; if(!once&&getenv("CT2_MVDBG")){ once=1; size_t q=0; while(q<nmv){ int id=mvs[q].mesh; size_t e2=q; float x0=1e9,x1=-1e9,y0=1e9,y1=-1e9,z0=1e9,z1=-1e9; for(;e2<nmv&&mvs[e2].mesh==id;e2++) for(int v=0;v<3;v++){ const MV*m=&mvs[e2]; if(m->x[v]<x0)x0=m->x[v]; if(m->x[v]>x1)x1=m->x[v]; if(m->y[v]<y0)y0=m->y[v]; if(m->y[v]>y1)y1=m->y[v]; if(m->z[v]<z0)z0=m->z[v]; if(m->z[v]>z1)z1=m->z[v]; } fprintf(stderr,"mesh %d tris %zu x %.0f..%.0f y %.0f..%.0f z %.4f..%.4f last=%d\n",id,e2-q,x0,x1,y0,y1,z0,z1,mvs[q].last); q=e2; } } }
    if(getenv("CT2_MVPIX")){ static int once2=0; if(!once2){ once2=1; int px=atoi(getenv("CT2_MVPIX")), py=atoi(strchr(getenv("CT2_MVPIX"),',')+1); fprintf(stderr,"pixel %d,%d zb=%.5f\n",px,py,zb[py*RW+px]); for(size_t q=0;q<nmv;q++){ const MV*m=&mvs[q]; double den=(m->y[1]-m->y[2])*(m->x[0]-m->x[2])+(m->x[2]-m->x[1])*(m->y[0]-m->y[2]); if(fabs(den)<1e-9) continue; double id=1.0/den,pxx=px+.5,pyy=py+.5; double l0=((m->y[1]-m->y[2])*(pxx-m->x[2])+(m->x[2]-m->x[1])*(pyy-m->y[2]))*id, l1=((m->y[2]-m->y[0])*(pxx-m->x[2])+(m->x[0]-m->x[2])*(pyy-m->y[2]))*id, l2=1.0-l0-l1; if(l0<0||l1<0||l2<0) continue; fprintf(stderr," mesh %d tri %zu last=%d z=%.5f\n",m->mesh,q,m->last,l0*m->z[0]+l1*m->z[1]+l2*m->z[2]); } } }
    static int skipm=-2; if(skipm==-2){ const char*e=getenv("CT2_MVSKIP"); skipm=e?atoi(e):-1; }
    size_t k=0;
    while(k<nmv){ int mid=mvs[k].mesh; if(mid==skipm){ while(k<nmv&&mvs[k].mesh==mid) k++; continue; } size_t e=k; int by0=1<<30,by1=-1,bx0=1<<30,bx1=-1;
        for(;e<nmv&&mvs[e].mesh==mid;e++){ const MV*m=&mvs[e];
            if(mvmode==1&&m->last) continue;
            double miny=fmin(m->y[0],fmin(m->y[1],m->y[2])), maxy=fmax(m->y[0],fmax(m->y[1],m->y[2])), minx=fmin(m->x[0],fmin(m->x[1],m->x[2])), maxx=fmax(m->x[0],fmax(m->x[1],m->x[2]));
            int y0=(int)fmax((double)b->y0,floor(miny)), y1=(int)fmin((double)(b->y1-1),ceil(maxy)), x0=(int)fmax(0.0,floor(minx)), x1=(int)fmin((double)(RW-1),ceil(maxx));
            if(y0>y1||x0>x1) continue;
            double den=(m->y[1]-m->y[2])*(m->x[0]-m->x[2])+(m->x[2]-m->x[1])*(m->y[0]-m->y[2]); if(fabs(den)<1e-9) continue; double id=1.0/den;
            if(y0<by0) by0=y0; if(y1>by1) by1=y1; if(x0<bx0) bx0=x0; if(x1>bx1) bx1=x1;
            for(int y=y0;y<=y1;y++){ double py=y+.5;
                for(int x=x0;x<=x1;x++){ double px=x+.5;
                    double l0=((m->y[1]-m->y[2])*(px-m->x[2])+(m->x[2]-m->x[1])*(py-m->y[2]))*id, l1=((m->y[2]-m->y[0])*(px-m->x[2])+(m->x[0]-m->x[2])*(py-m->y[2]))*id, l2=1.0-l0-l1;
                    if(l0<0||l1<0||l2<0) continue;
                    int i=y*RW+x; if(!shb[i]) continue;
                    double z=l0*m->z[0]+l1*m->z[1]+l2*m->z[2];
                    if(mvmode>=2?(z>zb[i]*(1.0f+ta_shtol)):(z<zb[i]*(1.0f-ta_shtol))) par[i]^=1; } } }   /* face atras da superficie: paridade impar = dentro do volume (por malha) */
        for(int y=by0;y<=by1;y++) for(int x=bx0;x<=bx1;x++){ int i=y*RW+x; if(par[i]){ acc[i]=1; par[i]=0; } }
        k=e; }
    for(int y=b->y0;y<b->y1;y++) for(int x=0;x<RW;x++){ int i=y*RW+x; if(acc[i]&&shb[i]){ uint32_t c=fb[i]; uint32_t r=(uint32_t)(((c>>16)&255)*ta_shad+.5f), g=(uint32_t)(((c>>8)&255)*ta_shad+.5f), bl=(uint32_t)((c&255)*ta_shad+.5f); fb[i]=0xFF000000u|(r<<16)|(g<<8)|bl; } }
}
static void do_band(const Band*b){
    for(int y=b->y0;y<b->y1;y++) memset(shb+y*RW,0,RW);
    { static int se=0; if(!se){ se=1; const char*e=GENV("CT2_SHAD"); if(e) ta_shad=(float)atof(e);  e=GENV("CT2_SHTOL"); if(e) ta_shtol=(float)atof(e); } }
    int sdone=0;
    static int only=-2; if(only==-2){ const char*e=GENV("CT2_ONLY"); only=e?atoi(e):-1; }
    for(int p=0;p<3;p++){ if(b->order[p]==2&&!sdone){ shadow_pass(b); sdone=1; } for(size_t i=0;i<ntri;i++) if(tris[i].list==b->order[p]&&(only<0||only==tris[i].list)){
        const Tri*t=&tris[i]; float lo=fminf(t->v[0].y,fminf(t->v[1].y,t->v[2].y)), hi=fmaxf(t->v[0].y,fmaxf(t->v[1].y,t->v[2].y));
        if(hi<b->y0-1||lo>b->y1+1) continue; raster(&tris[i],b->vram,b->pal,b->palctl,b->y0,b->y1); } }
    if(!sdone) shadow_pass(b);
}
#define MAXTH 32
#define MAXB 128
/* v15zv: fila de trabalho. Faixas pequenas (3 por thread) sao puxadas por contador atomico: sem a divisao fixa em 8 faixas
   (que deixava 24 dos 32 nucleos de um Ryzen 9 parados e dava desequilibrio) o custo por quadro cai com mais nucleos. */
typedef void (*par_fn)(void*,int);
static struct { par_fn fn; void*arg; int n; volatile int next; } pj;
#ifdef _WIN32
static DWORD WINAPI par_thread(LPVOID p){ (void)p;
#else
static void *par_thread(void *p){ (void)p;
#endif
    for(;;){ int i=__sync_fetch_and_add(&pj.next,1); if(i>=pj.n) break; pj.fn(pj.arg,i); }
    return 0; }
static int par_nth(void){
    static int nth=0; if(nth) return nth; nth=1;
#ifdef _WIN32
    SYSTEM_INFO si; GetSystemInfo(&si); nth=(int)si.dwNumberOfProcessors;
#else
    long n=sysconf(_SC_NPROCESSORS_ONLN); nth=n>0?(int)n:1;
#endif
    /* v15zz: usa ate 90% dos processadores logicos (minimo 1; PCs de 1-2 nucleos usam todos). Com a GPU pronta o desenho dos triangulos nao passa por aqui: so sobra a CPU para o que a GPU nao faz (pos-processamento/AA). CT2_THREADS=N muda (ate 32) */
    if(nth>2){ nth=(int)(nth*0.9f); if(nth<2) nth=2; }
    const char*e=GENV("CT2_THREADS"); if(e) nth=atoi(e); if(nth<1) nth=1; if(nth>MAXTH) nth=MAXTH; return nth; }
static void par_run(par_fn fn,void*arg,int n,int allow_mt){
    int nth=par_nth(); if(!allow_mt||nth==1||n<2){ for(int i=0;i<n;i++) fn(arg,i); return; }
    if(nth>n) nth=n; pj.fn=fn; pj.arg=arg; pj.n=n; pj.next=0;
#ifdef _WIN32
    HANDLE th[MAXTH]; for(int i=1;i<nth;i++) th[i]=CreateThread(0,0,par_thread,0,0,0);
    par_thread(0); for(int i=1;i<nth;i++){ WaitForSingleObject(th[i],INFINITE); CloseHandle(th[i]); }
#else
    pthread_t th[MAXTH]; for(int i=1;i<nth;i++) pthread_create(&th[i],0,par_thread,0);
    par_thread(0); for(int i=1;i<nth;i++) pthread_join(th[i],0);
#endif
}
static Band g_bd[MAXB];
static void band_job(void*arg,int i){ (void)arg; do_band(&g_bd[i]); }
static void render_bands(const uint8_t*vram,const uint32_t*pal,uint32_t palctl,const int*order){
    int nth=par_nth(); int nb=(nth==1||ntri<500)?1:nth*3; if(nb>MAXB) nb=MAXB; if(nb>RH) nb=RH;
    for(int i=0;i<nb;i++){ g_bd[i].vram=vram; g_bd[i].pal=pal; g_bd[i].palctl=palctl; g_bd[i].order=order; g_bd[i].y0=RH*i/nb; g_bd[i].y1=RH*(i+1)/nb; }
    par_run(band_job,0,nb,nb>1);
}
static void *blend_ptr=0; static void blend_report_dummy(void){ if(!blend_ptr) return; unsigned long (*c)[8][8][8][2][2][4]=(unsigned long(*)[8][8][8][2][2][4])blend_ptr; printf("--- BLENDSTATS list fmt src dst usealpha ignoretexa shade : n\n"); for(int l=0;l<5;l++)for(int f=0;f<8;f++)for(int s=0;s<8;s++)for(int d=0;d<8;d++)for(int u=0;u<2;u++)for(int g=0;g<2;g++)for(int h=0;h<4;h++) if(c[l][f][s][d][u][g][h]) printf("  list=%d fmt=%d src=%d dst=%d ua=%d ig=%d shade=%d : %lu\n",l,f,s,d,u,g,h,c[l][f][s][d][u][g][h]); }

/* tamanho do quadro interno: escala (UPSCALE) e largura base (modo tela larga) */
static int gpu_ready(void);
static void ta_setup_canvas(void){
    int ri=ta_res_i%10; if(ri<0) ri+=10; float ra=(float)ta_res_tab[ri][0]/(float)ta_res_tab[ri][1];
    int wide=(ta_aspect==2&&ra>1.4f);
    if(gpu_ready()){ cap_w=3840; cap_h=2160; } else { cap_w=1707; cap_h=960; }
    float sc=ta_upscale/100.f; if(sc<1.f) sc=1.f; if(sc>2.f) sc=2.f;
    /* o quadro interno acompanha a resolucao escolhida (2K/4K): altura da janela / 480 */
    { float sr=(float)ta_res_tab[ri][1]/480.f; if(sr>sc) sc=sr; }
    RBX=wide?floorf(480.f*ra+.5f):640.f; if(RBX>853.f) RBX=853.f;
    if(sc*RBX>cap_w) sc=cap_w/RBX; if(sc*480.f>cap_h) sc=cap_h/480.f; if(sc<1.f) sc=1.f;
    dual_on=0; if(!gpu_ready()&&!GENV("CT2_NODUAL")){ float so=cap_w/RBX; if(cap_h/480.f<so) so=cap_h/480.f; if(so>sc*1.05f){ dual_on=1; SOUT=so; DFW=(int)(RBX*so+.5f); DFH=(int)(480.f*so+.5f); } }
    RS=sc; RW=(int)(RBX*sc+.5f); RH=(int)(480.f*sc+.5f);
    if(RW>cap_w) RW=cap_w; if(RH>cap_h) RH=cap_h; OW=RW; OH=RH; ta_fb_w=OW; ta_fb_h=OH;
    /* ANTIALIASING SSAA: desenha num quadro interno maior e reduz no fim */
    if(ta_aa_mode>=3){ float f=(ta_aa_mode==3)?1.5f:2.f; if(f*RW>cap_w) f=(float)cap_w/RW; if(f*RH>cap_h) f=(float)cap_h/RH; if(f>=1.25f){ RS=sc*f; RW=(int)(RBX*RS+.5f); RH=(int)(480.f*RS+.5f); if(RW>cap_w) RW=cap_w; if(RH>cap_h) RH=cap_h; } }
}
static int ta_flat3d=0, ta_bars_black=0;
static void ta_canvas_vtx(Vtx*v){ if(ta_flat3d){ v->x=(v->x+(RBX-640.f)/2.f)*RS; } else v->x*=RBX/640.f*RS; v->y*=RS; }   /* fundo: estica para a largura toda */
static void ta_apply_canvas(void){
    float sx=RBX/640.f, ex=(RBX-640.f)/2.f;
#ifdef _WIN32
    { static unsigned lf2=0; static int logo_run=0, adx_done=0; if(ta_cur_frame!=lf2){ lf2=ta_cur_frame;
        if(ntri<=30) logo_run++; else { if(logo_run>=100&&!adx_done&&!GENV("CT2_NOSPLASH")){ extern int win_splash_adx(void); adx_done=1; win_splash_adx(); } logo_run=0; } } }   /* v15zzd: tela ADX/CRI depois dos logos SEGA/Hitmaker (varios quadros seguidos com poucos triangulos) e antes do press start */
#endif
    if(GENV("CT2_SMALLDBG")){ static unsigned lf=0; if(ta_cur_frame!=lf){ lf=ta_cur_frame; unsigned h=0; for(size_t i=0;i<ntri;i++) h=h*31u+tris[i].tex; if(ntri<=16||(ta_cur_frame%60)==0) printf("[small] f%u ntri=%zu h=%08x tex0=%08x\n",ta_cur_frame,ntri,h,ntri?tris[0].tex:0); } }
    /* v15zz: quadro sem 3D (menus, intro, NOW LOADING, tela da distribuidora) = proporcao 4:3 centralizada com barras laterais, nada esticado */
    int flat3d=0; if(sx>1.001f&&!GENV("CT2_WIDEMENU")){ size_t n3=0; for(size_t i=0;i<ntri;i++){ const Tri*t=&tris[i]; { float z0=t->v[0].z; if(fabsf(t->v[1].z-z0)>1e-4f*fabsf(z0)||fabsf(t->v[2].z-z0)>1e-4f*fabsf(z0)) n3++; } } flat3d=(n3<30||mx_screen!=MXS_NONE); { static int lastf=-1; if(flat3d!=lastf){ lastf=flat3d; printf("[wide] f%u quadro sem 3D=%d (nao-planos=%zu, tela menux=%d)\n",ta_cur_frame,flat3d,n3,(int)mx_screen); } } if(GENV("CT2_WIDEDBG")&&(ta_cur_frame%120)==3) printf("[wide] f%u ntri=%zu n3=%zu flat=%d sx=%.3f\n",ta_cur_frame,ntri,n3,flat3d,sx); } ta_flat3d=flat3d; { static unsigned lfb=0; if(ta_cur_frame!=lfb||ta_cur_frame==0){ lfb=ta_cur_frame; ta_bars_black=flat3d; } }   /* v15zzf: barras laterais SEMPRE pretas em menus/carregamento (1a passada do quadro decide) */
    /* v15zz: FOV real no 3D: o jogo nao recorta a geometria em 640, entao desenhar a cena SEM esticar (centralizada) mostra mais mundo dos lados; CT2_STRETCH3D=1 volta a esticar */
    int shift_all=flat3d||(sx>1.001f&&!GENV("CT2_STRETCH3D"));
    { extern float ta_wide_cull; ta_wide_cull=(sx>1.001f&&!GENV("CT2_STRETCH3D"))?fminf(8.f,1.f+6.f*(sx-1.f)):1.0f; }   /* v15zzi: o jogo passa a descartar objetos so fora do campo de visao alargado */
    for(size_t i=0;i<nmv;i++) for(int k=0;k<3;k++){ mvs[i].x[k]=shift_all?(mvs[i].x[k]+ex)*RS:mvs[i].x[k]*sx*RS; mvs[i].y[k]*=RS; }
    for(size_t i=0;i<ntri;i++){ Tri*t=&tris[i]; int stretch=1; float dx=0;
        if(shift_all){ stretch=0; dx=ex; } else if(sx>1.001f){ int ui=1; float x0=1e9f,x1=-1e9f; for(int k=0;k<3;k++){ if(t->v[k].z<1.0f) ui=0; if(t->v[k].x<x0) x0=t->v[k].x; if(t->v[k].x>x1) x1=t->v[k].x; }
            if(ui&&x1-x0<600.f){ stretch=0; dx=ex; } }   /* HUD/menus: proporcao 4:3 centralizada; 3D e fundos: esticados */
        t->clampuv=0;
        if(t->textured){ int ui=1; float u0=1e9f,u1=-1e9f,v0=1e9f,v1=-1e9f; for(int k=0;k<3;k++){ if(t->v[k].z<1.0f) ui=0; u0=fminf(u0,t->v[k].u); u1=fmaxf(u1,t->v[k].u); v0=fminf(v0,t->v[k].v); v1=fmaxf(v1,t->v[k].v); }
            int flatz=(fabsf(t->v[0].z-t->v[1].z)<=1e-5f*fabsf(t->v[0].z)&&fabsf(t->v[0].z-t->v[2].z)<=1e-5f*fabsf(t->v[0].z)&&!GENV("CT2_NOCLAMPUI"));   /* v15zz: quad plano (mesma profundidade: menu/sprite) com UV dentro de 0..1 nao deve dar a volta na textura (linha clara nas emendas das caixas) */
            if((ui||flatz)&&u0>=-0.001f&&u1<=1.001f&&v0>=-0.001f&&v1<=1.001f){ float tw=(float)(8<<((t->tsp>>3)&7)), th=(float)(8<<(t->tsp&7)); t->cu0=u0+0.5f/tw; t->cu1=u1-0.5f/tw; t->cv0=v0+0.5f/th; t->cv1=v1-0.5f/th; t->clampuv=(t->cu1>=t->cu0&&t->cv1>=t->cv0); } }
        for(int k=0;k<3;k++){ float x=t->v[k].x; x=stretch?x*sx:x+dx; t->v[k].x=x*RS; t->v[k].y*=RS; } }
}
typedef struct { float key; size_t idx; } SK;
static int sk_cmp(const void*a,const void*b){ const SK*x=(const SK*)a,*y=(const SK*)b; if(x->key<y->key) return -1; if(x->key>y->key) return 1; return x->idx<y->idx?-1:(x->idx>y->idx); }
static const uint32_t *render_impl(const uint8_t*vram,const uint32_t*pal,uint32_t palctl,unsigned *ntris_out);
#include <time.h>
#ifndef _WIN32
static double t_acc=0; static unsigned t_n=0;
static void t_report(void){ if(t_n) fprintf(stderr,"[ta] %u quadros, media %.2f ms por quadro\n",t_n,1000.0*t_acc/t_n); }
const uint32_t *ta_render_frame(const uint8_t*vram,const uint32_t*pal,uint32_t palctl,unsigned *ntris_out){
    static int reg=0; if(!reg){ reg=1; if(GENV("CT2_TIMING")) atexit(t_report); }
    struct timespec a,b; clock_gettime(CLOCK_MONOTONIC,&a); const uint32_t *r=render_impl(vram,pal,palctl,ntris_out); clock_gettime(CLOCK_MONOTONIC,&b);
    if(r && *ntris_out>1000){ t_acc+=(b.tv_sec-a.tv_sec)+(b.tv_nsec-a.tv_nsec)*1e-9; t_n++; if(GENV("CT2_TIMING")&&t_n%100==0) fprintf(stderr,"[ta] %u quadros, media %.2f ms\n",t_n,1000.0*t_acc/t_n); } return r;
}
#else
const uint32_t *ta_render_frame(const uint8_t*vram,const uint32_t*pal,uint32_t palctl,unsigned *ntris_out){ return render_impl(vram,pal,palctl,ntris_out); }
#endif
/* ---- ANTIALIASING: pos-processamento do quadro final (modos 1 BASIC, 2 FXAA, 3 SSAA 2X = reducao do quadro interno) ---- */
static uint32_t aab[FBMAX];
static inline int aa_l(uint32_t c){ return (int)(((c>>16)&255)*77+((c>>8)&255)*150+(c&255)*29)>>8; }
typedef struct { int m; } AaArg;
#define AAROWS 16
static void aa_job(void*arg,int j){
    int m=((AaArg*)arg)->m; int y0=j*AAROWS, y1=y0+AAROWS;
    if(m==3){ if(y1>OH) y1=OH;   /* SSAA: media de caixa das amostras internas de cada pixel de saida */
        for(int y=y0;y<y1;y++){ int sy0=y*RH/OH,sy1=(y+1)*RH/OH; if(sy1<=sy0) sy1=sy0+1; if(sy1>RH) sy1=RH;
            for(int x=0;x<OW;x++){ int sx0=x*RW/OW,sx1=(x+1)*RW/OW; if(sx1<=sx0) sx1=sx0+1; if(sx1>RW) sx1=RW; unsigned r=0,g=0,b=0,n=0;
                for(int yy=sy0;yy<sy1;yy++) for(int xx=sx0;xx<sx1;xx++){ uint32_t c=fb[yy*RW+xx]; r+=(c>>16)&255; g+=(c>>8)&255; b+=c&255; n++; }
                aab[y*OW+x]=0xFF000000u|((r/n)<<16)|((g/n)<<8)|(b/n); } }
        return; }
    const uint32_t *s=fb; int AW=RW,AH=RH; if(y1>AH) y1=AH;
    for(int y=y0;y<y1;y++){ int ym=y>0?y-1:0, yp=y<AH-1?y+1:AH-1;
        for(int x=0;x<AW;x++){ int xm=x>0?x-1:0, xp=x<AW-1?x+1:AW-1;
            uint32_t c=s[y*AW+x], n=s[ym*AW+x], so=s[yp*AW+x], w=s[y*AW+xm], e=s[y*AW+xp];
            int lc=aa_l(c),ln=aa_l(n),ls=aa_l(so),lw=aa_l(w),le=aa_l(e);
            int mx=lc,mn=lc; if(ln>mx)mx=ln; if(ln<mn)mn=ln; if(ls>mx)mx=ls; if(ls<mn)mn=ls; if(lw>mx)mx=lw; if(lw<mn)mn=lw; if(le>mx)mx=le; if(le<mn)mn=le;
            int rg=mx-mn; uint32_t o=c;
            if(m==1){ /* BASIC: barato (so 4 vizinhos); suaviza apenas onde ha contraste forte */
                if(rg>=24){ uint32_t r=(((c>>16)&255)*4+((n>>16)&255)+((so>>16)&255)+((w>>16)&255)+((e>>16)&255))>>3, g=(((c>>8)&255)*4+((n>>8)&255)+((so>>8)&255)+((w>>8)&255)+((e>>8)&255))>>3, b=((c&255)*4+(n&255)+(so&255)+(w&255)+(e&255))>>3; o=0xFF000000u|(r<<16)|(g<<8)|b; } }
            else { /* FXAA (versao enxuta): mistura com a vizinhanca 3x3 proporcional ao contraste local */
                int thr=mx>>3; if(thr<8) thr=8;
                if(rg>=thr){ uint32_t nw=s[ym*AW+xm], ne=s[ym*AW+xp], sw=s[yp*AW+xm], se=s[yp*AW+xp];
                    int sr=0,sg=0,sb=0; uint32_t q[8]={n,so,w,e,nw,ne,sw,se}; for(int k=0;k<8;k++){ sr+=(q[k]>>16)&255; sg+=(q[k]>>8)&255; sb+=q[k]&255; }
                    int avl=(ln+ls+lw+le)>>2; int d=avl>lc?avl-lc:lc-avl; int bl=d*256/(rg?rg:1); if(bl>256) bl=256; bl=bl*3/4+(bl>0?32:0); if(bl>256) bl=256;
                    int cr=(c>>16)&255,cg=(c>>8)&255,cb=c&255; int ar=sr/8,ag=sg/8,ab=sb/8;
                    int r=cr+((ar-cr)*bl>>8), g=cg+((ag-cg)*bl>>8), b=cb+((ab-cb)*bl>>8); o=0xFF000000u|((uint32_t)r<<16)|((uint32_t)g<<8)|(uint32_t)b; } }
            aab[y*AW+x]=o; } }
}
int ta_bri_lv=5;   /* BRIGHT do menu de pausa: 0..10, 5 = neutro */
static const uint32_t *aa_finish2(void);
static const uint32_t *aa_finish(void){
    const uint32_t *o=aa_finish2();
    if(ta_bri_lv!=5){ static uint32_t bb[FBMAX]; int n=OW*OH; float g=0.5f+ta_bri_lv*0.1f; int lut[256]; for(int i=0;i<256;i++){ int v=(int)(i*g+0.5f); lut[i]=v>255?255:v; }
        for(int i=0;i<n;i++){ uint32_t p=o[i]; bb[i]=(p&0xFF000000u)|((uint32_t)lut[(p>>16)&255]<<16)|((uint32_t)lut[(p>>8)&255]<<8)|(uint32_t)lut[p&255]; } return bb; }
    return o;
}
static const uint32_t *aa_finish2(void){
    int m=ta_aa_mode; if(m<=0||m>4||aa_skip) return fb;
    if(GENV("CT2_AADBG")) fprintf(stderr,"[aa] mode=%d RW=%d RH=%d OW=%d OH=%d\n",ta_aa_mode,RW,RH,OW,OH);
    if(m>=3){ if(RW==OW&&RH==OH) m=2; else m=3; }
    AaArg a={m}; int rows=(m==3)?OH:RH; par_run(aa_job,&a,(rows+AAROWS-1)/AAROWS,1);
    return aab;
}
#include "gpu11.inc"
/* v15zz: amplia o quadro 3D (escala do UPSCALE) para a escala maxima de saida, bilinear, em faixas paralelas */
typedef struct { const uint32_t *s; int sw,sh; } UpArg;
static uint32_t upb[FBMAX];
static void up_job(void*arg,int j){ const UpArg*a=(const UpArg*)arg; int y0=j*16,y1=y0+16; if(y1>DFH) y1=DFH; int sw=a->sw,sh=a->sh;
    for(int y=y0;y<y1;y++){ float fy=(y+.5f)*sh/DFH-.5f; if(fy<0) fy=0; int iy=(int)fy; int iy1=iy+1<sh?iy+1:sh-1; int wy=(int)((fy-iy)*256);
        for(int x=0;x<DFW;x++){ float fx=(x+.5f)*sw/DFW-.5f; if(fx<0) fx=0; int ix=(int)fx; int ix1=ix+1<sw?ix+1:sw-1; int wx=(int)((fx-ix)*256);
            uint32_t p00=a->s[iy*sw+ix],p10=a->s[iy*sw+ix1],p01=a->s[iy1*sw+ix],p11=a->s[iy1*sw+ix1]; uint32_t o=0xFF000000u;
            for(int sh2=0;sh2<24;sh2+=8){ int c00=(p00>>sh2)&255,c10=(p10>>sh2)&255,c01=(p01>>sh2)&255,c11=(p11>>sh2)&255;
                int top=c00*(256-wx)+c10*wx, bot=c01*(256-wx)+c11*wx; int v=(top*(256-wy)+bot*wy)>>16; o|=(uint32_t)v<<sh2; }
            upb[y*DFW+x]=o; } } }
static void assign_tc(const uint8_t*vram,const uint32_t*pal,uint32_t palctl){
    for(size_t i=0;i<ntri;i++){ Tri*t=&tris[i]; t->snap=0;
        /* menu de pausa: o jogo desenha o quadro anterior como textura 512x512 RGB565 nao-twiddled; sem o espelho do framebuffer usamos o ultimo quadro 3D (heuristica) */
        if(t->textured && ((t->tex>>27)&7)==1 && ((t->tex>>26)&1) && !((t->tex>>30)&1) && ((t->tsp>>3)&7)==6 && (t->tsp&7)==6 && have_scene){ t->snap=1; snap_used=1; t->tc=0; continue; }
        t->tc=t->textured?(const void*)tc_get(vram,pal,palctl,t->tex,t->tsp):0;     { const char*e=GENV("CT2_TEXONE"); static int done1=0; if(e&&!done1){ uint32_t want=(uint32_t)strtoul(e,0,16); for(size_t i=0;i<ntri;i++){ Tri*t=&tris[i]; if(t->textured&&t->tex==want&&t->tc){ const TC*c=(const TC*)t->tc; done1=1; FILE*f=fopen("/tmp/texone.txt","w"); fprintf(f,"%dx%d\n",c->sw,c->sh); for(int y=0;y<c->sh;y++){ for(int x=0;x<c->sw;x++) fprintf(f,"%08x ",c->d[y*c->sw+x]); fprintf(f,"\n"); } fclose(f); break; } } } }
}
}
static const uint32_t *render_impl(const uint8_t*vram,const uint32_t*pal,uint32_t palctl,unsigned *ntris_out){
    ta_setup_canvas(); aa_skip=0; all_ui_f=0;
    int use_gpu=gpu_ready();
    { const char*ze=GENV("CT2_ZTIE"); if(ze){ ZT=(ta_cur_frame==(unsigned)atoi(ze)); zt_n=zt_w=zt_k=zt_k2=0; } }
    { static int zi=0; if(!zi){ zi=1; const char*e=GENV("CT2_ZTOL"); if(e) ta_ztol=(float)atof(e); } }
    if(ta_dump_req||GENV("CT2_DUMPFRAME")){ /* o buffer TA ainda esta intacto antes do parse */ }
    parse(); if(!GENV("CT2_NOMX")) menux_frame();
    { const char*fe=GENV("CT2_FONTDUMP"); static int fd=0; if(fe&&!fd&&ta_cur_frame>=(unsigned)atoi(fe)){ for(size_t i=0;i<ntri;i++) if(mx_isglyph(&tris[i])){ const TC*e=tc_get(vram,pal,palctl,tris[i].tex,tris[i].tsp); if(e){ FILE*o=fopen("/tmp/font.ppm","wb"); fprintf(o,"P6\n%d %d\n255\n",e->sw,e->sh); for(int k=0;k<e->sw*e->sh;k++){ uint32_t p=e->d[k]; uint8_t a=p>>24; uint8_t q[3]={a,a,a}; fwrite(q,1,3,o); } fclose(o); printf("[fontdump] %dx%d tex=%08x tsp=%08x\n",e->sw,e->sh,tris[i].tex,tris[i].tsp); fd=1; } break; } } }
    if(ta_dump_req||(GENV("CT2_DUMPFRAME")&&ta_cur_frame==(unsigned)atoi(GENV("CT2_DUMPFRAME")))){ ta_dump_req=0; save_live=1; char dfn[64]; snprintf(dfn,sizeof dfn,"glitch_dump_%u.bin",ta_cur_frame); FILE*df=fopen(dfn,"wb"); if(!df){ FILE*ef=fopen("dump_erro.txt","w"); if(ef){ fprintf(ef,"nao foi possivel criar %s\n",dfn); fclose(ef); } } if(df){ /* repro offline: comandos TA + VRAM + paleta + registradores */
            uint32_t hdr[8]={0x32544344u,ta_reg_backgnd_t,ta_reg_param_base,ta_reg_backgnd_d,palctl,(uint32_t)blen,ta_cur_frame,0}; fwrite(hdr,4,8,df); fwrite(buf,1,blen,df); fwrite(vram,1,16u<<20,df); fwrite(pal,4,1024,df); fclose(df); printf("[dump] %s salvo (quadro %u, %zu bytes de TA)\n",dfn,ta_cur_frame,blen); fflush(stdout); } }
    if(GENV("CT2_TRIHASH")){ FILE*hf=fopen(GENV("CT2_TRIHASH"),"a"); for(size_t i=0;i<ntri;i++){ Tri*t=&tris[i]; fprintf(hf,"uv %.4g,%.4g %.4g,%.4g %.4g,%.4g ",t->v[0].u,t->v[0].v,t->v[1].u,t->v[1].v,t->v[2].u,t->v[2].v); fprintf(hf,"S%d C %.2f,%.2f,%.2f,%.2f %.2f,%.2f,%.2f,%.2f %.2f,%.2f,%.2f,%.2f ",t->shadow,t->v[0].r,t->v[0].g,t->v[0].b,t->v[0].a,t->v[1].r,t->v[1].g,t->v[1].b,t->v[1].a,t->v[2].r,t->v[2].g,t->v[2].b,t->v[2].a); fprintf(hf,"%zu L%d tsp%08x tex%08x isp%08x col%d t%d %.2f,%.2f,%g %.2f,%.2f,%g %.2f,%.2f,%g\n",i,t->list,t->tsp,t->tex,t->isp,t->col,t->textured,t->v[0].x,t->v[0].y,t->v[0].z,t->v[1].x,t->v[1].y,t->v[1].z,t->v[2].x,t->v[2].y,t->v[2].z); } fprintf(hf,"=====\n"); fclose(hf); }
    if(GENV("CT2_TEXPPM")){ static uint32_t done[64]; static int nd=0; for(size_t i=0;i<ntri;i++){ Tri*t=&tris[i]; if(!t->textured||!t->tc) continue; int k; for(k=0;k<nd;k++) if(done[k]==t->tex) break; if(k<nd||nd>=64) continue; done[nd++]=t->tex; const TC*e=(const TC*)t->tc; char fn[64]; snprintf(fn,sizeof fn,"/tmp/tex_%08x.ppm",t->tex); FILE*f=fopen(fn,"wb"); fprintf(f,"P6\n%d %d\n255\n",e->sw,e->sh); for(int p=0;p<e->sw*e->sh;p++){ uint8_t px[3]={(uint8_t)(e->d[p]>>16),(uint8_t)(e->d[p]>>8),(uint8_t)e->d[p]}; fwrite(px,1,3,f);} fclose(f); } }
    { const char*e=GENV("CT2_TEXDUMP"); if(e){ uint32_t want=(uint32_t)strtoul(e,0,16); for(size_t i=0;i<ntri;i++) if(tris[i].textured&&tris[i].tex==want){ const TC*tc=tc_get(vram,pal,palctl,tris[i].tex,tris[i].tsp); if(tc){ FILE*tf=fopen("texdump.ppm","wb"); fprintf(tf,"P6\n%d %d\n255\n",tc->sw,tc->sh); for(int q=0;q<tc->sw*tc->sh;q++){ uint32_t c=tc->d[q]; fputc((c>>16)&255,tf); fputc((c>>8)&255,tf); fputc(c&255,tf); } fclose(tf); printf("texdump %dx%d\n",tc->sw,tc->sh);} break; } } }
    { const char*e=GENV("CT2_DBGTEX"); if(e&&ta_cur_frame==(unsigned)atoi(e)) for(size_t i=0;i<ntri;i++) if(tris[i].textured&&(tris[i].tex==0x480e3f3cu||tris[i].tex==0x500bd5fcu)){ Tri*t=&tris[i]; const TC*tc=tc_get(vram,pal,palctl,t->tex,t->tsp); printf("[dbgtex] i=%zu tex=%08x tsp=%08x list=%d v0=(%.1f,%.1f,u%.2f,v%.2f) v1=(%.1f,%.1f) v2=(%.1f,%.1f) tc=%p d[mid]=%08x\n",i,t->tex,t->tsp,t->list,t->v[0].x,t->v[0].y,t->v[0].u,t->v[0].v,t->v[1].x,t->v[1].y,t->v[2].x,t->v[2].y,(void*)tc,tc?tc->d[(tc->sh/2)*tc->sw+tc->sw/2]:0); } }
    { const char*e=GENV("CT2_TEXTRIS"); static int tdone=0; if(e&&!tdone&&ta_cur_frame>=(unsigned)atoi(GENV("CT2_TEXTRIS_F")?GENV("CT2_TEXTRIS_F"):"0")){ uint32_t want=(uint32_t)strtoul(e,0,16); int n=0; for(size_t i=0;i<ntri;i++) if(tris[i].tex==want&&tris[i].textured){ Tri*t=&tris[i]; n++; printf("[tt] #%zu L%d tsp=%08x z=%g (%.1f,%.1f u%.4f v%.4f) (%.1f,%.1f u%.4f v%.4f) (%.1f,%.1f u%.4f v%.4f) rgba=%.2f %.2f %.2f %.2f\n",i,t->list,t->tsp,t->v[0].z,t->v[0].x,t->v[0].y,t->v[0].u,t->v[0].v,t->v[1].x,t->v[1].y,t->v[1].u,t->v[1].v,t->v[2].x,t->v[2].y,t->v[2].u,t->v[2].v,t->v[0].r,t->v[0].g,t->v[0].b,t->v[0].a); }
        if(n>0){ tdone=1; for(size_t i=0;i<ntri;i++) if(tris[i].tex==want&&tris[i].textured){ const TC*c=tc_get(vram,pal,palctl,tris[i].tex,tris[i].tsp); if(c){ FILE*o=fopen("/tmp/font2.ppm","wb"); fprintf(o,"P6\n%d %d\n255\n",c->sw,c->sh); for(int k=0;k<c->sw*c->sh;k++){ uint32_t pp=c->d[k]; uint8_t a=pp>>24; uint8_t q[3]={a,a,a}; fwrite(q,1,3,o); } fclose(o); printf("[tt] texture %dx%d\n",c->sw,c->sh); } break; } fflush(stdout); } } }
    if(ntris_out) *ntris_out=(unsigned)ntri;
    if(ntri<8) return 0;
    if(GENV("CT2_TEXSTATS") && ntri>3000){ static int done=0; if(!done){ done=1; static unsigned cnt[8][2][2][2][2][8][8]; memset(cnt,0,sizeof cnt);
        for(size_t i=0;i<ntri;i++) if(tris[i].textured){ uint32_t tx=tris[i].tex, ts=tris[i].tsp; cnt[(tx>>27)&7][(tx>>30)&1][(tx>>31)&1][(tx>>26)&1][(tx>>25)&1][ts&7][(ts>>3)&7]++; }
        { static unsigned cc[4][4][2]; memset(cc,0,sizeof cc); for(size_t i=0;i<ntri;i++){ cc[tris[i].col][(tris[i].tsp>>6)&3][tris[i].textured]++; }
          for(int a=0;a<4;a++)for(int b=0;b<4;b++)for(int c2=0;c2<2;c2++) if(cc[a][b][c2]) printf("  coltype=%d shading=%d textured=%d : %u\n",a,b,c2,cc[a][b][c2]);
          int k=0; for(size_t i=0;i<ntri&&k<6;i++) if(tris[i].textured&&tris[i].list==0&&i%500==3){ k++; Vtx*v=&tris[i].v[0]; printf("  amostra tri %zu: col=%d u=%.2f v=%.2f rgba=%.2f %.2f %.2f %.2f tsp=%08x\n",i,tris[i].col,v->u,v->v,v->r,v->g,v->b,v->a,tris[i].tsp); } }
        printf("--- formatos de textura (quadro com %zu triangulos): fmt vq mip nontw stride U V : n\n",ntri);
        for(int a=0;a<8;a++)for(int b=0;b<2;b++)for(int c=0;c<2;c++)for(int d=0;d<2;d++)for(int e=0;e<2;e++)for(int u=0;u<8;u++)for(int v=0;v<8;v++) if(cnt[a][b][c][d][e][u][v]) printf("  fmt=%d vq=%d mip=%d nontw=%d stride=%d %dx%d : %u\n",a,b,c,d,e,8<<u,8<<v,cnt[a][b][c][d][e][u][v]); } }
    int hasbld=(ntri>3800);
    if(GENV("CT2_TEXDUMP") && hasbld){ static int done2=0; static unsigned from=0; if(!from){ const char*e=GENV("CT2_TEXDUMP_FROM"); from=e?atoi(e):1; } static unsigned calls=0; calls++; if(!done2&&calls>=from){ printf("TEXDUMP frame ntri=%zu\n",ntri); done2=1; const char*dir=GENV("CT2_TEXDUMP"); uint32_t seen[64]; int ns=0;
        for(size_t i=0;i<ntri&&ns<60;i++) if(tris[i].textured&&tris[i].list==0){ uint32_t tx=tris[i].tex, ts=tris[i].tsp; int dup=0; for(int k=0;k<ns;k++) if(seen[k]==tx) dup=1; if(dup) continue; seen[ns++]=tx;
            int sw=8<<((ts>>3)&7), sh=8<<(ts&7); char p[400]; snprintf(p,sizeof p,"%s/t%02d_f%d_vq%d_mip%d_nt%d_%dx%d.ppm",dir,ns,(tx>>27)&7,(tx>>30)&1,(tx>>31)&1,(tx>>26)&1,sw,sh);
            FILE*f=fopen(p,"wb"); if(!f) continue; fprintf(f,"P6\n%d %d\n255\n",sw,sh);
            for(int y=0;y<sh;y++) for(int x=0;x<sw;x++){ uint32_t c=texel_slow(vram,pal,palctl,tx,ts&~(3u<<15),(x+.5f)/sw,(y+.5f)/sh); uint8_t px[3]={(uint8_t)(c>>16),(uint8_t)(c>>8),(uint8_t)c}; fwrite(px,1,3,f); } fclose(f); } } }
    if(GENV("CT2_MENUDBG")){ static int nc=0; if(nc++<2){ printf("--- MENUDBG ntri=%zu\n",ntri); for(size_t i=0;i<ntri&&i<400;i++) if(tris[i].textured){ uint32_t tx=tris[i].tex, ts=tris[i].tsp; printf(" tri%zu list=%d fmt=%d vq=%d mip=%d nt=%d b25=%d addr=%06x %dx%d  u0=%.2f v0=%.2f x=%.0f y=%.0f\n",i,tris[i].list,(tx>>27)&7,(tx>>30)&1,(tx>>31)&1,(tx>>26)&1,(tx>>25)&1,(tx&0x1FFFFF)*8,8<<(ts&7),8<<((ts>>3)&7),tris[i].v[0].u,tris[i].v[0].v,tris[i].v[0].x,tris[i].v[0].y); } } }
    if(GENV("CT2_BIGDBG")&&ntri>3000){ static int nb=0; if(nb++<1){ int shown=0; for(size_t i=0;i<ntri&&shown<25;i++){ Vtx*a=&tris[i].v[0],*b=&tris[i].v[1],*c=&tris[i].v[2]; float x0=a->x<b->x?a->x:b->x; x0=x0<c->x?x0:c->x; float x1=a->x>b->x?a->x:b->x; x1=x1>c->x?x1:c->x; float y0=a->y<b->y?a->y:b->y; y0=y0<c->y?y0:c->y; float y1=a->y>b->y?a->y:b->y; y1=y1>c->y?y1:c->y; if((x1-x0)*(y1-y0)>8000){ shown++; printf(" BIG tri%zu list=%d tex=%d col=%d tsp=%08x tcw=%08x bbox %.0f,%.0f-%.0f,%.0f z=%.4f rgba=%.2f %.2f %.2f %.2f u=%.2f v=%.2f\n",i,tris[i].list,tris[i].textured,tris[i].col,tris[i].tsp,tris[i].tex,x0,y0,x1,y1,a->z,a->r,a->g,a->b,a->a,a->u,a->v); } } } }
    if(GENV("CT2_DBGPIX")){ static int nf=0; int px=0,py=0,want=0; sscanf(GENV("CT2_DBGPIX"),"%d,%d,%d",&px,&py,&want); if(nf++==want || (want==-2 && ta_cur_frame==(unsigned)atoi(GENV("CT2_DBGFRAME"))) || (want==-1 && ntri>100 && ntri<1500 && nf%40==0 && nf<4000)){ printf("--- DBGPIX %d,%d (quadro %d, ntri=%zu)\n",px,py,want,ntri); for(size_t i=0;i<ntri;i++){ Vtx*a=&tris[i].v[0],*b=&tris[i].v[1],*c=&tris[i].v[2]; float x0=fminf(a->x,fminf(b->x,c->x)),x1=fmaxf(a->x,fmaxf(b->x,c->x)),y0=fminf(a->y,fminf(b->y,c->y)),y1=fmaxf(a->y,fmaxf(b->y,c->y)); if(tris[i].list!=2&&px>=x0&&px<=x1&&py>=y0&&py<=y1) printf(" BBOX tri%zu list=%d tex=%d tsp=%08x tcw=%08x a=(%.0f,%.0f,%g) b=(%.0f,%.0f,%g) c=(%.0f,%.0f,%g)\n",i,tris[i].list,tris[i].textured,tris[i].tsp,tris[i].tex,a->x,a->y,a->z,b->x,b->y,b->z,c->x,c->y,c->z); } for(size_t i=0;i<ntri;i++){ Vtx*a=&tris[i].v[0],*b=&tris[i].v[1],*c=&tris[i].v[2]; float den=(b->y-c->y)*(a->x-c->x)+(c->x-b->x)*(a->y-c->y); if(fabsf(den)<1e-6f) continue; float w0=((b->y-c->y)*(px-c->x)+(c->x-b->x)*(py-c->y))/den, w1=((c->y-a->y)*(px-c->x)+(a->x-c->x)*(py-c->y))/den, w2=1-w0-w1; if(w0<0||w1<0||w2<0) continue; uint32_t ts=tris[i].tsp,tx=tris[i].tex; printf(" tri%zu list=%d tex=%d col=%d tsp=%08x tcw=%08x fmt=%d %dx%d src=%d dst=%d usealpha=%d ignoretexa=%d shade=%d vcol=%.2f %.2f %.2f a=%.2f\n",i,tris[i].list,tris[i].textured,tris[i].col,ts,tx,(tx>>27)&7,8<<((ts>>3)&7),8<<(ts&7),ts>>29,(ts>>26)&7,(ts>>20)&1,(ts>>19)&1,(ts>>6)&3,a->r,a->g,a->b,a->a); } } }
    if(GENV("CT2_BLENDSTATS")){ static unsigned long cnt[5][8][8][8][2][2][4]; static int reg2=0; for(size_t i=0;i<ntri;i++){ if(!tris[i].textured) continue; uint32_t ts=tris[i].tsp,tx=tris[i].tex; cnt[tris[i].list%5][(tx>>27)&7][ts>>29][(ts>>26)&7][(ts>>20)&1][(ts>>19)&1][(ts>>6)&3]++; } if(!reg2){ reg2=1; atexit(blend_report_dummy); } blend_ptr=(void*)cnt; }
    if(GENV("CT2_LISTDBG")&&ntri>3000){ static int nq=0; if(nq++<2){ int cl[8]={0}; for(size_t i=0;i<ntri;i++) cl[tris[i].list&7]++; printf("--- LISTDBG ntri=%zu: l0=%d l1=%d l2=%d l3=%d l4=%d | params t0=%llu t1=%llu t2=%llu t4=%llu t5=%llu t7=%llu t3=%llu t6=%llu blen=%zu\n",ntri,cl[0],cl[1],cl[2],cl[3],cl[4],n_param[0],n_param[1],n_param[2],n_param[4],n_param[5],n_param[7],n_param[3],n_param[6],blen); } }
    if(GENV("CT2_VRAMDUMP")&&ta_cur_frame==(unsigned)atoi(GENV("CT2_VRAMDUMP"))){ FILE*f=fopen("/tmp/vramdump.ppm","wb"); fprintf(f,"P6\n1024 512\n255\n"); for(int y=0;y<512;y++) for(int x=0;x<1024;x++){ uint16_t v; memcpy(&v,vram+0x404080+y*2048+x*2,2); uint8_t px[3]={(uint8_t)(((v>>11)&31)<<3),(uint8_t)(((v>>5)&63)<<2),(uint8_t)((v&31)<<3)}; fwrite(px,1,3,f);} fclose(f); }
    if(GENV("CT2_TEXLIST")&&ta_cur_frame==(unsigned)atoi(GENV("CT2_TEXLIST"))){ { static uint32_t ut[4096],us[4096]; static int uc[4096]; int nu=0; for(size_t i=0;i<ntri;i++) if(tris[i].textured){ int k; for(k=0;k<nu;k++) if(ut[k]==tris[i].tex&&us[k]==tris[i].tsp) break; if(k==nu&&nu<4096){ ut[nu]=tris[i].tex; us[nu]=tris[i].tsp; uc[nu]=0; nu++; } if(k<4096) uc[k]++; } printf("--- TEXLIST %d texturas\n",nu); for(int k=0;k<0;k++){ char p[300]; snprintf(p,sizeof p,"/tmp/td/w%02d.ppm",k); FILE*f=fopen(p,"wb"); if(f){ int sw=8<<((us[k]>>3)&7), sh=8<<(us[k]&7); fprintf(f,"P6\n%d %d\n255\n",sw,sh); for(int y=0;y<sh;y++) for(int x=0;x<sw;x++){ uint32_t c=texel_slow(vram,pal,palctl,ut[k],us[k]&~(3u<<15),(x+.5f)/sw,(y+.5f)/sh); uint8_t px[3]={(uint8_t)(c>>16),(uint8_t)(c>>8),(uint8_t)c}; fwrite(px,1,3,f);} fclose(f);} }
        for(int k=0;k<nu&&k<60;k++) printf(" tcw=%08x tsp=%08x fmt=%d vq=%d mip=%d nt=%d U=%d V=%d n=%d\n",ut[k],us[k],(ut[k]>>27)&7,(ut[k]>>30)&1,(ut[k]>>31)&1,(ut[k]>>26)&1,8<<((us[k]>>3)&7),8<<(us[k]&7),uc[k]); } }
    if(GENV("CT2_RAW400")&&ta_cur_frame==(unsigned)atoi(GENV("CT2_RAW400"))){ for(int r=0;r<8;r++){ printf("RAW %06x:",0x400000+r*32); for(int j=0;j<32;j++) printf("%02x",vram[0x400000+r*32+j]); printf("\n"); } }
    if(GENV("CT2_VRAMBIN")&&ta_cur_frame==(unsigned)atoi(GENV("CT2_VRAMBIN"))){ FILE*f=fopen("/tmp/vram.bin","wb"); fwrite(vram,1,8u<<20,f); fclose(f); }
    if(GENV("CT2_TEXALL")&&ta_cur_frame==(unsigned)atoi(GENV("CT2_TEXALL"))){ static uint32_t ut[4096],us[4096]; static int uc[4096],ul[4096]; int nu=0; for(size_t i=0;i<ntri;i++) if(tris[i].textured){ int k; for(k=0;k<nu;k++) if(ut[k]==tris[i].tex&&us[k]==tris[i].tsp) break; if(k==nu&&nu<4096){ ut[nu]=tris[i].tex; us[nu]=tris[i].tsp; uc[nu]=0; ul[nu]=tris[i].list; nu++; } if(k<4096) uc[k]++; } for(int k=0;k<nu;k++){ int sw=8<<((us[k]>>3)&7), sh=8<<(us[k]&7); char p[64]; snprintf(p,sizeof p,"/tmp/td/t%03d.ppm",k); FILE*f=fopen(p,"wb"); fprintf(f,"P6\n%d %d\n255\n",sw,sh); for(int y=0;y<sh;y++) for(int x=0;x<sw;x++){ uint32_t c=texel_slow(vram,pal,palctl,ut[k],us[k]&~(3u<<15),(x+.5f)/sw,(y+.5f)/sh); uint8_t px[3]={(uint8_t)(c>>16),(uint8_t)(c>>8),(uint8_t)c}; fwrite(px,1,3,f);} fclose(f); printf("TEX %03d tcw=%08x tsp=%08x L%d n=%d %dx%d\n",k,ut[k],us[k],ul[k],uc[k],sw,sh);} }
    if(GENV("CT2_TEXDUMP")&&GENV("CT2_POLYDUMP")&&ta_cur_frame==(unsigned)atoi(GENV("CT2_POLYDUMP"))){ uint32_t want=strtoul(GENV("CT2_TEXDUMP"),0,16); for(size_t i=0;i<ntri;i++) if(tris[i].textured&&tris[i].tex==want){ uint32_t us=tris[i].tsp; int sw=8<<((us>>3)&7), sh=8<<(us&7); FILE*f=fopen("/tmp/td.ppm","wb"); fprintf(f,"P6\n%d %d\n255\n",sw,sh); for(int y=0;y<sh;y++) for(int x=0;x<sw;x++){ uint32_t c=texel_slow(vram,pal,palctl,want,us&~(3u<<15),(x+.5f)/sw,(y+.5f)/sh); uint8_t px[3]={(uint8_t)(c>>16),(uint8_t)(c>>8),(uint8_t)c}; fwrite(px,1,3,f);} fclose(f); printf("TEXDUMP %dx%d tsp=%08x\n",sw,sh,us); break; } }
    if(GENV("CT2_TRIDUMP")&&ta_cur_frame==(unsigned)atoi(GENV("CT2_TRIDUMP"))){ int cnt500=0,cnt30=0; for(size_t i=0;i<ntri;i++){ Tri*t=&tris[i]; int pr=0; if(t->tex==0x500c97ccu&&cnt500<2){cnt500++;pr=1;} if((t->tex==0x300d0604u||t->tex==0x301042bcu||t->tex==0x300d0624u)&&cnt30<4){cnt30++;pr=1;} if(pr) printf("TRI %zu list=%d tsp=%08x tcw=%08x tex=%d uv16=%d col=%d isp=%08x snap=%d  v0=(%.2f %.2f z=%g u=%g v=%g rgba %.2f %.2f %.2f %.2f) v1=(%.2f %.2f z=%g u=%g v=%g) v2=(%.2f %.2f z=%g u=%g v=%g)\n",i,t->list,t->tsp,t->tex,t->textured,t->uv16,t->col,t->isp,t->snap,t->v[0].x,t->v[0].y,t->v[0].z,t->v[0].u,t->v[0].v,t->v[0].r,t->v[0].g,t->v[0].b,t->v[0].a,t->v[1].x,t->v[1].y,t->v[1].z,t->v[1].u,t->v[1].v,t->v[2].x,t->v[2].y,t->v[2].z,t->v[2].u,t->v[2].v); } }
    if(GENV("CT2_GROUPS")&&ta_cur_frame==(unsigned)atoi(GENV("CT2_GROUPS"))){ size_t i=0; while(i<ntri){ size_t j=i; float x0=1e9,y0=1e9,x1=-1e9,y1=-1e9; while(j<ntri&&tris[j].tex==tris[i].tex&&tris[j].tsp==tris[i].tsp&&tris[j].list==tris[i].list){ for(int k=0;k<3;k++){ if(tris[j].v[k].x<x0)x0=tris[j].v[k].x; if(tris[j].v[k].x>x1)x1=tris[j].v[k].x; if(tris[j].v[k].y<y0)y0=tris[j].v[k].y; if(tris[j].v[k].y>y1)y1=tris[j].v[k].y; } j++; } printf("G L%d tcw=%08x tsp=%08x n=%zu bb=%.0f,%.0f-%.0f,%.0f rgba=%.2f,%.2f,%.2f,%.2f\n",tris[i].list,tris[i].tex,tris[i].tsp,j-i,x0,y0,x1,y1,tris[i].v[0].r,tris[i].v[0].g,tris[i].v[0].b,tris[i].v[0].a); i=j; } }
    if(GENV("CT2_FONTDUMP")&&ta_cur_frame==(unsigned)atoi(GENV("CT2_FONTDUMP"))){ for(size_t i=0;i<ntri;i++) if(tris[i].textured&&tris[i].tex==0x500c97ccu){ uint32_t us=tris[i].tsp; int sw=8<<((us>>3)&7), sh=8<<(us&7); FILE*f=fopen("/tmp/font_alpha.pgm","wb"); fprintf(f,"P5\n%d %d\n255\n",sw,sh); for(int y=0;y<sh;y++) for(int x=0;x<sw;x++){ uint32_t c=texel_slow(vram,pal,palctl,tris[i].tex,us&~(3u<<15),(x+.5f)/sw,(y+.5f)/sh); fputc((int)(c>>24),f);} fclose(f); f=fopen("/tmp/font_uv.txt","w"); for(size_t j=0;j<ntri;j++) if(tris[j].textured&&tris[j].tex==0x500c97ccu){ fprintf(f,"%zu %g,%g %g,%g %g,%g xy %g,%g %g,%g %g,%g\n",j,tris[j].v[0].u,tris[j].v[0].v,tris[j].v[1].u,tris[j].v[1].v,tris[j].v[2].u,tris[j].v[2].v,tris[j].v[0].x,tris[j].v[0].y,tris[j].v[1].x,tris[j].v[1].y,tris[j].v[2].x,tris[j].v[2].y);} fclose(f); break; } }
    if(GENV("CT2_POLYDUMP")&&ta_cur_frame>=(unsigned)atoi(GENV("CT2_POLYDUMP"))&&ta_cur_frame<(unsigned)atoi(GENV("CT2_POLYDUMP"))+2&&ntri>5){ for(size_t i=0;i<ntri;i++){ Tri*t=&tris[i]; if(t->tsp==0x208824c9u) continue; float x0=1e9,x1=-1e9,y0=1e9,y1=-1e9; for(int j=0;j<3;j++){ if(t->v[j].x<x0)x0=t->v[j].x; if(t->v[j].x>x1)x1=t->v[j].x; if(t->v[j].y<y0)y0=t->v[j].y; if(t->v[j].y>y1)y1=t->v[j].y; } if(GENV("CT2_UVALL")) printf("  UV %g,%g %g,%g %g,%g xy %g,%g %g,%g %g,%g\n",t->v[0].u,t->v[0].v,t->v[1].u,t->v[1].v,t->v[2].u,t->v[2].v,t->v[0].x,t->v[0].y,t->v[1].x,t->v[1].y,t->v[2].x,t->v[2].y); printf("P%03zu L%d tx%d tsp=%08x tcw=%08x isp=%08x bb=%.0f,%.0f-%.0f,%.0f z=%g rgba=%.2f,%.2f,%.2f,%.2f\n",i,t->list,t->textured,t->tsp,t->tex,t->isp,x0,y0,x1,y1,t->v[0].z,t->v[0].r,t->v[0].g,t->v[0].b,t->v[0].a); } }
    tframe++;
    snap_used=0;
    /* lista translucida: o PVR ordena do mais longe para o mais perto (auto-sort); empates mantem a ordem original */
    if(!GENV("CT2_NOSORT")){ size_t nt=0; for(size_t i=0;i<ntri;i++) if(tris[i].list==2) nt++;
        if(nt>1){ SK *sk=(SK*)malloc(nt*sizeof(SK)); size_t *slot=(size_t*)malloc(nt*sizeof(size_t)); Tri *cp=(Tri*)malloc(nt*sizeof(Tri));
            if(sk&&slot&&cp){ size_t n2=0; for(size_t i=0;i<ntri;i++) if(tris[i].list==2){ slot[n2]=i; cp[n2]=tris[i]; sk[n2].idx=n2; sk[n2].key=(tris[i].v[0].z+tris[i].v[1].z+tris[i].v[2].z)/3.f; n2++; }
                qsort(sk,n2,sizeof(SK),sk_cmp);
                for(size_t q=0;q<n2;q++) tris[slot[q]]=cp[sk[q].idx]; }
            free(sk); free(slot); free(cp); } }
    { static float zk=-1; if(zk<0){ const char*e=GENV("CT2_ZCL"); zk=e?(float)atof(e):1.f; } if(zk!=1.f) for(size_t i=0;i<ntri;i++) for(int k=0;k<3;k++) if(fabsf(tris[i].v[k].z-3.3333333f)<1e-4f) tris[i].v[k].z*=zk; }
    { const char*e=GENV("CT2_SKIPTEX"); if(e){ uint32_t sk=(uint32_t)strtoul(e,0,16); size_t j=0; for(size_t i=0;i<ntri;i++){ if(tris[i].tex==sk) continue; if(j!=i) tris[j]=tris[i]; j++; } ntri=j; } }
    { const char*e=GENV("CT2_PERT"); if(e){ double eps=atof(e); for(size_t i=0;i<ntri;i++) for(int k=0;k<3;k++) if(tris[i].v[k].z>3.3f){ tris[i].v[k].x=(float)(tris[i].v[k].x*(1+eps)); tris[i].v[k].y=(float)(tris[i].v[k].y*(1+eps)); } } }
    { static int bi=-1; if(bi<0){ bi=1; const char*e=GENV("CT2_BIASMIP"); if(e) ta_bias_mip=(float)atof(e); } }
    { static int hb=-1; if(hb<0) hb=GENV("CT2_HIDEBIG")!=0; if(hb) ta_dbg[2]=1; }
    if(ta_dbg[2]){ size_t j=0; for(size_t i=0;i<ntri;i++){ Tri*t=&tris[i]; int big=0; for(int k=0;k<3;k++) if(t->v[k].z>3.3f||fabsf(t->v[k].x)>20000.f||fabsf(t->v[k].y)>20000.f) big=1; if(big) continue; if(j!=i) tris[j]=tris[i]; j++; } ntri=j; }
    if(GENV("CT2_UIDBG")){ unsigned fr=(unsigned)atoi(GENV("CT2_UIDBG")); static unsigned lastf=0; static int nu=0; if(ta_cur_frame>=fr&&nu<3&&(lastf==0||ta_cur_frame>=lastf+50)){ lastf=ta_cur_frame; nu++; int cu[5]={0},cn[5]={0}; float zmin3=1e9f,zmax3=-1e9f,zminu=1e9f,zmaxu=-1e9f; for(size_t i=0;i<ntri;i++){ Tri*t=&tris[i]; int ui=1; float zl=1e9f,zh=-1e9f; for(int q=0;q<3;q++){ if(t->v[q].z<1.0f) ui=0; zl=fminf(zl,t->v[q].z); zh=fmaxf(zh,t->v[q].z);} int l=t->list%5; if(ui){ cu[l]++; zminu=fminf(zminu,zl); zmaxu=fmaxf(zmaxu,zh);} else { cn[l]++; zmin3=fminf(zmin3,zl); zmax3=fmaxf(zmax3,zh);} } printf("[uidbg] f%u ntri=%zu ui(list0..4)=%d %d %d %d %d  3d=%d %d %d %d %d zui=[%g,%g] z3d=[%g,%g]\n",ta_cur_frame,ntri,cu[0],cu[1],cu[2],cu[3],cu[4],cn[0],cn[1],cn[2],cn[3],cn[4],zminu,zmaxu,zmin3,zmax3); } }
    int dual3d=0; size_t nui=0, ntri_all=ntri; static Tri *uibuf=0; static size_t uicap=0;
    if(dual_on&&!use_gpu){ size_t n0=0; for(size_t i=0;i<ntri;i++) if(tris[i].list==0) n0++;
        if(n0<4){ /* quadro so de menu/HUD: tudo na escala maxima, sem AA (texto nitido) */ RS=SOUT; RW=OW=ta_fb_w=DFW; RH=OH=ta_fb_h=DFH; aa_skip=1; all_ui_f=1; }
        else { dual3d=1; if(uicap<ntri){ uicap=ntri+4096; uibuf=(Tri*)realloc(uibuf,uicap*sizeof(Tri)); } size_t j=0;
            for(size_t i=0;i<ntri;i++){ Tri*t=&tris[i]; if(t->v[0].z>=1.f&&t->v[1].z>=1.f&&t->v[2].z>=1.f) uibuf[nui++]=*t; else { if(j!=i) tris[j]=*t; j++; } } ntri=j; } }
    ta_apply_canvas();
    { static float vcl=-1.f; if(vcl<0){ const char*e=GENV("CT2_VCLAMP"); vcl=e?(float)atof(e):0.f; } if(vcl>0) for(size_t i=0;i<ntri;i++) for(int k2=0;k2<3;k2++){ Vtx*q=&tris[i].v[k2]; if(q->x<-vcl) q->x=-vcl; if(q->x>vcl) q->x=vcl; if(q->y<-vcl) q->y=-vcl; if(q->y>vcl) q->y=vcl; } }
    for(size_t i=0;i<ntri;i++) setplanes(&tris[i]);
    { const char*e=GENV("CT2_TRIRANGE"); if(e){ int a=0,b=0; sscanf(e,"%d,%d",&a,&b); for(int i=a;i<=b&&i<(int)ntri;i++){ Tri*t=&tris[i]; printf("[tri] #%d list=%d tex=%08x tsp=%08x isp=%08x tx%d cull?  (%.1f,%.1f,z%.4f uv %.3f,%.3f) (%.1f,%.1f,z%.4f uv %.3f,%.3f) (%.1f,%.1f,z%.4f uv %.3f,%.3f)\n",i,t->list,t->tex,t->tsp,t->isp,t->textured,t->v[0].x,t->v[0].y,t->v[0].z,t->v[0].u,t->v[0].v,t->v[1].x,t->v[1].y,t->v[1].z,t->v[1].u,t->v[1].v,t->v[2].x,t->v[2].y,t->v[2].z,t->v[2].u,t->v[2].v); } } }
    { const char*e=GENV("CT2_TRITEX"); if(e){ uint32_t want=strtoul(e,0,16); for(size_t i=0;i<ntri;i++){ Tri*t=&tris[i]; if(t->tex!=want) continue; printf("[tritex] #%zu list=%d tsp=%08x isp=%08x",i,t->list,t->tsp,t->isp); for(int k2=0;k2<3;k2++) printf(" (%.1f,%.1f,z%.4f uv %.3f,%.3f)",t->v[k2].x,t->v[k2].y,t->v[k2].z,t->v[k2].u,t->v[k2].v); printf("\n"); } } }
    { const char*e=GENV("CT2_TRIAT"); if(e&&(!GENV("CT2_TRIFRAME")||ta_cur_frame==(unsigned)atoi(GENV("CT2_TRIFRAME")))){ float px=0,py=0; sscanf(e,"%f,%f",&px,&py); for(size_t i=0;i<ntri;i++){ Tri*t=&tris[i]; Vtx*a=&t->v[0],*b=&t->v[1],*c=&t->v[2]; double den=((double)b->y-c->y)*((double)a->x-c->x)+((double)c->x-b->x)*((double)a->y-c->y); if(fabs(den)<1e-9) continue; double w0=(((double)b->y-c->y)*(px-c->x)+((double)c->x-b->x)*(py-c->y))/den, w1=(((double)c->y-a->y)*(px-c->x)+((double)a->x-c->x)*(py-c->y))/den, w2=1-w0-w1; if(w0>=-1e-6&&w1>=-1e-6&&w2>=-1e-6) printf("[triat] f%u rgba %.2f,%.2f,%.2f,%.2f / %.2f,%.2f,%.2f,%.2f / %.2f,%.2f,%.2f,%.2f zpix=%g #%zu list=%d tex=%08x tsp=%08x isp=%08x tex?%d  (%.2f,%.2f,z%.4f uv %.3f,%.3f) (%.2f,%.2f,z%.4f uv %.3f,%.3f) (%.2f,%.2f,z%.4f uv %.3f,%.3f)\n",ta_cur_frame,a->r,a->g,a->b,a->a,b->r,b->g,b->b,b->a,c->r,c->g,c->b,c->a,t->zc+t->zx*(px-c->x)+t->zy*(py-c->y),i,t->list,t->tex,t->tsp,t->isp,t->textured,a->x,a->y,a->z,a->u,a->v,b->x,b->y,b->z,b->u,b->v,c->x,c->y,c->z,c->u,c->v); } } }
    assign_tc(vram,pal,palctl);
    if(GENV("CT2_GIANT")){ float th=(float)atof(GENV("CT2_GIANT")); for(size_t i=0;i<ntri;i++){ Tri*t=&tris[i]; float m=0; for(int k=0;k<3;k++){ m=fmaxf(m,fmaxf(fabsf(t->v[k].x),fabsf(t->v[k].y))); } if(m>th&&t->list!=2){ t->textured=0; t->tc=0; t->tsp&=~(1u<<20); for(int k=0;k<3;k++){ t->v[k].r=1; t->v[k].g=0; t->v[k].b=1; t->v[k].a=1; } } } }
    if(GENV("CT2_FLATDBG")){ static uint32_t seen[512]; static int ns=0; for(size_t i=0;i<ntri;i++){ Tri*t=&tris[i]; if(!t->textured||!t->tc||t->snap) continue; const TC*e=(const TC*)t->tc; if(e->sw<32||e->sh<32) continue;
            float x0=fminf(t->v[0].x,fminf(t->v[1].x,t->v[2].x)),x1=fmaxf(t->v[0].x,fmaxf(t->v[1].x,t->v[2].x)),y0=fminf(t->v[0].y,fminf(t->v[1].y,t->v[2].y)),y1=fmaxf(t->v[0].y,fmaxf(t->v[1].y,t->v[2].y)); if((x1-x0)*(y1-y0)<15000.f) continue;
            int k; for(k=0;k<ns;k++) if(seen[k]==t->tex) break; if(k<ns) continue; if(ns<512) seen[ns++]=t->tex;
            double m=0,v=0; int n=0; for(int p=0;p<e->sw*e->sh;p+=7){ uint32_t c=e->d[p]; double g=((c>>16)&255)+((c>>8)&255)+(c&255); m+=g; v+=g*g; n++; } m/=n; v=v/n-m*m;
            printf("FLAT? f%u tcw=%08x tsp=%08x list=%d fmt=%d %dx%d mean=%.0f var=%.1f bbox=%.0f,%.0f-%.0f,%.0f\n",ta_cur_frame,t->tex,t->tsp,t->list,(t->tex>>27)&7,e->sw,e->sh,m/3,v,x0,y0,x1,y1); } }
    if(!use_gpu) for(int i=0;i<RW*RH;i++){ fb[i]=ta_bars_black?0xFF000000u:0xFF202020u; zb[i]=-1.f; }
    { /* plano de fundo: ISP_BACKGND_T aponta para (ISP,TSP,TEX) + 3 vertices na VRAM */
      uint32_t T=ta_reg_backgnd_t; uint32_t base=(ta_reg_param_base&0x00F00000u)+((T&0x00FFFFF8u)>>1)*1u; /* (tag_address<<3 bits) em palavras de 32 bits */
      uint32_t addr=(ta_reg_param_base&0x00F00000u)+(((T>>3)&0x1FFFFFu)*4u); (void)base;
      int skip=(T>>24)&7, shadow=(T>>27)&1, tagoff=T&7; (void)shadow; (void)tagoff;
      int vs=3+skip; if(skip<=5&&addr+12+vs*12<=8u*1024*1024){
        uint32_t wl[64]; for(int k=0;k<64;k++){ uint32_t o=addr+4*k; o=(o&3)|((o&0x3FFFFCu)<<1)|((o&0x400000u)?4u:0u); memcpy(&wl[k],vram+(o&0x7FFFFFu),4);} const uint32_t *w=wl; uint32_t isp=w[0],tsp=w[1],tcw=w[2]; (void)isp;(void)tsp;(void)tcw;
        Vtx v[3]; memset(v,0,sizeof v);
        for(int k=0;k<3;k++){ const uint32_t *p=w+3+k*vs; float f; memcpy(&f,p,4); v[k].x=f; memcpy(&f,p+1,4); v[k].y=f; memcpy(&f,p+2,4); v[k].z=f; v[k].a=v[k].r=v[k].g=v[k].b=1;
            if(skip>=1){ uint32_t c=p[vs-1]; v[k].a=((c>>24)&255)/255.f; v[k].r=((c>>16)&255)/255.f; v[k].g=((c>>8)&255)/255.f; v[k].b=(c&255)/255.f; } }
        if(GENV("CT2_BGSCAN")&&T){ static int cnt=0; if(cnt++==5){ const uint32_t*vw=(const uint32_t*)vram; int shown=0;
            for(int view=0;view<2&&shown<20;view++) for(uint32_t i=0;i+30<2097152&&shown<20;i++){
              for(int st=3;st<=8&&shown<20;st++){ int ok=1; float xs[3],ys[3]; for(int k=0;k<3&&ok;k++){ float x,y,z; uint32_t a=i+k*st; if(view){ uint32_t o0=a*4,o1=(a+1)*4,o2=(a+2)*4; uint32_t m[3]={o0,o1,o2}; uint32_t q[3]; for(int e=0;e<3;e++){ uint32_t o=m[e]; o=(o&3)|((o&0x3FFFFCu)<<1)|((o&0x400000u)?4u:0u); memcpy(&q[e],vram+(o&0x7FFFFF),4);} memcpy(&x,&q[0],4);memcpy(&y,&q[1],4);memcpy(&z,&q[2],4);} else { memcpy(&x,&vw[a],4);memcpy(&y,&vw[a+1],4);memcpy(&z,&vw[a+2],4);} if(!(x>=-4&&x<=700&&y>=-4&&y<=520&&z>=0&&z<=1e-1f&&(z>0||x!=0||y!=0))) ok=0; xs[k]=x;ys[k]=y; }
                if(ok&&!(xs[0]==xs[1]&&ys[0]==ys[1])&&(xs[0]!=xs[2]||ys[0]!=ys[2])&&(xs[0]>600||xs[1]>600||xs[2]>600)&&(ys[0]>440||ys[1]>440||ys[2]>440)){ shown++; printf("SCAN view%d @%06x st%d:",view,i*4,st); for(int k=-3;k<st*3+1;k++) printf(" %08x",vw[i+k]); printf("\n"); } } } } }
        if(GENV("CT2_BGDBG")&&ta_cur_frame==(unsigned)atoi(GENV("CT2_BGDBG"))){ static int n=0; if(n++<2){ printf("RAW:"); for(int k=0;k<24;k++) printf(" %08x",w[k]); printf("\nLIN:"); for(int k=0;k<24;k++){ uint32_t q; memcpy(&q,vram+addr+4*k,4); printf(" %08x",q);} printf("\n"); } }
        if(GENV("CT2_BGDBG")&&ta_cur_frame==(unsigned)atoi(GENV("CT2_BGDBG"))){ static int n=0; if(n++<2) printf("BG: T=%08x base=%08x addr=%06x skip=%d isp=%08x tsp=%08x tcw=%08x v0=(%.1f,%.1f,%g) v1=(%.1f,%.1f) v2=(%.1f,%.1f) col0=%.2f %.2f %.2f\n",T,ta_reg_param_base,addr,skip,isp,tsp,tcw,v[0].x,v[0].y,v[0].z,v[1].x,v[1].y,v[2].x,v[2].y,v[0].r,v[0].g,v[0].b); }
        /* o plano cobre a tela inteira: desenha 2 triangulos gigantes com as cores dos vertices (sem textura) */
        float zz=1e-6f; memcpy(&zz,&ta_reg_backgnd_d,4); (void)zz;
        for(int k=0;k<3;k++) ta_canvas_vtx(&v[k]);
        Tri t; memset(&t,0,sizeof t); t.isp=0x80000000u; t.list=0; t.textured=0; t.v[0]=v[0]; t.v[1]=v[1]; t.v[2]=v[2]; t.v[0].z=t.v[1].z=t.v[2].z=1e-9f;
        setplanes(&t); if(!use_gpu) raster(&t,vram,pal,palctl,0,RH);
        { Tri t2=t; t2.v[0]=v[1]; t2.v[1]=v[2]; t2.v[2]=v[1]; t2.v[2].x=v[1].x+v[2].x-v[0].x; t2.v[2].y=v[1].y+v[2].y-v[0].y; t2.v[0].z=t2.v[1].z=t2.v[2].z=1e-9f; setplanes(&t2); if(!use_gpu) raster(&t2,vram,pal,palctl,0,RH); if(use_gpu){ gpu_bg_add(&t); gpu_bg_add(&t2); } }
        if(!use_gpu) for(int i=0;i<RW*RH;i++) zb[i]=-1.f;
      } }
    static const int order[3]={0,4,2};
    if(use_gpu){ if(!gpu_draw(vram,pal,palctl)){ for(int i=0;i<RW*RH;i++) fb[i]=ta_bars_black?0xFF000000u:0xFF202020u; } } else render_bands(vram,pal,palctl,order);
    if(!dual3d && !snap_used && ntri>1500){ memcpy(lastscene,fb,(size_t)RW*RH*4); ls_w=RW; ls_h=RH; have_scene=1; }
    if(ZT){ for(int l=0;l<5;l++) for(int m=0;m<8;m++) if(zh_t[l][m]){ printf("ZH list=%d dm=%d tests=%u ties=%u tiefail=%u\n",l,m,zh_t[l][m],zh_tie[l][m],zh_tiefail[l][m]); } memset(zh_t,0,sizeof zh_t); memset(zh_tie,0,sizeof zh_tie); memset(zh_tiefail,0,sizeof zh_tiefail); printf("ZTIE frame %u: lose=%u win=%u\n",ta_cur_frame,zt_n,zt_w); ZT=0; }
    if(save_live){ save_live=0; FILE*pf=fopen("glitch_frame.ppm","wb"); if(pf){ fprintf(pf,"P6\n%d %d\n255\n",RW,RH); for(int i=0;i<RW*RH;i++){ unsigned char p3[3]={(unsigned char)(fb[i]>>16),(unsigned char)(fb[i]>>8),(unsigned char)fb[i]}; fwrite(p3,1,3,pf); } fclose(pf); }
        int nsh=0,nmesh=nmv?mvs[nmv-1].mesh+1:0; for(int i=0;i<RW*RH;i++) nsh+=shb[i]; FILE*lf=fopen("glitch_info.txt","w"); if(lf){ fprintf(lf,"quadro %u canvas %dx%d RBX=%.1f RS=%.2f upscale=%d aspect=%d res=%d tris=%zu mv=%zu malhas=%d pixels_receptores=%d sombra=%.2f\n",ta_cur_frame,RW,RH,RBX,RS,ta_upscale,ta_aspect,ta_res_i,ntri,nmv,nmesh,nsh,ta_shad); fclose(lf); } printf("[dump] glitch_frame.ppm + glitch_info.txt\n"); }
    if(dual3d){
        const uint32_t *R=aa_finish(); { UpArg ua={R,OW,OH}; par_run(up_job,&ua,(DFH+15)/16,1); }
        memcpy(fb,upb,(size_t)DFW*DFH*4); RS=SOUT; RW=OW=ta_fb_w=DFW; RH=OH=ta_fb_h=DFH;
        if(nui){ size_t sn=ntri,sm=nmv,sc_=captri; Tri*st=tris; tris=uibuf; ntri=nui; captri=uicap; nmv=0;
            for(int i=0;i<RW*RH;i++) zb[i]=-1.f;
            ta_apply_canvas(); for(size_t i=0;i<ntri;i++) setplanes(&tris[i]); assign_tc(vram,pal,palctl); render_bands(vram,pal,palctl,order);
            tris=st; ntri=sn; nmv=sm; captri=sc_; }
        if(!snap_used && ntri_all>1500){ memcpy(lastscene,fb,(size_t)RW*RH*4); ls_w=RW; ls_h=RH; have_scene=1; }
        return fb; }
    return aa_finish();
}
int ta_render(const char *path,const uint8_t*vram,const uint32_t*pal,uint32_t palctl,unsigned *ntris_out){
    if(!ta_render_frame(vram,pal,palctl,ntris_out)) return 0;
    FILE*f=fopen(path,"wb"); if(!f) return 0; fprintf(f,"P6\n%d %d\n255\n",OW,OH); { const uint32_t*o=aa_finish(); for(int i=0;i<OW*OH;i++){ uint8_t px[3]={(uint8_t)(o[i]>>16),(uint8_t)(o[i]>>8),(uint8_t)o[i]}; fwrite(px,1,3,f); } }
    fclose(f); return 1;
}

/* grava o quadro renderizado na VRAM (FB_W_SOF1): o jogo le de volta o quadro, p.ex. como textura do fundo do menu de pausa */
void ta_writeback(uint8_t *vram,const uint32_t *fb,uint32_t sof,uint32_t stride8,uint32_t ctrl){
    uint32_t base=sof&0x7FFFFFu, st=stride8*8u; int fmt=ctrl&7; if(!fb||st==0) return;
    for(int y=0;y<H;y++){ uint32_t o=base+(uint32_t)y*st; for(int x=0;x<W;x++){ uint32_t p=fb[(y*OH/H)*OW+(x*OW/W)]; uint32_t r=(p>>16)&255,g=(p>>8)&255,b=p&255;
        if(fmt==0||fmt==1){ uint32_t off=o+x*2u; if(off+2>8u*1024*1024) return; uint16_t v= fmt==1? (uint16_t)(((r>>3)<<11)|((g>>2)<<5)|(b>>3)) : (uint16_t)(0x8000|((r>>3)<<10)|((g>>3)<<5)|(b>>3)); memcpy(vram+off,&v,2); }
        else if(fmt==3){ uint32_t off=o+x*4u; if(off+4>8u*1024*1024) return; memcpy(vram+off,&p,4); } } }
}
void ta_writeback_cur(uint8_t *vram,uint32_t sof,uint32_t stride8,uint32_t ctrl){ ta_writeback(vram,fb,sof,stride8,ctrl); }

const char *ta_toggle_dbg(int n){ static const char*nm[8]={"F5 mipmap/LOD desligado","F6 culling desligado","F7 esconde triangulos gigantes (cortados no plano proximo)","","","","",""}; if(n<0||n>2) return ""; ta_dbg[n]=!ta_dbg[n]; static char b[96]; snprintf(b,sizeof b,"%s: %s",nm[n],ta_dbg[n]?"ON":"OFF"); return b; }
