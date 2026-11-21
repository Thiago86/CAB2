#include <stdarg.h>
static void mx_viewlog(const char*fmt,...){ static FILE*lf=0; static int n=0; if(n>400) return; if(!lf){ lf=fopen("view.log","w"); if(!lf) return; fprintf(lf,"# troca de camera (menu VIEW): selecao, toques enviados ao controle 3 e fim\n"); } n++; va_list ap; va_start(ap,fmt); vfprintf(lf,fmt,ap); va_end(ap); fputc('\n',lf); fflush(lf); }
/* menux.inc - entradas NATIVAS nos menus do jogo (incluido por ta.c; usa tris/ntri/addtri).
 * Em vez de um painel separado, injeta triangulos com a propria fonte e as caixas do jogo:
 *   - Options: linha "CHEATS" (TIMER ON/OFF, UNLOCK ALL ON/OFF) entre VIBRATION e EXIT
 *   - Regras (Around/Small Apple): linha "PLAY AS YOU LIKE" acima de EXIT (jogo livre)
 *   - Controller Setting: TYPE C (controle moderno, macros de toque duplo)
 * A entrada (D-pad/A) e filtrada por menux_filter(), chamado depois que o controle e lido. */
#include <stdint.h>
extern int ct2_p3_on; extern uint16_t ct2_p3_btn;
extern int ct2_cheat_notimer, ct2_cheat_unlock; extern int ta_dbg[8];   /* ta_dbg[0] = MIPMAP desligado, [1] = CULLING desligado (opcoes graficas) */

#define TSP_TXT 0x9491a4e4u
typedef struct { char c; int band,s,e; } MxG;
static const MxG mx_tab[]={
 {'A',7,2,18},{'B',7,20,34},{'C',7,36,51},{'D',7,53,67},{'E',7,69,81},{'F',7,83,95},{'G',7,97,112},
 {'H',6,2,16},{'I',6,18,23},{'J',6,25,37},{'K',6,39,54},{'L',6,56,67},{'M',6,69,87},{'N',6,89,104},{'O',6,106,122},
 {'P',5,2,16},{'Q',5,18,34},{'R',5,36,50},{'S',5,52,66},{'T',5,68,82},{'U',5,84,98},{'V',5,100,115},
 {'+',1,82,92},{'-',2,2,13},{'%',2,62,78},
 {'3',3,2,15},{'4',3,17,30},{'5',3,32,45},{'6',3,47,61},{'7',3,63,75},{'8',3,77,91},{'9',3,93,106},
 {'W',4,2,22},{'X',4,24,39},{'Y',4,41,57},{'Z',4,59,73},{'0',4,75,88},{'1',4,90,98},{'2',4,100,113},
};
/* fonte italica branca (menu de pausa, MIN/MAX): textura 500c227c, mesmas faixas de 16 px */
static const MxG mx_itab[]={
 {'A',7,2,13},{'B',7,16,26},{'C',7,29,39},{'D',7,42,52},{'E',7,55,65},{'F',7,68,77},{'G',7,80,91},{'H',7,94,105},{'I',7,108,112},{'J',7,115,124},
 {'K',6,2,13},{'L',6,16,24},{'M',6,27,40},{'N',6,43,54},{'O',6,57,68},{'P',6,71,81},{'Q',6,84,95},{'R',6,98,108},{'S',6,111,121},
 {'T',5,2,12},{'U',5,15,26},{'V',5,29,40},{'W',5,43,57},{'X',5,60,71},{'Y',5,74,85},{'Z',5,88,98},
};
#define MX_SPACE 7
static const MxG *mx_tp=mx_tab; static unsigned mx_tn=sizeof mx_tab/sizeof mx_tab[0];   /* tabela ativa (preta ou italica) */
static void mx_usetab(int ital){ if(ital){ mx_tp=mx_itab; mx_tn=sizeof mx_itab/sizeof mx_itab[0]; } else { mx_tp=mx_tab; mx_tn=sizeof mx_tab/sizeof mx_tab[0]; } }
static const MxG *mx_find(char c){ for(unsigned i=0;i<mx_tn;i++) if(mx_tp[i].c==c) return &mx_tp[i]; return 0; }
static const MxG *mx_find_uv(int band,int s){ for(unsigned i=0;i<mx_tn;i++) if(mx_tp[i].band==band&&mx_tp[i].s==s) return &mx_tp[i]; return 0; }
static float mx_adv(char c,float sc){ if(c==' ') return MX_SPACE*sc; const MxG*g=mx_find(c); return g? (g->e-g->s+1)*sc : MX_SPACE*sc; }
static float mx_textw(const char*s,float sc){ float w=0; size_t n=strlen(s); for(size_t i=0;i<n;i++){ const MxG*g=mx_find(s[i]); if(i==n-1) w+=(g?(g->e-g->s+2):MX_SPACE)*sc; else w+=mx_adv(s[i],sc); } return w; }

/* ---- estado persistente ---- */
static int mx_gtype=0; int ct2_kb_act=0; static int kb_native=0;
static int mx_timer_off=0, mx_unlock=0, mx_ctype=0;   /* ctype: 0 = tipo do jogo, 1 = TYPE C (moderno) */
static int mx_rules_map=0; static int mx_free=0;                                  /* jogo livre ativo (sem cronometro nem aviso de passageiro) */
static int mx_loaded=0;
/* v15zz: teclado configuravel. Acoes: 0 CIMA 1 BAIXO 2 ESQ 3 DIR 4 ACELERAR(R) 5 FREIO(L) 6 A(marcha R) 7 B(marcha D) 8 X 9 Y 10 START */
#define KB_N 11
static const int kb_def[KB_N]={0x26,0x28,0x25,0x27,0x20,0xA0,'Z','C','X','V',0x0D};   /* v15zzg: setas = direcional */
int ct2_kmap[KB_N]={0x26,0x28,0x25,0x27,0x20,0xA0,'Z','C','X','V',0x0D}; float ct2_mouse_x=-1,ct2_mouse_y=-1; int ct2_mouse_click=0, ct2_mouse_moved=0;   /* mouse em coordenadas 640x480 do menu (win.c) */ int ct2_esc=0; int ct2_kmap_custom=0;
int ct2_kbd_cap=0, ct2_kbd_got=0, ct2_kbd_used=0;   /* cap: win.c devolve a proxima tecla em got (-1 = Esc); used: quadros restantes em que o teclado esta em uso (liga os macros modernos) */
static int mx_ke=0, mx_ksel=0, mx_kcool=0, mx_kwait=0;   /* editor: 0 fechado, 1 lista, 2 esperando tecla */
int ct2_ff=0;   /* (desativado: avanco rapido removido) */
static int mx_kcol=0;   /* coluna do editor: 0 teclado, 1 controle */
/* controle (XInput): indice 0..15 = bit de wButtons, 16 = LT, 17 = RT */
static const int pd_def[KB_N]={0,1,2,3,17,16,12,13,14,15,4};
int ct2_pmap[KB_N]={0,1,2,3,17,16,12,13,14,15,4}; int ct2_pmap_custom=0; int ct2_pad_cap=0, ct2_pad_got=-2;   /* pad_got: -2 nada, -1 cancelou, >=0 botao */
static const char *mx_pdname(int p){ static const char*n[18]={"DPAD UP","DPAD DOWN","DPAD LEFT","DPAD RIGHT","START","BACK","L3","R3","LB","RB","GUIDE","UNUSED","BUTTON A","BUTTON B","BUTTON X","BUTTON Y","LT","RT"}; return (p>=0&&p<18)?n[p]:"NONE"; }
static void mx_pchk(void){ ct2_pmap_custom=0; for(int i=0;i<KB_N;i++) if(ct2_pmap[i]!=pd_def[i]) ct2_pmap_custom=1; }
static const char *kb_name[KB_N+2]={"UP","DOWN","LEFT","RIGHT","ACCELERATE R","BRAKE L","R GEAR A","D GEAR B","BUTTON X","BUTTON Y","START","RESET DEFAULT","BACK"};
static const char *mx_vkname(int vk,char*b){
    if((vk>='A'&&vk<='Z')||(vk>='0'&&vk<='9')){ b[0]=(char)vk; b[1]=0; return b; }
    switch(vk){ case 0x20: return "SPACE"; case 0x0D: return "ENTER"; case 0x09: return "TAB"; case 0x08: return "BACKSPACE"; case 0x25: return "ARROW LEFT"; case 0x26: return "ARROW UP"; case 0x27: return "ARROW RIGHT"; case 0x28: return "ARROW DOWN";
        case 0xA0: return "LEFT SHIFT"; case 0xA1: return "RIGHT SHIFT"; case 0xA2: return "LEFT CTRL"; case 0xA3: return "RIGHT CTRL"; case 0xA4: return "LEFT ALT"; case 0xA5: return "RIGHT ALT";
        case 0x2D: return "INSERT"; case 0x2E: return "DELETE"; case 0x24: return "HOME"; case 0x23: return "END"; case 0x21: return "PAGE UP"; case 0x22: return "PAGE DOWN"; case 0x14: return "CAPS LOCK"; case 0x6B: return "NUM PLUS"; case 0x6D: return "NUM MINUS"; case 0x6A: return "NUM TIMES"; case 0x6F: return "NUM DIVIDE"; case 0x6E: return "NUM DOT";
        default: break; }
    if(vk>=0x60&&vk<=0x69){ snprintf(b,16,"NUM %d",vk-0x60); return b; }
    if(vk>=0x70&&vk<=0x87){ snprintf(b,16,"F%d",vk-0x6F); return b; }
    snprintf(b,16,"KEY %d",vk); return b; }
static void mx_kdefault(void){ for(int i=0;i<KB_N;i++) ct2_kmap[i]=kb_def[i]; ct2_kmap_custom=0; for(int i=0;i<KB_N;i++) ct2_pmap[i]=pd_def[i]; ct2_pmap_custom=0; }
static void mx_kchk(void){ ct2_kmap_custom=0; for(int i=0;i<KB_N;i++) if(ct2_kmap[i]!=kb_def[i]) ct2_kmap_custom=1; }
static int mx_pause=0, mx_play=0, mx_psel=-1, mx_plast=2, mx_sens_lv=7, mx_view_opt=0, mx_view_pend=-1, mx_view_cd=0;   /* menu de pausa */
static const char *mx_view_name[6]={"NORMAL","DASHBOARD","ANGLE","SPEEDOMETER","PERSPECTIVE","BACK"};
extern int ta_bri_lv; extern int ta_upscale, ta_aspect, ta_res_i, ta_aa_mode, ta_disp_gen; extern int ta_gpu_mode; extern const int ta_res_tab[10][2];
static void mx_save(void){ FILE*f=fopen("opcoes.ini","w"); if(!f) return; fprintf(f,"timer_off=%d\nunlock=%d\nctype=%d\nlod_off=%d\ncull_off=%d\nres=%d\naspect=%d\nupscale=%d\naa=%d\nsens=%d\nview=%d\ngpu=%d\nbright=%d\n",mx_timer_off,mx_unlock,mx_ctype,ta_dbg[0],ta_dbg[1],ta_res_i,ta_aspect,ta_upscale,ta_aa_mode,mx_sens_lv,mx_view_opt,ta_gpu_mode,ta_bri_lv); fprintf(f,"gtype=%d\n",mx_gtype); fprintf(f,"kmap="); for(int i=0;i<KB_N;i++) fprintf(f,"%d%c",ct2_kmap[i],i<KB_N-1?',':'\n'); fprintf(f,"pmap="); for(int i=0;i<KB_N;i++) fprintf(f,"%d%c",ct2_pmap[i],i<KB_N-1?',':'\n'); fclose(f); }
int ta_toggle_timer(void){ mx_timer_off=!mx_timer_off; ct2_cheat_notimer=mx_timer_off||mx_free; mx_save(); return mx_timer_off; }
void ta_load_settings(void);
static void mx_load(void){ if(mx_loaded) return; mx_loaded=1; int keep=GENV("CT2_KEEPOPT")!=0;
    FILE*f=fopen("opcoes.ini","r"); if(f){ char l[160]; while(fgets(l,sizeof l,f)){ int v;
        if(!strncmp(l,"pmap=",5)){ int k[KB_N]; if(sscanf(l+5,"%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d",k,k+1,k+2,k+3,k+4,k+5,k+6,k+7,k+8,k+9,k+10)==KB_N){ int ok=1; for(int i=0;i<KB_N;i++) if(k[i]<0||k[i]>17) ok=0; if(ok){ for(int i=0;i<KB_N;i++) ct2_pmap[i]=k[i]; mx_pchk(); } } continue; }
        if(!strncmp(l,"gtype=",6)){ int g=0; if(sscanf(l+6,"%d",&g)==1) mx_gtype=g?1:0; }
        if(!strncmp(l,"kmap=",5)){ int k[KB_N]; if(sscanf(l+5,"%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d",k,k+1,k+2,k+3,k+4,k+5,k+6,k+7,k+8,k+9,k+10)==KB_N){ int ok=1; for(int i=0;i<KB_N;i++) if(k[i]<8||k[i]>254) ok=0; { static const int wasd[KB_N]={87,83,65,68,32,160,90,67,88,86,13}; int isw=1; for(int i=0;i<KB_N;i++) if(k[i]!=wasd[i]) isw=0; if(isw) ok=0; }   /* v15zzh: mapa antigo W/A/S/D salvo vira o padrao novo (setas) */
        if(ok){ for(int i=0;i<KB_N;i++) ct2_kmap[i]=k[i]; mx_kchk(); } } continue; }
        if(sscanf(l,"ctype=%d",&v)==1) mx_ctype=v; else if(sscanf(l,"res=%d",&v)==1) ta_res_i=v; else if(sscanf(l,"aspect=%d",&v)==1) ta_aspect=v; else if(sscanf(l,"upscale=%d",&v)==1) ta_upscale=v; else if(sscanf(l,"aa=%d",&v)==1) ta_aa_mode=v; else if(sscanf(l,"sens=%d",&v)==1) mx_sens_lv=v; else if(sscanf(l,"view=%d",&v)==1) (void)v; /* a camera sempre comeca em NORMAL: o jogo abre sempre na camera normal */ else if(sscanf(l,"gpu=%d",&v)==1) ta_gpu_mode=v; else if(sscanf(l,"bright=%d",&v)==1) ta_bri_lv=v;
        else if(keep){ if(sscanf(l,"timer_off=%d",&v)==1) mx_timer_off=v; else if(sscanf(l,"unlock=%d",&v)==1) mx_unlock=v; } } fclose(f); }
    if(!keep){ mx_timer_off=0; mx_unlock=0; }
    { const char*e; if((e=GENV("CT2_UPSCALE"))) ta_upscale=atoi(e); if((e=GENV("CT2_ASPECT"))) ta_aspect=atoi(e); if((e=GENV("CT2_RESI"))) ta_res_i=atoi(e); if((e=GENV("CT2_AA"))) ta_aa_mode=atoi(e); }
    if(ta_res_i<0||ta_res_i>29) ta_res_i=2; if(ta_aspect<0||ta_aspect>2) ta_aspect=0; if(ta_upscale<100) ta_upscale=100; if(ta_upscale>200) ta_upscale=200; if(ta_aa_mode<0||ta_aa_mode>4) ta_aa_mode=0; if(mx_sens_lv<0||mx_sens_lv>7) mx_sens_lv=7; if(mx_view_opt<0||mx_view_opt>5) mx_view_opt=0; if(ta_bri_lv<0||ta_bri_lv>10) ta_bri_lv=5; ta_disp_gen++; }

/* ---- estado da tela atual (preenchido a cada quadro em menux_frame) ---- */
enum { MXS_NONE=0, MXS_MAIN, MXS_OPT, MXS_RULES, MXS_CTRL };
static int mx_screen=MXS_NONE;
static int mx_nrows=0, mx_sel=-1;       /* linhas de barras e a selecionada pelo jogo (EXIT = ultima) */
static int mx_vact=0, mx_vanchor=0;     /* cursor virtual sobre a linha nova */
/*gtype movido*/ static int mx_gtype_unused=0;                  /* tipo mostrado pelo jogo na tela de controle: 'A'=0 'B'=1 */
enum { V_NONE=0, V_CHEATS=1, V_RES=11, V_SCR=12, V_ASP=13, V_UPS=14, V_FPS=15, V_SENS=21, V_VIEW=22, V_BRI=23 };   /* linhas virtuais (mx_vact) */
static const int mx_up_pct[8]={100,114,129,143,157,171,186,200};
static int mx_up_level(void){ int b=0; for(int i=0;i<8;i++) if(ta_upscale>=mx_up_pct[i]-3) b=i; return b; }
static int mx_unlock_sel=0; static int mx_optlast=0;             /* na linha CHEATS: 0 = TIMER selecionado, 1 = UNLOCK ALL */

typedef struct { char t[64]; float x0,x1,y; int first,last; } MxLine;
typedef struct { int first,last; float y0,x0,x1; int sel,dim; } MxRow;

static int mx_isital(const Tri*t){ return t->list==2&&t->textured&&t->tsp==TSP_TXT&&t->tex==0x500c227cu; }
static int mx_isglyph(const Tri*t){ return t->list==2&&t->textured&&t->tsp==TSP_TXT&&t->v[0].r<0.1f&&t->v[0].g<0.1f&&t->v[0].b<0.1f; }
/* barra de menu: fatia 8x32 (tsp 949024c2); independe do endereco da textura */
static int mx_isbart(const Tri*t){ if(t->list!=2||!t->textured||t->tsp!=0x949024c2u) return 0; float y0=fminf(t->v[0].y,fminf(t->v[1].y,t->v[2].y)),y1=fmaxf(t->v[0].y,fmaxf(t->v[1].y,t->v[2].y)); return fabsf(y1-y0-32.f)<0.5f; }
static uint32_t mx_selT[3]={0x301042dcu,0x301042bcu,0x301042fcu}, mx_unsT[3]={0x300d0624u,0x300d0604u,0x300d0644u};

static Tri *mx_new(const Tri*tpl){ Tri tmp=*tpl; if(ntri>=captri){ captri=captri*2+4096; tris=realloc(tris,captri*sizeof *tris); } Tri*t=&tris[ntri++]; *t=tmp; return t; }
/* emite um glifo com a fonte do jogo, usando 'tpl' (um triangulo de glifo existente) como molde */
static void mx_glyph(const Tri*tpl,const MxG*g,float x,float y,float sc){
    float u0=(g->s-1)/128.f,u1=(g->e+1)/128.f,vb=g->band/8.f,vt=vb+0.125f; float w=(g->e-g->s+2)*sc,h=16*sc;
    Tri*a=mx_new(tpl); Tri*b=mx_new(tpl);
    a=&tris[ntri-2]; b=&tris[ntri-1];
    a->v[0].x=x;   a->v[0].y=y;   a->v[0].u=u0; a->v[0].v=vt;
    a->v[1].x=x;   a->v[1].y=y+h; a->v[1].u=u0; a->v[1].v=vb;
    a->v[2].x=x+w; a->v[2].y=y;   a->v[2].u=u1; a->v[2].v=vt;
    b->v[0].x=x+w; b->v[0].y=y;   b->v[0].u=u1; b->v[0].v=vt;
    b->v[1].x=x;   b->v[1].y=y+h; b->v[1].u=u0; b->v[1].v=vb;
    b->v[2].x=x+w; b->v[2].y=y+h; b->v[2].u=u1; b->v[2].v=vb;
}
static void mx_text(const Tri*tpl,const char*s,float cx,float y,float sc){
    float x=cx-mx_textw(s,sc)/2.f; size_t n=strlen(s);
    for(size_t i=0;i<n;i++){ if(s[i]!=' '){ const MxG*g=mx_find(s[i]); if(g) mx_glyph(tpl,g,x,y,sc); } x+=mx_adv(s[i],sc); }
}
static void mx_text_left(const Tri*tpl,const char*s,float x,float y,float sc){ mx_text(tpl,s,x+mx_textw(s,sc)/2.f,y,sc); }
/* retangulo liso (sem textura), mesma mistura das barras do jogo */
static void mx_rect(const Tri*bar,float x0,float y0,float x1,float y1,float r,float g,float b,float a){
    Tri*t1=mx_new(bar); Tri*t2=mx_new(bar); t1=&tris[ntri-2]; t2=&tris[ntri-1];
    t1->textured=0; t2->textured=0; t1->uv16=0; t2->uv16=0;
    float X[4]={x0,x1,x1,x0},Y[4]={y0,y0,y1,y1}; int ix[6]={0,1,2,0,2,3};
    for(int k=0;k<6;k++){ Tri*t=k<3?t1:t2; Vtx*v=&t->v[k%3]; v->x=X[ix[k]]; v->y=Y[ix[k]]; v->r=r; v->g=g; v->b=b; v->a=a; v->u=0; v->v=0; }
}
static void mx_box(const Tri*bar,float x0,float y0,float x1,float y1,float th,float r,float g,float b,float a){
    mx_rect(bar,x0,y0,x1,y0+th,r,g,b,a); mx_rect(bar,x0,y1-th,x1,y1,r,g,b,a); mx_rect(bar,x0,y0,x0+th,y1,r,g,b,a); mx_rect(bar,x1-th,y0,x1,y1,r,g,b,a);
}
static void mx_front(size_t a){   /* v15zz: poe os triangulos a..ntri na frente de tudo o que o jogo ja desenhou (profundidade 1/w maior = mais perto) */
    double zm=0; for(size_t i=0;i<a;i++) for(int k=0;k<3;k++) if(tris[i].v[k].z>zm) zm=tris[i].v[k].z; zm=zm*1.02+1e-4;
    for(size_t i=a;i<ntri;i++){ for(int k=0;k<3;k++) tris[i].v[k].z=(float)zm; tris[i].zc=zm; tris[i].zx=0; tris[i].zy=0; tris[i].isp=(tris[i].isp&0x1fffffffu)|(7u<<29); tris[i].list=2; } }
static void mx_shift(size_t a,size_t b,float dx,float dy){ for(size_t i=a;i<=b&&i<ntri;i++) for(int k=0;k<3;k++){ tris[i].v[k].x+=dx; tris[i].v[k].y+=dy; } }
static void mx_barstyle(size_t a,size_t b,int sel){   /* converte uma barra (tris a..b) para o estilo selecionado/normal */
    for(size_t i=a;i<=b&&i<ntri;i++){ int part=(i-a<2)?0:(b-i<2)?2:1;
        tris[i].tex=sel?mx_selT[part]:mx_unsT[part];
        for(int k=0;k<3;k++){ tris[i].v[k].r=tris[i].v[k].g=tris[i].v[k].b=1.f; tris[i].v[k].a=sel?1.f:0.8f; } }
}

static int mx_getenv_notimer=-1;
static void menux_frame(void){
    mx_load();
    if(mx_getenv_notimer<0) mx_getenv_notimer=GENV("CT2_NOTIMER")?1:0;
    ct2_cheat_unlock=mx_unlock||GENV("CT2_UNLOCK");
    /* ---- 1) decodifica linhas de texto (fonte preta grande) e barras ---- */
    static MxLine ML[96]; int nl=0; static MxRow MR[24]; int nr=0;
    for(size_t i=0;i+1<ntri&&nl<95;){
        Tri*t=&tris[i];
        if(mx_isglyph(t)&&mx_isglyph(&tris[i+1])){
            int band=(int)(t->v[1].v*8.f+0.5f); int s=(int)(t->v[0].u*128.f+0.5f)+1; const MxG*g=mx_find_uv(band,s);
            float x0=t->v[0].x,y=t->v[0].y; float w=tris[i+1].v[2].x-x0;
            MxLine*ln=nl?&ML[nl-1]:0;
            if(ln&&ln->last==(int)i-1&&fabsf(ln->y-y)<0.5f&&x0>=ln->x1-2.f&&x0-ln->x1<(MX_SPACE+6)*3){
                size_t n=strlen(ln->t); float gap=x0-ln->x1; int sp=(int)(gap/ (MX_SPACE)+0.45f); if(gap<4) sp=0; while(sp-->0&&n<62) ln->t[n++]=' '; if(n<62) ln->t[n++]=g?g->c:'?'; ln->t[n]=0; ln->x1=x0+w; ln->last=(int)i+1;
            } else { ln=&ML[nl++]; ln->t[0]=g?g->c:'?'; ln->t[1]=0; ln->x0=x0; ln->x1=x0+w; ln->y=y; ln->first=(int)i; ln->last=(int)i+1; }
            i+=2; continue;
        }
        i++;
    }
    for(size_t i=0;i<ntri&&nr<23;){
        if(mx_isbart(&tris[i])){ size_t j=i; float y0=1e9f,x0=1e9f,x1=-1e9f; while(j<ntri&&mx_isbart(&tris[j])){ for(int k=0;k<3;k++){ if(tris[j].v[k].y<y0) y0=tris[j].v[k].y; if(tris[j].v[k].x<x0) x0=tris[j].v[k].x; if(tris[j].v[k].x>x1) x1=tris[j].v[k].x; } j++; }
            MR[nr].first=(int)i; MR[nr].last=(int)j-1; MR[nr].y0=y0; MR[nr].x0=x0; MR[nr].x1=x1; MR[nr].sel=(tris[i].v[0].a>0.95f); MR[nr].dim=tris[i].v[0].r<0.9f; if(!MR[nr].dim&&j-i>=5){ uint32_t*T=MR[nr].sel?mx_selT:mx_unsT; T[0]=tris[i].tex; T[1]=tris[i+2].tex; T[2]=tris[j-1].tex; } nr++; i=j;
        } else i++;
    }
    /* ---- 2) identifica a tela ---- */
    int scr=MXS_NONE; int has_audio=0,has_ctrl=0,has_rules=0,has_around=0,has_type=0;
    for(int i=0;i<nl;i++){ if(!strcmp(ML[i].t,"AUDIO")) has_audio=1; if(!strcmp(ML[i].t,"CONTROLLER SETTING")) has_ctrl=1; if(strstr(ML[i].t,"NORMAL RULES")) has_rules=1; if(!strcmp(ML[i].t,"AROUND APPLE")) has_around=1; if(!strcmp(ML[i].t,"TYPE")) has_type=1; }
    if(has_audio&&has_ctrl) scr=MXS_OPT; else if(has_rules) scr=MXS_RULES; else if(has_around) scr=MXS_MAIN; else if(has_type&&nr==2) scr=MXS_CTRL;
    mx_screen=scr; mx_nrows=nr; mx_sel=-1; for(int i=0;i<nr;i++) if(MR[i].sel) mx_sel=i;
    if(scr==MXS_MAIN) mx_free=0; if(scr==MXS_NONE&&GENV("CT2_FORCEFREE")&&ta_cur_frame>3500) mx_free=1; { static int lf=-1; if(mx_free!=lf&&GENV("CT2_MENUXDBG")){ lf=mx_free; printf("[menux] frame %u mx_free=%d notimer=%d\n",ta_cur_frame,mx_free,ct2_cheat_notimer); } }
    if(scr!=MXS_OPT&&scr!=MXS_RULES&&!(scr==MXS_NONE&&mx_vact>=V_SENS)) mx_vact=0;
    ct2_cheat_notimer=mx_timer_off||mx_free||mx_getenv_notimer;
    { static int lastl=-9,nlog=0; if(scr!=lastl&&nlog<60){ lastl=scr; nlog++; FILE*lf=fopen("menux.log",nlog==1?"w":"a"); if(lf){ fprintf(lf,"frame %u tela=%d linhas=%d barras=%d glyphs_ok\n",ta_cur_frame,scr,nl,nr); for(int i=0;i<nl&&i<12;i++) fprintf(lf,"   '%s'\n",ML[i].t); fclose(lf); } } }
    if(GENV("CT2_MENUXDBG")){ static int last=-9,lastsel=-9; if(scr!=last||mx_sel!=lastsel){ last=scr; lastsel=mx_sel; printf("[menux] frame %u tela=%d linhas=%d sel=%d vact=%d\n",ta_cur_frame,scr,nr,mx_sel,mx_vact); for(int i=0;i<nl;i++) printf("   linha y=%.0f x=%.1f..%.1f '%s'\n",ML[i].y,ML[i].x0,ML[i].x1,ML[i].t); } }
    if(scr==MXS_OPT&&GENV("CT2_OPTDUMP")){ static int od=0; if(od++==3){ for(size_t i=0;i<ntri;i++){ Tri*t=&tris[i]; float y0=fminf(t->v[0].y,fminf(t->v[1].y,t->v[2].y)); if(y0>95&&y0<205) printf("[opt] #%zu L%d tx%d tsp=%08x tex=%08x uv16=%d col=%d (%.1f,%.1f u%.3f v%.3f) (%.1f,%.1f) (%.1f,%.1f) rgba=%.2f %.2f %.2f %.2f\n",i,t->list,t->textured,t->tsp,t->tex,t->uv16,t->col,t->v[0].x,t->v[0].y,t->v[0].u,t->v[0].v,t->v[1].x,t->v[1].y,t->v[2].x,t->v[2].y,t->v[0].r,t->v[0].g,t->v[0].b,t->v[0].a); } fflush(stdout); } }
    /* ---- em jogo? (visor "game time" amarelo no canto) e menu de pausa (texto italico: EXIT / RETRY / MENU) ---- */
    int hud=0, was_pause=mx_pause; mx_pause=0;
    if(scr==MXS_NONE){
        if(GENV("CT2_HUDDBG")&&(ta_cur_frame==5000||ta_cur_frame==5200)){ for(size_t i=0;i<ntri;i++){ Tri*t=&tris[i]; float xx=fmaxf(t->v[0].x,fmaxf(t->v[1].x,t->v[2].x)), yy=fmaxf(t->v[0].y,fmaxf(t->v[1].y,t->v[2].y)); if(t->textured&&xx<260.f&&yy<130.f) printf("[hud] f%u L%d tsp=%08x rgb=%.2f %.2f %.2f\n",ta_cur_frame,t->list,t->tsp,t->v[0].r,t->v[0].g,t->v[0].b); } }
        for(size_t i=0;i<ntri;i++){ Tri*t=&tris[i]; if(t->textured&&t->list==2&&(t->tsp==0x949004dbu||t->tsp==0x949004e2u)&&t->v[0].r>0.9f&&t->v[0].g>0.9f&&t->v[0].b<0.15f){ float xx=fmaxf(t->v[0].x,fmaxf(t->v[1].x,t->v[2].x)), yy=fmaxf(t->v[0].y,fmaxf(t->v[1].y,t->v[2].y)); if(xx<260.f&&yy<130.f){ hud=1; break; } } }
        typedef struct { char t[24]; float x0,x1,y,r,g,b; int first,last; } PLn; static PLn PL[16]; int np=0; mx_usetab(1);
        for(size_t i=0;i+1<ntri&&np<15;){ Tri*t=&tris[i];
            if(mx_isital(t)&&mx_isital(&tris[i+1])){ int band=(int)(t->v[1].v*8.f+0.5f); int sx=(int)(t->v[0].u*128.f+0.5f)+1; const MxG*g=mx_find_uv(band,sx); float x0=t->v[0].x,y=t->v[0].y,w=tris[i+1].v[2].x-x0; PLn*ln=np?&PL[np-1]:0;
                if(ln&&ln->last==(int)i-1&&fabsf(ln->y-y)<0.5f&&x0>=ln->x1-2.f&&x0-ln->x1<(MX_SPACE+6)){ size_t n=strlen(ln->t); float gap=x0-ln->x1; if(gap>=4.f&&n<22) ln->t[n++]=' '; if(n<22) ln->t[n++]=g?g->c:'?'; ln->t[n]=0; ln->x1=x0+w; ln->last=(int)i+1; }
                else { ln=&PL[np++]; ln->t[0]=g?g->c:'?'; ln->t[1]=0; ln->x0=x0; ln->x1=x0+w; ln->y=y; ln->r=t->v[0].r; ln->g=t->v[0].g; ln->b=t->v[0].b; ln->first=(int)i; ln->last=(int)i+1; }
                i+=2; continue; }
            i++; }
        mx_usetab(0);
        int iE=-1,iR=-1,iM=-1,iV=-1; for(int k=0;k<np;k++){ if(!strcmp(PL[k].t,"EXIT")) iE=k; else if(!strcmp(PL[k].t,"RETRY")) iR=k; else if(!strcmp(PL[k].t,"MENU")) iM=k; else if(!strncmp(PL[k].t,"VIBRATION",9)) iV=k; }
        if(iE>=0&&iR>=0&&iM>=0&&fabsf(PL[iE].x0-PL[iM].x0)<2.f){
            mx_pause=1; int vsel=(iV>=0&&PL[iV].r>0.6f);    /* sem rumble pack a linha VIBRATION fica cinza e o cursor nao para nela */
            mx_plast=vsel?3:2; mx_psel=-1; int rows[4]={iE,iR,iM,vsel?iV:-1}; for(int q=0;q<4;q++) if(rows[q]>=0&&PL[rows[q]].r>0.9f&&PL[rows[q]].g>0.6f&&PL[rows[q]].b<0.3f) mx_psel=q;
            float ylast=(iV>=0)?PL[iV].y:PL[iM].y, x0=PL[iM].x0; int vact=mx_vact;
            if(vact>=V_SENS){ for(int q=0;q<4;q++) if(rows[q]>=0&&q==mx_psel) for(int k=PL[rows[q]].first;k<=PL[rows[q]].last;k++) for(int v=0;v<3;v++){ tris[k].v[v].r=tris[k].v[v].g=tris[k].v[v].b=1.f; } }   /* o cursor do jogo fica "escondido" enquanto a linha nova esta ativa */
            Tri itpl=tris[PL[iM].first]; size_t n0;
            for(int row=0;row<3;row++){ float y=ylast+20.f*(row+1); int on=(vact==(row==2?V_BRI:row?V_VIEW:V_SENS)); float cr=on?1.f:1.f, cg=on?0.77f:1.f, cb=on?0.f:1.f; const char*lb=row==2?"BRIGHT":row?"VIEW":"SENSIBILITY";
                mx_usetab(1); n0=ntri; mx_text_left(&itpl,lb,x0,y,1.f); float tw=mx_textw(lb,1.f);
                if(row==0){ for(int k=0;k<8;k++){ float bx=x0+tw+14.f+k*9.f, hh=3.f+2.f*k; int fill=(k<=mx_sens_lv); mx_rect(&itpl,bx,y+17.f-hh,bx+6.f,y+17.f,fill?cr:0.44f,fill?cg:0.44f,fill?cb:0.44f,1.f); } }
                else if(row==2){ for(int k=0;k<11;k++){ float bx=x0+tw+14.f+k*7.f, hh=3.f+1.4f*k; int fill=(k<=ta_bri_lv); mx_rect(&itpl,bx,y+17.f-hh,bx+5.f,y+17.f,fill?cr:0.44f,fill?cg:0.44f,fill?cb:0.44f,1.f); } }
                else mx_text_left(&itpl,mx_view_name[mx_view_opt%6],x0+tw+12.f,y,1.f);
                mx_usetab(0);
                for(size_t k=n0;k<ntri;k++) if(tris[k].textured) for(int v=0;v<3;v++){ tris[k].v[v].r=cr; tris[k].v[v].g=cg; tris[k].v[v].b=cb; tris[k].v[v].a=1.f; } }
        }
    }
    if(!mx_pause&&was_pause&&mx_vact>=V_SENS) mx_vact=0;
    mx_play=(scr==MXS_NONE&&hud&&!mx_pause);
    { extern int ct2_free_mute; ct2_free_mute=mx_free; }
    /* jogo livre: esconde o aviso "stop and pick up customers!" (textura 512x64 no meio da tela) */
    { static unsigned lw=0; for(size_t i=0;i<ntri;i++) if(tris[i].textured&&tris[i].tex==0x100fa268u){ if(ta_cur_frame>lw+20){ extern void aica_voz_nota(const char*); aica_voz_nota("aviso na tela"); { extern void aica_aviso(unsigned); aica_aviso(ta_cur_frame); } if(GENV("CT2_MXVOICE")) printf("[mxvoice] aviso visivel no quadro %u (free=%d)\n",ta_cur_frame,mx_free); } lw=ta_cur_frame; break; } }
    if(mx_free&&scr==MXS_NONE){ size_t j=0; for(size_t i=0;i<ntri;i++){ Tri*t=&tris[i]; int hide=0;
            if(t->textured&&t->list==2){ float y0=fminf(t->v[0].y,fminf(t->v[1].y,t->v[2].y)),x0=fminf(t->v[0].x,fminf(t->v[1].x,t->v[2].x)),x1=fmaxf(t->v[0].x,fmaxf(t->v[1].x,t->v[2].x));
                if(t->tex==0x100fa268u){ static unsigned lastf=0; if(GENV("CT2_MXVOICE")&&ta_cur_frame>lastf+20) printf("[mxvoice] aviso na tela no quadro %u\n",ta_cur_frame); lastf=ta_cur_frame; hide=1; } else if(t->tsp==0x949004f3u&&y0>100.f&&y0<380.f&&x1-x0>=40.f) hide=1; }   /* o aviso anima (largura varia): so a posicao vertical conta */
            /* visor "game time" (numero amarelo + rotulo) no canto superior esquerdo */
            if(!hide&&t->textured&&t->list==2&&(t->tsp==0x949004dbu||t->tsp==0x949004e2u)&&t->v[0].r>0.9f&&t->v[0].g>0.9f&&t->v[0].b<0.15f){
                float xx=fmaxf(t->v[0].x,fmaxf(t->v[1].x,t->v[2].x)), yy=fmaxf(t->v[0].y,fmaxf(t->v[1].y,t->v[2].y)); if(xx<260.f&&yy<130.f) hide=1; }
            if(!hide){ if(j!=i) tris[j]=tris[i]; j++; } } ntri=j; }
    if(scr==MXS_NONE||scr==MXS_MAIN) return;
    /* molde de glifo preto */
    Tri gtpl; int have=0; for(size_t i=0;i<ntri;i++) if(mx_isglyph(&tris[i])){ gtpl=tris[i]; have=1; break; } if(!have) return;
    /* ---- Options: linhas novas RESOLUTION / ASPECT / UPSCALE / ANTIALIASING (acima de AUDIO) e CHEATS (entre VIBRATION e EXIT); o conjunto todo e comprimido para caber ---- */
    if(scr==MXS_OPT){
        int ex=nr-1, vib=-1, mus=-1; for(int i=0;i<nr;i++){ for(int k=0;k<nl;k++) if(ML[k].last<MR[i].first&&ML[k].y>MR[i].y0&&ML[k].y<MR[i].y0+32){ if(!strcmp(ML[k].t,"VIBRATION")) vib=i; if(!strcmp(ML[k].t,"MUSIC VOLUME")) mus=i; } }
        if(vib<0||ex<0) return;
        mx_optlast=MR[vib].dim? vib-1 : vib;
        const float dy=36.f; float ny=MR[vib].y0+dy;
        /* controle deslizante do MUSIC VOLUME (blocos azuis + rotulos MIN/MAX) serve de molde para o UPSCALE */
        size_t nstart=ntri; size_t sl[64]; int nsl=0, nblk=0;
        if(mus>=0){ for(int pass=0;pass<2;pass++) for(size_t i=0;i<ntri&&nsl<64;i++){ Tri*t=&tris[i]; if(!t->textured) continue; float yy=fminf(t->v[0].y,fminf(t->v[1].y,t->v[2].y)); if(yy<MR[mus].y0-2.f||yy>=MR[mus].y0+34.f) continue;
                int blk=(t->list==0&&t->tsp==0x208824c9u&&fabsf(t->v[0].r-0.1f)<0.03f&&t->v[0].b>0.9f);
                int lab=(t->list==2&&t->tsp==TSP_TXT&&t->v[0].r>0.9f&&t->v[0].g>0.9f&&t->v[0].b>0.9f);
                if(pass==0&&blk){ sl[nsl++]=i; nblk++; } else if(pass==1&&lab) sl[nsl++]=i; } }
        int exfirst=MR[ex].first; for(int k=0;k<nl;k++) if(ML[k].y>MR[ex].y0&&ML[k].y<MR[ex].y0+32&&ML[k].last<MR[ex].first) exfirst=ML[k].first;
        mx_shift(exfirst,MR[ex].last,0,dy);
        int vsel=mx_vact;
        if(vsel){ for(int i=0;i<nr;i++) if(MR[i].sel){ mx_barstyle(MR[i].first,MR[i].last,0); } }
        float cx=(MR[vib].x0+MR[vib].x1)/2.f, dxb=MR[vib].x0-33.f; if(fabsf(dxb)<3.f) dxb=0.f; float xv=478.f+dxb;
        static const char*lbl[5]={"RESOLUTION","SCREEN","ASPECT","UPSCALE","ANTIALIASING"}; static const char*asp[3]={"STRETCH","SIDE BARS","WIDESCREEN EXP"};
        for(int row=0;row<5;row++){   /* linhas novas no alto (v15zzzc: 5 com SCREEN): y virtual -76, -40, -4, 32, 68 */
            float y0=104.f-36.f*(5-row); int vid=V_RES+row;
            size_t n0=ntri; for(int i=MR[vib].first;i<=MR[vib].last;i++) mx_new(&tris[i]);
            mx_shift(n0,ntri-1,0,y0-MR[vib].y0); mx_barstyle(n0,ntri-1,vsel==vid);
            { char lb[32]; if(row==3) snprintf(lb,sizeof lb,"UPSCALE %d%%",ta_upscale); else snprintf(lb,sizeof lb,"%s",lbl[row]); mx_text(&gtpl,lb,cx,y0+9,1.f); }
            char vb[48]; vb[0]=0; if(row==0){ int ri=ta_res_i%10; snprintf(vb,sizeof vb,"%dX%d",ta_res_tab[ri][0],ta_res_tab[ri][1]); }
            else if(row==1) snprintf(vb,sizeof vb,"%s",(ta_res_i/10==2)?"BORDERLESS":(ta_res_i/10==1)?"FULLSCREEN":"WINDOW");
            else if(row==2) snprintf(vb,sizeof vb,"%s",asp[ta_aspect%3]); else if(row==4){ static const char*aan[5]={"OFF","BASIC","FXAA","SSAA 1.5X","SSAA 2X"}; snprintf(vb,sizeof vb,"%s",aan[(ta_aa_mode>=0&&ta_aa_mode<5)?ta_aa_mode:0]); }
            if(vb[0]){ float w=mx_textw(vb,1.f), sc=(w>232.f)?232.f/w:1.f; mx_text(&gtpl,vb,xv,y0+9+(1.f-sc)*8.f,sc); }
            else if(row==3&&nsl>0){   /* UPSCALE: clona o controle deslizante do MUSIC VOLUME */
                /* O molde e o MUSIC VOLUME do jogo, mas ele so desenha os blocos ACESOS: mexer no volume mudava o tamanho da barra de UPSCALE.
                   Guardo um bloco (2 triangulos) e o passo entre blocos quando ha 2+ acesos e monto sempre os 8 blocos. */
                static Tri ctpl[2]; static float cpitch=0.f, cyref=0.f, cxref=0.f; static int chave=0;
                if(nblk>=4){   /* atualiza todo quadro: a tela entra deslizando, entao as posicoes mudam */ float x0=1e9f,x1=1e9f; for(int k=0;k<3;k++){ x0=fminf(x0,tris[sl[0]].v[k].x); x1=fminf(x1,tris[sl[2]].v[k].x); } if(x1-x0>1.f){ ctpl[0]=tris[sl[0]]; ctpl[1]=tris[sl[1]]; cpitch=x1-x0; cyref=MR[mus].y0; cxref=MR[mus].x0; chave=1; } }
                int lv=mx_up_level(); size_t n1=ntri;
                if(chave){
                    for(int blk=0;blk<8;blk++) for(int j=0;j<2;j++){ Tri tmp=ctpl[j]; for(int k=0;k<3;k++){ tmp.v[k].x+=blk*cpitch+MR[mus].x0-cxref; tmp.v[k].y+=MR[mus].y0-cyref; if(blk>lv){ tmp.v[k].r=0.30f; tmp.v[k].g=0.32f; tmp.v[k].b=0.45f; } } mx_new(&tmp); }
                    for(int q=nblk;q<nsl;q++) mx_new(&tris[sl[q]]);   /* rotulos MIN/MAX */
                    mx_shift(n1,ntri-1,0,y0-MR[mus].y0);
                } else {   /* ainda nao vi o molde com 2+ blocos: comportamento antigo */
                    for(int q=0;q<nsl;q++) mx_new(&tris[sl[q]]);
                    mx_shift(n1,ntri-1,0,y0-MR[mus].y0);
                    for(int q=0;q<nblk;q++){ int blk=q/2; if(blk>lv){ Tri*t=&tris[n1+q]; for(int k=0;k<3;k++){ t->v[k].r=0.30f; t->v[k].g=0.32f; t->v[k].b=0.45f; } } }
                }
            }
        }
        { /* linha CHEATS */
            size_t n0=ntri; for(int i=MR[vib].first;i<=MR[vib].last;i++) mx_new(&tris[i]);
            mx_shift(n0,ntri-1,0,dy); mx_barstyle(n0,ntri-1,vsel==V_CHEATS);
            float ry=ny; char b1[40],b2[40]; mx_text(&gtpl,"CHEATS",cx,ry+9,1.f);
            snprintf(b1,sizeof b1,"TIMER STOP %s",mx_timer_off?"ON":"OFF"); snprintf(b2,sizeof b2,"UNLOCK ALL %s",mx_unlock?"ON":"OFF");
            mx_text(&gtpl,b1,478.f,ry-1,0.85f); mx_text(&gtpl,b2,478.f,ry+15,0.85f);
            if(vsel==V_CHEATS) mx_text(&gtpl,"LEFT RIGHT TIMER STOP    A UNLOCK ALL",478.f,ry+33,0.5f);
        }
        /* comprime tudo (entre o titulo e a barra de ajuda) para caber as 4 linhas novas */
        for(size_t i=0;i<ntri;i++){ Tri*t=&tris[i]; float y0=1e9f,y1=-1e9f,x0=1e9f,x1=-1e9f; for(int k=0;k<3;k++){ y0=fminf(y0,t->v[k].y); y1=fmaxf(y1,t->v[k].y); x0=fminf(x0,t->v[k].x); x1=fmaxf(x1,t->v[k].x); }
            if(i<nstart&&(y0<94.f||y1>414.f||x1-x0>=600.f)) continue;   /* so as linhas do jogo da faixa do meio; as novas sempre */
            for(int k=0;k<3;k++){ t->v[k].y=104.f+(t->v[k].y+76.f)*0.69f; t->v[k].x=320.f+(t->v[k].x-320.f)*0.72f; } }
        mx_nrows=nr;
        return;
    }
    /* ---- Regras: linha PLAY AS YOU LIKE (acima de MAP quando existe MAP; senao entre a ultima linha e EXIT) ---- */
    if(scr==MXS_RULES){
        int ex=nr-1; if(ex<1) return; const float dy=36.f; int last=ex-1;
        int ismap=0; for(int k=0;k<nl;k++) if(ML[k].y>MR[last].y0&&ML[k].y<MR[last].y0+32&&ML[k].last<MR[last].first&&!strcmp(ML[k].t,"MAP")) ismap=1;
        mx_rules_map=ismap&&ex>=2;
        int exfirst=MR[ex].first; for(int k=0;k<nl;k++) if(ML[k].y>MR[ex].y0&&ML[k].y<MR[ex].y0+32&&ML[k].last<MR[ex].first) exfirst=ML[k].first;
        int mapfirst=MR[last].first; for(int k=0;k<nl;k++) if(ML[k].y>MR[last].y0&&ML[k].y<MR[last].y0+32&&ML[k].last<MR[last].first) mapfirst=ML[k].first;
        int tpl=(mx_rules_map&&last>=1)?last-1:last;   /* molde da barra: a ultima barra de regras ("10 MINUTES"); com MAP o molde NAO pode ser a barra MAP (estreita) */
        float stepx=(tpl>=1)?MR[tpl].x0-MR[tpl-1].x0:46.f; if(stepx<20.f||stepx>80.f||!mx_rules_map) stepx=46.f;   /* degrau da escada de barras */
        float ny=mx_rules_map?MR[last].y0:MR[last].y0+dy, nx=MR[tpl].x0+stepx;
        size_t n0=ntri; for(int i=MR[tpl].first;i<=MR[tpl].last;i++) mx_new(&tris[i]);
        mx_shift(exfirst,MR[ex].last,0,dy);
        if(mx_rules_map) mx_shift(mapfirst,MR[last].last,0,dy);
        int vsel=mx_vact;
        if(vsel){ for(int i=0;i<nr;i++) if(MR[i].sel) mx_barstyle(MR[i].first,MR[i].last,0); }
        float bw=MR[tpl].x1-MR[tpl].x0, tw; if(bw<1.f) bw=1.f; tw=bw;   /* mesma largura das demais barras */ float extra=tw-bw, cw=9.f, bx=nx; if(bx+tw>634.f) bx=634.f-tw;
        mx_shift(n0,ntri-1,bx-MR[tpl].x0,mx_rules_map?(MR[last].y0-MR[tpl].y0):dy);
        for(size_t q=n0;q<ntri;q++) for(int v=0;v<3;v++){ float d=tris[q].v[v].x-bx;   /* alarga so o miolo: pontas arredondadas e borda mantem o tamanho */
            float nd=(d<=cw)?d:(d>=bw-cw)?d+extra:cw+(d-cw)*(tw-2*cw)/(bw-2*cw); tris[q].v[v].x=bx+nd; }
        mx_barstyle(n0,ntri-1,vsel);
        { const char*lbl="PLAY AS LONG AS YOU LIKE"; float sc=1.f, w1=mx_textw(lbl,1.f); if(w1>tw-26.f) sc=(tw-26.f)/w1; mx_text(&gtpl,lbl,bx+tw/2.f,ny+9+(1.f-sc)*8.f,sc); }
        mx_nrows=nr;
        return;
    }
    /* ---- Controller Setting: TYPE C ---- */
    if(scr==MXS_CTRL){
        { Tri bar; int hb=0; for(size_t i=0;i<ntri;i++) if(mx_isbart(&tris[i])){ bar=tris[i]; hb=1; break; }
          if(hb&&((mx_ctype==0&&mx_gtype==1)||GENV("CT2_KEDIT"))){   /* so no controle B: A e C ficam como eram */
            size_t n0;
            if(!mx_ke){ n0=ntri; mx_text_left(&gtpl,"CUSTOMIZE CONTROLS",395,334,0.55f); mx_text_left(&gtpl,"PAD  PRESS Y",395,350,0.55f); mx_text_left(&gtpl,"KEYBOARD  PRESS ENTER",395,366,0.55f); for(size_t k=n0;k<ntri;k++) if(tris[k].textured) for(int v=0;v<3;v++){ tris[k].v[v].r=tris[k].v[v].g=tris[k].v[v].b=0.f; tris[k].v[v].a=1.f; } }
            else {
                size_t nf0=ntri; mx_rect(&bar,90,84,550,448,0.04f,0.04f,0.08f,1.0f); mx_box(&bar,90,84,550,448,2,1.f,0.77f,0.f,1.f);
                n0=ntri; mx_text_left(&gtpl,"CONTROLS SETUP",110,90,0.8f); mx_text_left(&gtpl,"KEYBOARD USES MODERN CONTROL  TAP ACCELERATE TWICE FOR TURBO DASH",110,110,0.45f);
                mx_text_left(&gtpl,"KEYBOARD",265,130,0.55f); mx_text_left(&gtpl,"CONTROLLER",400,130,0.55f);
                for(int i=0;i<KB_N+2;i++){ float y=150.f+i*20.f; char nb[16]; mx_text_left(&gtpl,kb_name[i],110,y,0.62f);
                    if(i<KB_N){ const char*kn=(mx_ke==2&&i==mx_ksel)?"PRESS A KEY":mx_vkname(ct2_kmap[i],nb); mx_text_left(&gtpl,kn,265,y,0.62f);
                        const char*pn=(mx_ke==3&&i==mx_ksel)?"PRESS A BUTTON":mx_pdname(ct2_pmap[i]); mx_text_left(&gtpl,pn,400,y,0.62f); } }
                mx_text_left(&gtpl,mx_ke>=2?"ESC CANCEL":"ARROWS OR MOUSE SELECT  A OR CLICK CHANGE  B BACK",110,420,0.5f);
                for(size_t k=n0;k<ntri;k++) if(tris[k].textured) for(int v=0;v<3;v++){ tris[k].v[v].r=tris[k].v[v].g=tris[k].v[v].b=1.f; tris[k].v[v].a=1.f; }
                /* destaca a linha selecionada (nome) e a coluna ativa em amarelo */
                for(size_t k=n0;k<ntri;k++) if(tris[k].textured){ float yy=tris[k].v[0].y, xx=tris[k].v[0].x; int row=(int)((yy-150.f+10.f)/20.f); int col=(xx<260.f)?-1:(xx<395.f?0:1);
                    if(yy>=140.f&&yy<416.f&&row==mx_ksel&&(col==-1||col==mx_kcol||mx_ksel>=KB_N)) for(int v=0;v<3;v++){ tris[k].v[v].r=1.f; tris[k].v[v].g=0.77f; tris[k].v[v].b=0.f; } }
                for(int i=0;i<KB_N;i++){ float y=150.f+i*20.f; for(int c=0;c<2;c++){ float x0=c?397.f:262.f, x1=c?546.f:392.f; int sel=(mx_ksel==i&&mx_kcol==c); float th=sel?2.f:1.f; mx_box(&bar,x0,y-3.f,x1,y+17.f,th,sel?1.f:0.45f,sel?0.77f:0.45f,sel?0.f:0.5f,1.f); } }
                mx_front(nf0);
            } } }
        if(mx_ke) return;   /* editor aberto: nao mexe no resto da tela */
        /* glifo do tipo: tris com TEX_TXT em (~320,348) */
        size_t gi=(size_t)-1; for(size_t i=0;i+1<ntri;i++) if(mx_isglyph(&tris[i])&&fabsf(tris[i].v[0].y-348.f)<2.f&&tris[i].v[0].x>300.f&&tris[i].v[0].x<400.f&&mx_isglyph(&tris[i+1])){ gi=i; break; }
        if(gi==(size_t)-1) return;
        int band=(int)(tris[gi].v[1].v*8.f+0.5f); int s=(int)(tris[gi].v[0].u*128.f+0.5f)+1; const MxG*g=mx_find_uv(band,s);
        { int ng=(g&&g->c=='B')?1:0; if(ng!=mx_gtype){ mx_gtype=ng; mx_save(); } }
        if(mx_ctype&&mx_gtype==0){
            const MxG*c=mx_find('C'); float x=tris[gi].v[0].x; float w=c->e-c->s+2;
            float u0=(c->s-1)/128.f,u1=(c->e+1)/128.f; Tri*a=&tris[gi],*b=&tris[gi+1];
            a->v[0].u=u0; a->v[1].u=u0; a->v[2].u=u1; b->v[0].u=u1; b->v[1].u=u0; b->v[2].u=u1;
            a->v[2].x=x+w; b->v[0].x=x+w; b->v[2].x=x+w;
            /* caixas em volta de L / R / B / A (tipo moderno) + explicacao dos macros */
            Tri bar; int hb=0; for(size_t i=0;i<ntri;i++) if(mx_isbart(&tris[i])){ bar=tris[i]; hb=1; break; }
            if(hb){
                const float rr=0.f,gg=0.f,bb=0.f,aa=0.85f;
                mx_box(&bar,82,122,164,150,2,rr,gg,bb,aa);     /* L brake */
                mx_box(&bar,442,122,576,150,2,rr,gg,bb,aa);    /* R accelerator */
                mx_box(&bar,482,218,572,246,2,rr,gg,bb,aa);    /* B D gear */
                mx_box(&bar,442,250,574,278,2,rr,gg,bb,aa);    /* A R gear */
                mx_text_left(&gtpl,"MODERN CONTROL",350,330,0.7f);
                mx_text_left(&gtpl,"TAP R TWICE - D GEAR + GAS",350,372,0.62f);
                mx_text_left(&gtpl,"TAP L TWICE - R GEAR + BRAKE",350,388,0.62f);
            }
        }
        return;
    }
}

/* ---- filtro de entrada (chamado depois que o controle foi lido, 1x por quadro) ---- */
static int mx_pend=0, mx_pulse=0, mx_ph=0, mx_pt=0;   /* macro do jogo livre: 1 = subir ate a 1a linha; 2 = pulso de A */
void menux_filter(uint16_t *pb,uint8_t *rt,uint8_t *lt){
    kb_native=0;
    if(ct2_esc&&mx_screen==MXS_NONE&&!mx_ke) *pb&=(uint16_t)~(1u<<3);   /* ESC = Start (abre/fecha o menu de pausa na gameplay) */
    if(ct2_kb_act){ int a=ct2_kb_act&1,bk=ct2_kb_act&2,rg=ct2_kb_act&4,dg=ct2_kb_act&8;   /* teclado: acoes (acelerar/frear/marchas) viram os botoes certos do tipo de controle do jogo */
        if(mx_play&&!mx_ctype&&mx_gtype==1){ kb_native=1; if(a) *pb&=(uint16_t)~(1u<<2); if(bk) *pb&=(uint16_t)~(1u<<1); if(dg) *rt=255; if(rg) *lt=255; }   /* tipo B: A acelera, B freia, R = D, L = R */
        else { if(a) *rt=255; if(bk) *lt=255; if(rg) *pb&=(uint16_t)~(1u<<2); if(dg) *pb&=(uint16_t)~(1u<<1); } }   /* A/C: gatilhos = pedais; A/B = marchas; menus: A/B = confirmar/voltar */
    static uint16_t prev=0xFFFF, hold=0; uint16_t raw=*pb; uint16_t pr=(uint16_t)(prev&~raw); prev=raw; hold&=(uint16_t)~raw;
    const uint16_t K_UP=1u<<4,K_DN=1u<<5,K_LF=1u<<6,K_RT=1u<<7,K_A=1u<<2,K_B=1u<<1;
    { extern int ct2_quit_req; if(mx_screen==MXS_MAIN&&mx_nrows>=8&&mx_sel==mx_nrows-1&&(pr&(K_A|(1u<<3)))) ct2_quit_req=1; }   /* v15zu: A em EXIT no menu principal encerra o jogo */
    if(ct2_kbd_used>0) ct2_kbd_used--;
    { /* editor de teclas (tela Controller Setting): Y abre; cima/baixo escolhem; A troca; B volta */
      const char*fk=GENV("CT2_FAKEKEY"); static int fkd=0; if(fk&&ct2_kbd_cap&&!fkd){ unsigned f=(unsigned)strtoul(fk,(char**)&fk,10); if(*fk==':') fk++; if(ta_cur_frame>=f){ ct2_kbd_got=(int)strtol(fk,0,10); fkd=1; } }
      if(mx_ke&&(mx_screen!=MXS_CTRL||(!GENV("CT2_KEDIT")&&(mx_ctype!=0||mx_gtype!=1)))){ mx_ke=0; ct2_kbd_cap=0; ct2_pad_cap=0; }
      if(!mx_ke&&mx_screen==MXS_CTRL&&((mx_ctype==0&&mx_gtype==1)||GENV("CT2_KEDIT"))&&(pr&((1u<<9)|(1u<<3)))&&mx_kcool==0){ mx_ke=1; mx_ksel=0; mx_kcol=0; mx_kcool=15; }   /* Y no controle ou Enter (Start) no teclado */
      if(mx_ke){
          if(mx_kcool>0) mx_kcool--;
          else if(mx_ke==1){
              int mclick=0; if(ct2_mouse_x>=90&&ct2_mouse_x<=550&&ct2_mouse_y>=140&&ct2_mouse_y<416){   /* mouse: passar por cima seleciona, clicar atribui */
                  int row=(int)((ct2_mouse_y-147.f)/20.f); if(row<0) row=0; if(row>KB_N+1) row=KB_N+1; int col=(ct2_mouse_x>=395.f)?1:0;
                  if(ct2_mouse_moved||ct2_mouse_click){ mx_ksel=row; if(row<KB_N&&ct2_mouse_x>=262.f) mx_kcol=col; }
                  if(ct2_mouse_click&&(row>=KB_N||ct2_mouse_x>=262.f)) mclick=1; }
              ct2_mouse_click=0; ct2_mouse_moved=0;
              if(mclick) pr|=K_A;
              if(pr&K_UP) mx_ksel=(mx_ksel+KB_N+1)%(KB_N+2); else if(pr&K_DN) mx_ksel=(mx_ksel+1)%(KB_N+2);
              else if(pr&(K_LF|K_RT)) mx_kcol^=1;
              else if(pr&(K_A|(1u<<3))){ if(mx_ksel<KB_N){ if(mx_kcol==0){ mx_ke=2; ct2_kbd_cap=1; ct2_kbd_got=0; } else { mx_ke=3; ct2_pad_cap=1; ct2_pad_got=-2; } mx_kwait=0; }
                  else if(mx_ksel==KB_N){ mx_kdefault(); mx_save(); mx_kcool=20; } else { mx_ke=0; mx_kcool=20; } }
              else if(pr&K_B){ mx_ke=0; mx_kcool=20; }
          } else if(mx_ke==2){
              if(++mx_kwait>900) ct2_kbd_got=-1;
              if(ct2_kbd_got){ int g=ct2_kbd_got; ct2_kbd_got=0; ct2_kbd_cap=0; mx_ke=1; mx_kcool=20;
                  if(g>0){ int old=ct2_kmap[mx_ksel]; for(int i=0;i<KB_N;i++) if(i!=mx_ksel&&ct2_kmap[i]==g) ct2_kmap[i]=old; ct2_kmap[mx_ksel]=g; mx_kchk(); mx_save(); } }
          } else {
              { const char*fp=GENV("CT2_FAKEPAD"); static int fpd=0; if(fp&&!fpd&&mx_kwait>30){ ct2_pad_got=atoi(fp); fpd=1; } }
              if(++mx_kwait>900||ct2_kbd_got==-1) ct2_pad_got=-1;   /* Esc no teclado tambem cancela */
              if(ct2_pad_got!=-2){ int g=ct2_pad_got; ct2_pad_got=-2; ct2_pad_cap=0; ct2_kbd_got=0; mx_ke=1; mx_kcool=20;
                  if(g>=0){ int old=ct2_pmap[mx_ksel]; for(int i=0;i<KB_N;i++) if(i!=mx_ksel&&ct2_pmap[i]==g) ct2_pmap[i]=old; ct2_pmap[mx_ksel]=g; mx_pchk(); mx_save(); } }
          }
      }
      if(mx_ke==0&&mx_kcool>0){ mx_kcool--; *pb=0xFFFF; *rt=0; *lt=0; hold=0; return; }
      if(mx_ke){ *pb=0xFFFF; *rt=0; *lt=0; return; }
    }
    if(pr&&GENV("CT2_MENUXDBG")) printf("[menux] f%u botao raw=%04x pr=%04x tela=%d sel=%d/%d vact=%d\n",ta_cur_frame,raw,pr,mx_screen,mx_sel,mx_nrows,mx_vact);
    if(mx_pend){
        *pb|=hold; pr=0; if(++mx_pt>360){ mx_pt=0; mx_pend=0; return; }
        if(mx_pend==1){            /* sobe ate a 1a linha: pulsos curtos de Cima, parando assim que o cursor chega */
            if(mx_screen!=MXS_RULES) mx_pend=0;
            else if(mx_sel==0){ mx_pend=2; mx_ph=0; }
            else { if(mx_ph<2) *pb&=(uint16_t)~K_UP; mx_ph=(mx_ph+1)%6; }
        } else if(mx_pend==2){     /* solta tudo por 10 quadros e entao aperta A por 10 quadros */
            if(mx_screen!=MXS_RULES){ mx_pend=0; }
            else if(mx_sel!=0){ mx_pend=1; mx_ph=0; }
            else { mx_ph++; if(mx_ph>4&&mx_ph<=9){ *pb&=(uint16_t)~K_A; mx_free=1; } if(mx_ph>9) mx_pend=0; }
        }
        return;
    }
    int vib_last=0;
    if(mx_screen==MXS_OPT){
        int sel=mx_sel; int onexit=(sel==mx_nrows-1); int onvib=(sel==mx_optlast); int onaud=(sel==0);   /* sem rumble pack o jogo pula VIBRATION */
        const uint16_t K_LR=K_LF|K_RT;
        if(!mx_vact){
            if((pr&K_DN)&&onvib){ mx_vact=V_CHEATS; mx_vanchor=0; hold|=K_DN; }
            else if((pr&K_UP)&&onexit){ mx_vact=V_CHEATS; mx_vanchor=1; hold|=K_UP; }
            else if((pr&K_UP)&&onaud){ mx_vact=V_FPS; mx_vanchor=0; hold|=K_UP; }            /* AUDIO -> cima -> ANTIALIASING (ultima das 5 linhas novas) */
        } else if(mx_vact>=V_RES){   /* linhas novas no alto: o cursor do jogo fica em AUDIO */
            hold|=(pr&(K_LR|K_A|(1u<<10)));    /* o jogo nao pode trocar STEREO/MONO enquanto se mexe aqui */
            if(pr&K_UP){ if(mx_vact>V_RES) mx_vact--; hold|=K_UP; }
            else if(pr&K_DN){ if(mx_vact<V_FPS){ mx_vact++; hold|=K_DN; } else { mx_vact=0; hold|=K_DN; } }
            else if(pr&K_LR){ int d=(pr&K_RT)?1:-1; int ch=0;
                if(mx_vact==V_RES){ ta_res_i=(ta_res_i/10)*10+((ta_res_i%10)+d+10)%10; ch=1; }   /* RESOLUTION: so a resolucao */
                else if(mx_vact==V_SCR){ ta_res_i=((ta_res_i/10+d+3)%3)*10+ta_res_i%10; ch=1; }   /* SCREEN: WINDOW / FULLSCREEN / BORDERLESS */
                else if(mx_vact==V_ASP){ ta_aspect=(ta_aspect+d+3)%3; ch=1; }
                else if(mx_vact==V_UPS){ int lv=mx_up_level()+d; if(lv<0) lv=0; if(lv>7) lv=7; ta_upscale=mx_up_pct[lv]; ch=1; }
                else if(mx_vact==V_FPS){ ta_aa_mode=(ta_aa_mode+d+5)%5; ch=1; }
                if(ch){ ta_disp_gen++; mx_save(); } }
        } else {   /* linha CHEATS entre VIBRATION e EXIT; o cursor do jogo fica em VIBRATION (ancora 0) ou EXIT (ancora 1) */
            if(pr&K_UP){ if(mx_vanchor==0){ mx_vact=0; hold|=K_UP; } else { mx_vact=0; } }
            else if(pr&K_DN){ if(mx_vanchor==1){ mx_vact=0; hold|=K_DN; } else { mx_vact=0; } }
            else if(mx_vact==V_CHEATS&&(pr&(K_LF|K_RT))){ mx_timer_off=!mx_timer_off; ct2_cheat_notimer=mx_timer_off||mx_free; mx_save(); printf("[cheat] timer stop %s\n",mx_timer_off?"ON (cronometro parado)":"OFF"); fflush(stdout); hold|=(pr&(K_LF|K_RT)); }
            else if(mx_vact==V_CHEATS&&(pr&(K_A|(1u<<10)))){ mx_unlock=!mx_unlock; ct2_cheat_unlock=mx_unlock; mx_save(); printf("[cheat] unlock all %s\n",mx_unlock?"ON":"OFF"); fflush(stdout); hold|=(pr&(K_A|(1u<<10))); }
        }
        (void)vib_last;
    } else if(mx_screen==MXS_RULES){
        int ex=mx_nrows-1; int before=mx_rules_map?ex-2:ex-1; int sel=mx_sel; int onexit=(sel==(mx_rules_map?ex-1:ex)); int onlast=(sel==before);   /* linha virtual entre "10 MINUTES" e MAP */
        if(!mx_vact){
            if((pr&K_DN)&&onlast){ mx_vact=1; mx_vanchor=0; hold|=K_DN; }
            else if((pr&K_UP)&&onexit){ mx_vact=1; mx_vanchor=1; hold|=K_UP; }
            else if(pr&K_A){ mx_free=0; }
        } else {
            if(pr&K_UP){ if(mx_vanchor==0){ mx_vact=0; hold|=K_UP; } else { mx_vact=0; } }
            else if(pr&K_DN){ if(mx_vanchor==1){ mx_vact=0; hold|=K_DN; } else { mx_vact=0; } }
            else if(pr&K_A){ hold|=K_A; mx_vact=0; mx_pend=1; mx_pulse=0; mx_pt=0; mx_ph=0; }
        }
    } else if(mx_screen==MXS_NONE&&mx_pause){   /* menu de pausa: linhas novas SENSIBILITY e VIEW abaixo da ultima linha do jogo */
        const uint16_t K_LR=K_LF|K_RT; int onlast=(mx_psel==mx_plast), onfirst=(mx_psel==0);
        if(!mx_vact){
            if((pr&K_DN)&&onlast){ mx_vact=V_SENS; mx_vanchor=0; hold|=K_DN; }
            else if((pr&K_UP)&&onfirst){ mx_vact=V_BRI; mx_vanchor=1; hold|=K_UP; }
        } else if(mx_vact>=V_SENS){
            hold|=(pr&(K_LR|K_A|(1u<<10)));
            if(pr&K_UP){ if(mx_vact==V_VIEW){ mx_vact=V_SENS; hold|=K_UP; } else if(mx_vact==V_BRI){ mx_vact=V_VIEW; hold|=K_UP; } else { mx_vact=0; if(mx_vanchor==0) hold|=K_UP; } }
            else if(pr&K_DN){ if(mx_vact==V_SENS){ mx_vact=V_VIEW; hold|=K_DN; } else if(mx_vact==V_VIEW){ mx_vact=V_BRI; hold|=K_DN; } else { mx_vact=0; if(mx_vanchor==1) hold|=K_DN; } }
            else if(pr&K_LR){ int d=(pr&K_RT)?1:-1;
                if(mx_vact==V_SENS){ mx_sens_lv+=d; if(mx_sens_lv<0) mx_sens_lv=0; if(mx_sens_lv>7) mx_sens_lv=7; mx_save(); }
                else if(mx_vact==V_BRI){ ta_bri_lv+=d; if(ta_bri_lv<0) ta_bri_lv=0; if(ta_bri_lv>10) ta_bri_lv=10; mx_save(); }
                else { mx_view_opt=(mx_view_opt+d+6)%6; mx_view_pend=mx_view_opt; mx_view_cd=0; ct2_p3_on=1; mx_save(); } }
        }
    } else if(mx_screen==MXS_CTRL){
        if(pr&(K_LF|K_RT)){
            if(mx_ctype==0&&mx_gtype==0&&(pr&K_LF)){ mx_ctype=1; mx_save(); hold|=K_LF; }              /* A -> (esq) -> C */
            else if(mx_ctype==1&&(pr&K_RT)){ mx_ctype=0; mx_save(); hold|=K_RT; }                       /* C -> (dir) -> A */
            else if(mx_ctype==1&&(pr&K_LF)){ mx_ctype=0; mx_save(); /* C -> (esq) -> B: o jogo vai de A para B */ }
            else if(mx_ctype==0&&mx_gtype==1&&(pr&K_RT)){ mx_ctype=1; mx_save(); /* B -> (dir) -> C: o jogo vai de B para A */ }
        }
    }
    /* ---- macros do TYPE C (controle moderno): toque duplo em R / L ----
       Crazy Dash (manual): com gas e freio soltos, engatar D e acelerar juntos. */
    /* v15zo: toque duplo DELIBERADO: o 1o toque tem de ser curto (<=12 quadros) e o 2o comecar ate 18 quadros depois de soltar; antes bastava soltar e apertar de novo dentro de 30 quadros (pisar no freio/acelerador normalmente disparava a macro: engatava a re e cortava o gas, e o salto saia baixo). Nenhuma macro dispara nem continua enquanto o botao Y (salto) estiver apertado e por 25 quadros depois. */
    { static int rprev=0,lprev=0,rmac=0,lmac=0,rhold=0,lhold=0,rshort=0,lshort=0,rgap=0,lgap=0,ysup=0;
      int r=kb_native? ((ct2_kb_act&1)!=0) : (rprev? (*rt>20) : (*rt>60));  int l=kb_native? ((ct2_kb_act&2)!=0) : (lprev? (*lt>20) : (*lt>60));   /* histerese: toques leves de gatilho analogico contam */
      if(!(*pb&(1u<<9))) ysup=25; else if(ysup>0) ysup--;
      if(rgap>0) rgap--; if(lgap>0) lgap--;
      if(r) rhold++; else { if(rprev){ rshort=(rhold<=12); rgap=18; } rhold=0; }
      if(l) lhold++; else { if(lprev){ lshort=(lhold<=12); lgap=18; } lhold=0; }
      if((mx_ctype||ct2_kbd_used>0)&&mx_screen==MXS_NONE){   /* teclado = controle moderno: tocar duas vezes em ACELERAR = crazy dash */
          if(ysup>0){ rmac=lmac=0; rshort=lshort=0; }
          else{
          if(r&&!rprev){ if(rshort&&rgap>0){ rmac=10; rshort=0; rgap=0; if(GENV("CT2_MENUXDBG")) printf("[macro] f%u crazy dash (D+gas)\n",ta_cur_frame); } }
          if(l&&!lprev){ if(lshort&&lgap>0){ lmac=10; lshort=0; lgap=0; if(GENV("CT2_MENUXDBG")) printf("[macro] f%u R gear + brake\n",ta_cur_frame); } }
          }
          if(rmac>0){ int k=10-rmac; rmac--; if(kb_native){ *lt=0; if(k<2){ *rt=255; *pb|=K_A; } else { *rt=0; *pb&=(uint16_t)~K_A; } *pb|=K_B; } else { *pb&=(uint16_t)~K_B; *lt=0; *rt=(k<2)?0:255; } }   /* B (D gear) primeiro, gas logo em seguida, freio solto */
          if(lmac>0){ int k=10-lmac; lmac--; if(kb_native){ *rt=0; if(k<2){ *lt=255; *pb|=K_B; } else { *lt=0; *pb&=(uint16_t)~K_B; } *pb|=K_A; } else { *pb&=(uint16_t)~K_A; *lt=(k<2)?0:255; *rt=0; } }  /* A (R gear) e freio */
      } else { rmac=lmac=0; }
      rprev=r; lprev=l; }
    /* VIEW: ao voltar para o jogo, aperta START + botao no controle 3 (porta C): A = normal, B = painel, Y = angulo, X = velocimetro */
    if(mx_view_pend>=0){ ct2_p3_btn=0xFFFF; static unsigned vf0=0; static int away=0; static int slotk=-1, slotbtn=-1, m0=0;
        extern int ct2_cam_mode,ct2_cam_sub;
        if(mx_view_cd==0){ vf0=0; away=0; slotk=-1; slotbtn=-1; m0=ct2_cam_mode; mx_view_cd=1; mx_viewlog("selecao %s (opcao %d) no quadro %u, jogo=%d, modo atual=%d",mx_view_name[mx_view_pend%6],mx_view_pend,ta_cur_frame,mx_play,ct2_cam_mode); }
        if(!mx_play){ if(vf0&&++away>45){ mx_viewlog("abortado: jogo pausado de novo no quadro %u",ta_cur_frame); mx_view_pend=-1; vf0=0; } }   /* piscadas curtas de mx_play nao reiniciam */
        else { away=0;
        if(!vf0) vf0=ta_cur_frame?ta_cur_frame:1; int c=(int)(ta_cur_frame-vf0);
        /* LACO FECHADO: o jogo guarda o modo de camera em 0x8c175e58 (medido: 0 normal, 9 ANGLE, 1 DASHBOARD, 2 BACK, 10 PERSPECTIVE, 10 com 0x8c2ac916=0 PERSPECTIVE, com 0x8c2ac916!=0 SPEEDOMETER).
           A cada 40 quadros vemos o modo; se ja e o pedido, termina; senao toca START+botao (B cicla ANGLE>DASHBOARD>BACK, X cicla PERSPECTIVE>SPEEDOMETER, A = normal). */
        static const int bt[6]={2,1,1,10,9,1}; static const char bn[6]={'A','B','B','X','Y','B'}; int v=mx_view_pend%6, m=ct2_cam_mode;
        static int sp=0; if(!sp) sp=GENV("CT2_VSPACE")?atoi(GENV("CT2_VSPACE")):40;
        int spv=(v==5&&!GENV("CT2_VSPACE"))?24:sp;   /* so BACK: toques mais juntos, passa menos tempo por ANGLE/DASHBOARD (o ciclo de B obriga a passar por elas) */
        const int T0=48; int PLEN=(GENV("CT2_PLEN")?atoi(GENV("CT2_PLEN")):6);   /* 6 quadros: valor que funcionou no PC para DASHBOARD/ANGLE; o toque que POUSA em BACK e curto (2), pois no PC o jogo passava de 2 para 9 no quadro seguinte */
        if(v==5&&m==1&&!GENV("CT2_PLEN")) PLEN=2;
        if(c>=T0){ int k=(c-T0)/spv, r=(c-T0)%spv; static int basem=0, okn=0, xs=0; static int pre=0;
            if(slotk<0){ xs=0; okn=0; pre=0; }   /* X no modo 10 nao diz em qual dos 2 estados esta: volta ao normal (A) e conta os toques do X desde o zero */
            if(pre&&m==0) pre=0;
            int ok = v==0?m==0 : v==1?m==1 : v==2?m==9 : v==5?m==2 : v==3?m==10 : m==7;   /* como nas primeiras versoes (v15zc..zl): SPEEDOMETER = 1 toque de START+X; PERSPECTIVE = o 2o toque */   /* SPEEDOMETER = 2o toque do X (o modo continua 10; sem variavel propria achada) */
            if(r==0&&k!=slotk){ slotk=k; slotbtn=-1; basem=m;
                if(k>=14){ mx_viewlog("desisti: modo=%d no quadro %u apos %d toques",m,ta_cur_frame,k); mx_view_pend=-1; vf0=0; }
                else if(pre){ slotbtn=2; mx_viewlog("toque %d: START+A (voltar ao normal) no quadro %u (modo antes=%d)",k+1,ta_cur_frame,m); }
                else if(!ok){ slotbtn=bt[v]; if(bt[v]==10) xs++; mx_viewlog("toque %d: START+%c no quadro %u (modo antes=%d)",k+1,bn[v],ta_cur_frame,m); } }
            /* solta o botao assim que o modo muda (um passo por toque) */
            if(slotbtn>=0&&m!=basem) slotbtn=-1;
            /* so termina quando o alvo se mantem por 12 quadros: o jogo as vezes anda um 2o passo no quadro seguinte (visto em camera.log: 9 e logo 1) */
            if(mx_view_pend>=0){ if(ok&&slotbtn<0) okn++; else okn=0;
                if(okn>=12){ mx_viewlog("pronto: modo=%d no quadro %u apos %d toques",m,ta_cur_frame,xs+k); mx_view_pend=-1; vf0=0; okn=0; } }
            if(mx_view_pend>=0&&slotbtn>=0&&r<PLEN) ct2_p3_btn=(uint16_t)(0xFFFF&~(1u<<3)&~(1u<<slotbtn)); } } }
    *pb|=hold;
}
/* sensibilidade da direcao (so no jogo, nunca nos menus): curva de potencia; o nivel maximo e a resposta original do jogo */
int ta_ingame_play(void){ return mx_play; }
static void mx_senslog(int x,int o,const char*why){ static FILE*lf=0; static unsigned n=0; if(n>6000) return; if(!lf){ lf=fopen("sens.log","w"); if(!lf) return; fprintf(lf,"# sensibilidade da direcao: quadro | em_jogo | nivel(0-7) | analogico_X_bruto -> enviado ao jogo | motivo\n"); } n++; fprintf(lf,"f%u jogo=%d nivel=%d x=%d -> %d  %s\n",ta_cur_frame,mx_play,mx_sens_lv,x,o,why); if((n&15)==0) fflush(lf); }
int ta_sens_apply(int x){
    static int lp=-1,ll=-1; static unsigned lastlog=0; int o=x;
    static float cur=128.f;   /* tambem vale para teclado/digital (0/255): limita a deflexao maxima e a velocidade de giro */
    if(mx_play&&mx_sens_lv<7){ float d=(x-128)/127.f, a=fabsf(d); float g=1.f+(7-mx_sens_lv)*0.17f; a=powf(a,g); a*=0.5f+0.5f*mx_sens_lv/7.f; float tg=128.f+(d<0?-a:a)*127.f;
        float st=4.f+mx_sens_lv*10.f; if(fabsf(tg-128.f)<fabsf(cur-128.f)) st*=3.f;   /* volta ao centro mais rapido */
        if(tg>cur+st) cur+=st; else if(tg<cur-st) cur-=st; else cur=tg;
        o=(int)(cur+0.5f); if(o<0) o=0; if(o>255) o=255; } else cur=(float)x;
    if(mx_play!=lp||mx_sens_lv!=ll){ lp=mx_play; ll=mx_sens_lv; mx_senslog(x,o,"mudanca de estado (em_jogo/nivel)"); fflush(stdout); }
    else if((x<120||x>136)&&ta_cur_frame-lastlog>=20){ lastlog=ta_cur_frame; mx_senslog(x,o,(mx_play&&mx_sens_lv<7)?"curva aplicada":(mx_play?"nivel maximo: sem curva":"fora de jogo: sem curva")); }
    return o; }
static int ta_sens_apply_old(int x){ if(!mx_play||mx_sens_lv>=7) return x; float d=(x-128)/127.f, a=fabsf(d); float g=1.f+(7-mx_sens_lv)*0.17f; a=powf(a,g); int o=(int)(128.f+(d<0?-a:a)*127.f+0.5f); if(o<0) o=0; if(o>255) o=255; return o; }

/* depuracao (harness): CT2_NAV=opt navega sozinho ate a tela Options (START nas telas de abertura, D ate OPTIONS no menu principal, A) */
void menux_nav(uint16_t *pb){
    if(GENV("CT2_VIEWSET")){   /* teste: CT2_VIEWSET="opcao:quadro,..." escolhe a camera do menu VIEW sem passar pelo menu */
        const char*q=GENV("CT2_VIEWSET"); while(*q){ int o=(int)strtol(q,(char**)&q,10); if(*q==':') q++; unsigned f=(unsigned)strtoul(q,(char**)&q,10); if(*q==',') q++; if(ta_cur_frame==f){ mx_view_opt=o%6; mx_view_pend=mx_view_opt; mx_view_cd=0; ct2_p3_on=1; mx_play=1; } } }
    if(GENV("CT2_NAVGAME")){   /* CT2_NAVGAME=<A_ate>,<START_em>,<teclas>: depois do menu principal aperta A ate o quadro A_ate, START no quadro START_em e entao as teclas (U D L R A, 20 quadros cada) */
        static unsigned bf=0; static unsigned t0=0; t0++; static int na=-1,np=0; static char keys[64]; if(na<0){ if(sscanf(GENV("CT2_NAVGAME"),"%d,%d,%63s",&na,&np,keys)<2) { na=1500; np=2000; } if(na<0) na=1500; }
        if(!bf&&mx_screen==MXS_MAIN) bf=ta_cur_frame?ta_cur_frame:1;
        if(!bf){ if(t0%16<4&&mx_screen==MXS_NONE) *pb&=(uint16_t)~(1u<<3); return; }
        unsigned d=ta_cur_frame-bf; if(d<(unsigned)na){ if(d%20<4) *pb&=(uint16_t)~(1u<<2); }
        else if(d>=(unsigned)np&&d<(unsigned)np+4) *pb&=(uint16_t)~(1u<<3);
        else if(d>=(unsigned)np+60){ unsigned k=(d-(unsigned)np-60)/20, ph=(d-(unsigned)np-60)%20; if(ph<4&&k<strlen(keys)){ char c=keys[k]; if(c=='U') *pb&=(uint16_t)~(1u<<4); if(c=='D') *pb&=(uint16_t)~(1u<<5); if(c=='L') *pb&=(uint16_t)~(1u<<6); if(c=='R') *pb&=(uint16_t)~(1u<<7); if(c=='A') *pb&=(uint16_t)~(1u<<2); if(c=='S') *pb&=(uint16_t)~(1u<<3); if(c=='B') *pb&=(uint16_t)~(1u<<1); if(c=='X') *pb&=(uint16_t)~(1u<<10); if(c=='Y') *pb&=(uint16_t)~(1u<<9); } }
        return; }
    static unsigned t=0; t++; if(t%16>=4&&mx_screen!=MXS_OPT) return;
    const uint16_t K_DN=1u<<5,K_A=1u<<2,K_S=1u<<3;
    if(mx_screen==MXS_NONE) *pb&=(uint16_t)~K_S;
    else if(mx_screen==MXS_OPT&&GENV("CT2_NAVOPT")){ static unsigned to=0; to++; const char*q=GENV("CT2_NAVOPT"); unsigned k=(to>60)?(to-60)/24:0; unsigned ph=(to>60)?(to-60)%24:99; if(ph<4&&k<strlen(q)){ char c=q[k]; if(c=='U') *pb&=(uint16_t)~(1u<<4); if(c=='D') *pb&=(uint16_t)~(1u<<5); if(c=='L') *pb&=(uint16_t)~(1u<<6); if(c=='R') *pb&=(uint16_t)~(1u<<7); if(c=='A') *pb&=(uint16_t)~(1u<<2); } }
    else if(mx_screen==MXS_MAIN){ if(mx_sel<4) *pb&=(uint16_t)~K_DN; else if(mx_sel==4&&t%32<4) *pb&=(uint16_t)~K_A; }
}

void ta_load_settings(void){ mx_load(); }
void ta_save_settings(void){ mx_save(); }
int ta_menu_active(void){ return mx_screen!=MXS_NONE; }
int ta_menu_screen(void){ return mx_screen; }
