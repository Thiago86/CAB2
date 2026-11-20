/* Janela, teclado e controle (XInput) do Windows para o executavel do jogo. So Win32 puro (user32/gdi32); XInput carregado em tempo de execucao. */
#include <stdio.h>
#include "win.h"
void menux_filter(unsigned short *pb,unsigned char *rt,unsigned char *lt);
#include <windows.h>
#include <math.h>
#include <string.h>
#include <stdlib.h>
#define FW 640
#define FH 480
extern int ta_res_i, ta_aspect, ta_disp_gen, ta_fb_w, ta_fb_h; extern const int ta_res_tab[10][2]; void ta_load_settings(void); const char *ta_gpu_name(void); void ta_save_settings(void);
static HWND hwnd=0; static int quit=0; static uint32_t pix[3840*2160]; static BITMAPINFO bmi; static int inited=0; static int curw=FW, curh=FH; static int d_gen=0, d_mode=0;
static int win_active(void){ return hwnd&&!IsIconic(hwnd)&&GetForegroundWindow()==hwnd; }   /* v15zzy: minimizado / sem foco = nao le teclado nem controle */
static void disp_restore(void){ if(d_mode){ ChangeDisplaySettingsA(0,0); d_mode=0; } }
static void draw(HDC dc,HWND h){
    RECT rc; GetClientRect(h,&rc); if(rc.right<=0||rc.bottom<=0) return;
    int dx=0,dy=0,dw=rc.right,dh=rc.bottom;
    if(ta_aspect!=0){   /* barras laterais / tela larga: mantem a proporcao do quadro, centralizado, com barras pretas */
        float ca=(float)curw/(float)curh; if((float)dw/(float)dh>ca){ int nw=(int)(dh*ca+.5f); dx=(dw-nw)/2; dw=nw; } else { int nh=(int)(dw/ca+.5f); dy=(dh-nh)/2; dh=nh; }
        HBRUSH bk=(HBRUSH)GetStockObject(BLACK_BRUSH); RECT a={0,0,dx,rc.bottom}, b={dx+dw,0,rc.right,rc.bottom}, c={0,0,rc.right,dy}, d={0,dy+dh,rc.right,rc.bottom};
        if(a.right>a.left) FillRect(dc,&a,bk); if(b.right>b.left) FillRect(dc,&b,bk); if(c.bottom>c.top) FillRect(dc,&c,bk); if(d.bottom>d.top) FillRect(dc,&d,bk); }
    SetStretchBltMode(dc,(dw<curw||dh<curh)?HALFTONE:COLORONCOLOR); SetBrushOrgEx(dc,0,0,0);
    StretchDIBits(dc,dx,dy,dw,dh,0,0,curw,curh,pix,&bmi,DIB_RGB_COLORS,SRCCOPY);
}
static LRESULT CALLBACK wndproc(HWND h,UINT m,WPARAM w,LPARAM l){
    switch(m){
    case WM_CLOSE: quit=1; DestroyWindow(h); return 0;
    case WM_DESTROY: quit=1; disp_restore(); return 0;
    case WM_ERASEBKGND: return 1;
    case WM_PAINT: { PAINTSTRUCT ps; HDC dc=BeginPaint(h,&ps); draw(dc,h); EndPaint(h,&ps); return 0; }
    default: break; }
    return DefWindowProcA(h,m,w,l);
}
static void init(void){
    if(inited) return; inited=1;
    memset(&bmi,0,sizeof bmi); bmi.bmiHeader.biSize=sizeof(BITMAPINFOHEADER); bmi.bmiHeader.biWidth=FW; bmi.bmiHeader.biHeight=-FH; bmi.bmiHeader.biPlanes=1; bmi.bmiHeader.biBitCount=32; bmi.bmiHeader.biCompression=BI_RGB;
    WNDCLASSA wc; memset(&wc,0,sizeof wc); wc.lpfnWndProc=wndproc; wc.hInstance=GetModuleHandleA(0); wc.hCursor=LoadCursorA(0,IDC_ARROW); wc.lpszClassName="CT2Win";
    RegisterClassA(&wc);
    { HMODULE u=LoadLibraryA("user32.dll"); if(u){ typedef BOOL (WINAPI *PD)(void); PD f=(PD)GetProcAddress(u,"SetProcessDPIAware"); if(f) f(); } }   /* sem isso o Windows escala a janela em telas de alta densidade */
    ta_load_settings(); atexit(disp_restore);
    RECT r={0,0,1024,768}; AdjustWindowRect(&r,WS_OVERLAPPEDWINDOW,FALSE);
    hwnd=CreateWindowA("CT2Win","Crazy Taxi 2 (recompilacao estatica)",WS_OVERLAPPEDWINDOW,CW_USEDEFAULT,CW_USEDEFAULT,r.right-r.left,r.bottom-r.top,0,0,wc.hInstance,0);
    if(!hwnd){ quit=1; return; }
    ShowWindow(hwnd,SW_SHOW); UpdateWindow(hwnd); d_gen=0;
}
/* aplica RESOLUTION (janela ou tela cheia) quando a opcao muda */
static void disp_apply(void){
    if(!hwnd||d_gen==ta_disp_gen) return; d_gen=ta_disp_gen;
    int ri=ta_res_i%10, full=ta_res_i>=10; int w=ta_res_tab[ri][0], h=ta_res_tab[ri][1];
    if(full){
        DEVMODEA dm; memset(&dm,0,sizeof dm); dm.dmSize=sizeof dm; dm.dmPelsWidth=(DWORD)w; dm.dmPelsHeight=(DWORD)h; dm.dmBitsPerPel=32; dm.dmFields=DM_PELSWIDTH|DM_PELSHEIGHT|DM_BITSPERPEL;
        if(ChangeDisplaySettingsA(&dm,CDS_FULLSCREEN)==DISP_CHANGE_SUCCESSFUL) d_mode=1; else disp_restore();   /* modo nao suportado: tela cheia na resolucao do desktop (a imagem e escalada) */
        SetWindowLongA(hwnd,GWL_STYLE,WS_POPUP|WS_VISIBLE);
        SetWindowPos(hwnd,HWND_TOP,0,0,GetSystemMetrics(SM_CXSCREEN),GetSystemMetrics(SM_CYSCREEN),SWP_FRAMECHANGED|SWP_SHOWWINDOW);
    } else {
        disp_restore(); RECT wa; SystemParametersInfoA(SPI_GETWORKAREA,0,&wa,0); int aw=wa.right-wa.left, ah=wa.bottom-wa.top;
        RECT r={0,0,w,h}; AdjustWindowRect(&r,WS_OVERLAPPEDWINDOW,FALSE); int ww=r.right-r.left, wh=r.bottom-r.top;
        if(ww>aw||wh>ah){ float f=(float)aw/(float)ww; float g=(float)ah/(float)wh; if(g<f) f=g; ww=(int)(ww*f); wh=(int)(wh*f); }   /* janela maior que a tela: reduz mantendo a proporcao */
        SetWindowLongA(hwnd,GWL_STYLE,WS_OVERLAPPEDWINDOW|WS_VISIBLE);
        SetWindowPos(hwnd,HWND_NOTOPMOST,wa.left+(aw-ww)/2,wa.top+(ah-wh)/2,ww,wh,SWP_FRAMECHANGED|SWP_SHOWWINDOW);
    }
    InvalidateRect(hwnd,0,TRUE);
}
void win_request_quit(void){ quit=1; if(hwnd) PostMessageA(hwnd,WM_CLOSE,0,0); }
int win_pump(void){
    init(); disp_apply(); MSG msg;
    while(PeekMessageA(&msg,0,0,0,PM_REMOVE)){ if(msg.message==WM_QUIT){ quit=1; } TranslateMessage(&msg); DispatchMessageA(&msg); }
    { extern int ct2_kbd_cap, ct2_pad_cap, ct2_pad_got; extern int ct2_esc; ct2_esc=0; if(win_active()&&(GetAsyncKeyState(VK_ESCAPE)&0x8000)){ if(ct2_pad_cap) ct2_pad_got=-1; else if(!ct2_kbd_cap) ct2_esc=1; } }   /* v15zzk: ESC nao fecha mais o jogo; vira o Start (menu de pausa) - menux_filter */
    return !quit;
}
int win_present(const uint32_t *fb){
    init(); { int w=ta_fb_w, h=ta_fb_h; if(w<64||h<64||w>3840||h>2160){ w=FW; h=FH; } if(w!=curw||h!=curh){ curw=w; curh=h; bmi.bmiHeader.biWidth=w; bmi.bmiHeader.biHeight=-h; } memcpy(pix,fb,(size_t)w*h*4); }
    if(hwnd){ HDC dc=GetDC(hwnd); draw(dc,hwnd); ReleaseDC(hwnd,dc); }
    return win_pump();
}
static int w_late=0; int win_behind(void){ return w_late; }
static int w_resync=0; void win_resync(void){ w_resync=1; }
void win_throttle(void){
    static LARGE_INTEGER freq, next; static int started=0; LARGE_INTEGER now;
    if(!started){ QueryPerformanceFrequency(&freq); QueryPerformanceCounter(&next); started=1;
        { HMODULE m=LoadLibraryA("winmm.dll"); if(m){ typedef UINT (WINAPI *TBP)(UINT); TBP f=(TBP)GetProcAddress(m,"timeBeginPeriod"); if(f) f(1); } }   /* v15n: sem isso Sleep(1) dorme ~15 ms e o jogo roda lento */ }
    if(w_resync){ QueryPerformanceCounter(&next); w_resync=0; }   /* depois de uma tela de abertura bloqueante, nao tenta recuperar o tempo perdido */
    next.QuadPart+=freq.QuadPart/60;
    QueryPerformanceCounter(&now); w_late=(next.QuadPart-now.QuadPart)<=0;   /* ja estourou o tempo do quadro */
    for(;;){ QueryPerformanceCounter(&now); long long left=next.QuadPart-now.QuadPart; if(left<=0) break; if(left>freq.QuadPart/250) Sleep(1); }
    QueryPerformanceCounter(&now); if(now.QuadPart-next.QuadPart>freq.QuadPart/10) next=now;   /* atrasado demais: nao tenta recuperar */
}
typedef struct { DWORD pkt; WORD btn; BYTE lt,rt; SHORT lx,ly,rx,ry; } XState;
typedef DWORD (WINAPI *XGetState)(DWORD,XState*);
typedef struct { WORD l,r; } XVib;
typedef DWORD (WINAPI *XSetState)(DWORD,XVib*);
static XGetState xget=0; static XSetState xset=0; static int xtried=0; static int vib_last=-1;
double win_now_ms(void);
/* v15zz: telas de abertura do porte (NOW LOADING e distribuidora): arquivos P6 .ppm ao lado do exe; qualquer tecla/Start/clique pula tudo. Retorna 1 se o jogador pulou. */
static int splash_one(const char*path,int ms){
    FILE*f=fopen(path,"rb"); if(!f) return 0; int W=0,H=0,mx=0; if(fscanf(f,"P6 %d %d %d",&W,&H,&mx)!=3||W<16||H<16||W>4096||H>4096||mx!=255){ fclose(f); return 0; } fgetc(f);
    unsigned char*rgb=(unsigned char*)malloc((size_t)W*H*3); uint32_t*px=(uint32_t*)malloc((size_t)W*H*4); if(!rgb||!px||fread(rgb,3,(size_t)W*H,f)!=(size_t)W*H){ fclose(f); free(rgb); free(px); return 0; } fclose(f);
    for(int i=0;i<W*H;i++) px[i]=((uint32_t)rgb[i*3]<<16)|((uint32_t)rgb[i*3+1]<<8)|rgb[i*3+2]; free(rgb);
    init(); BITMAPINFO bi; memset(&bi,0,sizeof bi); bi.bmiHeader.biSize=sizeof(BITMAPINFOHEADER); bi.bmiHeader.biWidth=W; bi.bmiHeader.biHeight=-H; bi.bmiHeader.biPlanes=1; bi.bmiHeader.biBitCount=32; bi.bmiHeader.biCompression=BI_RGB;
    HBRUSH bg=CreateSolidBrush(RGB(0,0,0));   /* barras sempre pretas */
    double t0=win_now_ms(); int skip=0; int armed=0;
    while(win_now_ms()-t0<ms&&!skip){
        if(!win_pump()){ skip=1; break; }
        if(hwnd){ HDC dc=GetDC(hwnd); RECT rc; GetClientRect(hwnd,&rc); if(rc.right>0&&rc.bottom>0){ int dw=rc.right,dh=rc.bottom,dx=0,dy=0; float ia=4.f/3.f; if(ta_aspect==0) ia=(float)dw/(float)dh;   /* STRETCH: preenche a janela; demais modos: 4:3 centralizado com barras */ if((float)dw/(float)dh>ia){ int nw=(int)(dh*ia+.5f); dx=(dw-nw)/2; dw=nw; } else { int nh=(int)(dw/ia+.5f); dy=(dh-nh)/2; dh=nh; }
                RECT a={0,0,dx,rc.bottom},b={dx+dw,0,rc.right,rc.bottom},c={0,0,rc.right,dy},d={0,dy+dh,rc.right,rc.bottom}; if(a.right>a.left) FillRect(dc,&a,bg); if(b.right>b.left) FillRect(dc,&b,bg); if(c.bottom>c.top) FillRect(dc,&c,bg); if(d.bottom>d.top) FillRect(dc,&d,bg);
                SetStretchBltMode(dc,HALFTONE); SetBrushOrgEx(dc,0,0,0); StretchDIBits(dc,dx,dy,dw,dh,0,0,W,H,px,&bi,DIB_RGB_COLORS,SRCCOPY); } ReleaseDC(hwnd,dc); }
        { int any=0; for(int vk=8;vk<255;vk++){ if(vk==VK_ESCAPE||(vk>=0x10&&vk<=0x12)) continue; if(win_active()&&(GetAsyncKeyState(vk)&0x8000)) any=1; }
          if(xget&&win_active()){ for(DWORD idx=0;idx<4;idx++){ XState s; memset(&s,0,sizeof s); if(xget(idx,&s)==0&&(s.btn&0xF0F0)) any=1; } }
          if(!any) armed=1; else if(armed) skip=1; }   /* so conta tecla apertada depois de soltar as anteriores */
        Sleep(10); }
    DeleteObject(bg); free(px); return skip;
}
int win_splash_all(void){ return splash_one("intro_loading.ppm",2600); }   /* NOW LOADING antes do jogo */
int win_splash_adx(void){ int r=splash_one("intro_adx.ppm",3200); win_resync(); return r; }   /* distribuidora ADX/CRI: depois do logo SEGA/Hitmaker, antes do press start */
extern unsigned ct2_vib_pow, ct2_vib_stamp; extern unsigned ta_cur_frame;
/* v15zf: vibracao: o jogo comanda o Jump Pack emulado (forca 0-7); sem novo comando por ~0,7 s o motor para */
static void vib_apply(DWORD idx){
    unsigned p=ct2_vib_pow; if(ta_cur_frame-ct2_vib_stamp>45u) p=0; if(p>7) p=7;
    int v=p?(int)((0.35f+0.65f*(float)p/7.f)*65535.f):0;   /* piso de 35%: forca 1 do jogo seria imperceptivel no motor do XInput */ if(v==vib_last||!xset) return; vib_last=v;
    XVib m; m.l=(WORD)v; m.r=(WORD)(v*3/4); xset(idx,&m);
}
static int key(int vk){ if(!win_active()) return 0; SHORT k=GetAsyncKeyState(vk); return (k&0x8000)!=0||(k&1)!=0; }   /* v15s: bit 0 = foi apertada desde a ultima consulta (nao perde toques curtos entre quadros) */
/* controle generico (DirectInput/HID) via winmm, usado quando nao ha controle XInput */
typedef struct { DWORD dwSize,dwFlags,dwXpos,dwYpos,dwZpos,dwRpos,dwUpos,dwVpos,dwButtons,dwButtonNumber,dwPOV,dwReserved1,dwReserved2; } JoyEx;
typedef UINT (WINAPI *JoyGetPosEx)(UINT,JoyEx*);
static JoyGetPosEx jget=0; static int jtried=0;
static char w_st[100], w_pf[90];
static void w_title(void){ char t[320]; snprintf(t,sizeof t,"Crazy Taxi 2 (recompilacao estatica) - v15zz  [%s]  %s  %s",ta_gpu_name(),w_pf,w_st); if(hwnd) SetWindowTextA(hwnd,t); }
void win_status(const char*s){ snprintf(w_st,sizeof w_st,"%s",s?s:""); w_title(); }
double win_now_ms(void){ static LARGE_INTEGER f; LARGE_INTEGER n; if(!f.QuadPart) QueryPerformanceFrequency(&f); QueryPerformanceCounter(&n); return 1000.0*(double)n.QuadPart/(double)f.QuadPart; }
void win_perf(double fps,double render_ms,double present_ms,int skipped){ snprintf(w_pf,sizeof w_pf,"[%.0f fps | desenho %.1f ms | tela %.1f ms | pulados %d/60]",fps,render_ms,present_ms,skipped); w_title(); }
int win_hotkeys(void){ static int prev[4]; int r=0; static const int vk[4]={VK_F1,VK_F2,VK_F3,VK_F4}; static const int vk2[4]={'1','2','3','4'}; for(int i=0;i<4;i++){ int d=win_active()&&(((GetAsyncKeyState(vk[i])&0x8000)!=0)||((GetAsyncKeyState(vk2[i])&0x8000)!=0)); if(d&&!prev[i]) r|=1<<i; prev[i]=d; } return r; }
extern int ta_dump_req; extern int ta_toggle_timer(void); extern const char *ta_toggle_dbg(int);
void win_pad(uint16_t *buttons,uint8_t *rt,uint8_t *lt,uint8_t *jx,uint8_t *jy){
    uint16_t b=0xFFFF; int x=128,y=128,r=0,l=0;
    /* teclado: setas = direcional e analogico; Z/Espaco=A, C=B, X/A=X (destination display), S=Y, Enter/P/Tab/Esc nao = Start, Q=gatilho esquerdo, W=gatilho direito */
    /* v15zz: teclas configuraveis (Options > Controller > Y = KEYBOARD SETUP). Padrao: W/S/A/D = direcional, Espaco = acelerar (R), Shift esq = freio (L), Z = A, C = B, X = X, V = Y, Enter = Start; setas, P e Tab continuam valendo enquanto nao houver mapa proprio */
    { extern int ct2_kmap[11], ct2_kmap_custom, ct2_kbd_cap, ct2_kbd_got, ct2_kbd_used; int cu=ct2_kmap_custom; int any=0;
      #define KM(i) key(ct2_kmap[i])
      if(KM(0)||(!cu&&key(VK_UP))){ b&=~(1u<<4); any=1; }   if(KM(1)||(!cu&&key(VK_DOWN))){ b&=~(1u<<5); any=1; }   /* v15q: teclado so no D-pad */
      if(KM(2)||(!cu&&key(VK_LEFT))){ b&=~(1u<<6); any=1; } if(KM(3)||(!cu&&key(VK_RIGHT))){ b&=~(1u<<7); any=1; }
      { extern int ct2_kb_act; ct2_kb_act=0; if(KM(4)) ct2_kb_act|=1; if(KM(5)) ct2_kb_act|=2; if(KM(6)) ct2_kb_act|=4; if(KM(7)) ct2_kb_act|=8; if(ct2_kb_act) any=1; }   /* v15zzh: o menux_filter converte conforme o tipo do jogo */
      if(KM(8)){ b&=~(1u<<10); any=1; }
      if(KM(9)){ b&=~(1u<<9); any=1; }
      if(KM(10)||(!cu&&(key('P')||key(VK_TAB)))){ b&=~(1u<<3); any=1; }
      #undef KM
      if(any) ct2_kbd_used=300;   /* quem joga no teclado usa o controle moderno (toque duplo em acelerar = turbo dash) */
      { static unsigned char pd[256]; static int kw=0;   /* teclas ja seguradas ao abrir a captura (ex.: a tecla de confirmar) nao contam */
        if(ct2_kbd_cap){ for(int vk=8;vk<255;vk++){ if(vk>=0x10&&vk<=0x12) continue; int d=win_active()&&(GetAsyncKeyState(vk)&0x8000)!=0; if(kw&&d&&!pd[vk]&&!ct2_kbd_got){ ct2_kbd_got=(vk==VK_ESCAPE)?-1:vk; } pd[vk]=(unsigned char)d; } kw=1; } else kw=0; }
      { /* mouse -> coordenadas 640x480 da area 4:3 dos menus */
        extern float ct2_mouse_x, ct2_mouse_y; extern int ct2_mouse_click, ct2_mouse_moved; static int lb=0; static float ox=-9,oy=-9; POINT pt; RECT rc; ct2_mouse_x=ct2_mouse_y=-1;
        if(hwnd&&GetCursorPos(&pt)&&ScreenToClient(hwnd,&pt)&&GetClientRect(hwnd,&rc)&&rc.right>0&&rc.bottom>0&&GetForegroundWindow()==hwnd){
            float cw=(float)rc.right,ch=(float)rc.bottom,bw=cw,bh=ch,bx=0,by=0; if(ta_aspect!=0){ if(cw/ch>4.f/3.f){ bw=ch*4.f/3.f; bx=(cw-bw)/2.f; } else { bh=cw*3.f/4.f; by=(ch-bh)/2.f; } }
            float u=((float)pt.x-bx)/bw*640.f, v=((float)pt.y-by)/bh*480.f; if(u>=0&&u<640&&v>=0&&v<480){ ct2_mouse_x=u; ct2_mouse_y=v; if(fabsf(u-ox)+fabsf(v-oy)>1.5f) ct2_mouse_moved=1; ox=u; oy=v; } }
        int d=win_active()&&(GetAsyncKeyState(VK_LBUTTON)&0x8000)!=0; if(d&&!lb) ct2_mouse_click=1; lb=d; }
    }
    if(!xtried){ xtried=1; HMODULE m=LoadLibraryA("xinput1_4.dll"); if(!m) m=LoadLibraryA("xinput9_1_0.dll"); if(!m) m=LoadLibraryA("xinput1_3.dll"); if(m){ xget=(XGetState)GetProcAddress(m,"XInputGetState"); xset=(XSetState)GetProcAddress(m,"XInputSetState"); } }
    int got=0;
    if(xget&&win_active()){ for(DWORD idx=0;idx<4&&!got;idx++){ XState s; memset(&s,0,sizeof s); if(xget(idx,&s)!=0) continue; got=1; vib_apply(idx);
        { /* v15zz: botoes do controle configuraveis (Controller Setting > painel); padrao identico ao anterior */
          extern int ct2_pmap[11], ct2_pmap_custom, ct2_pad_cap, ct2_pad_got; int cu=ct2_pmap_custom;
          #define PD(idx) ((idx)<16?(((s.btn>>(idx))&1)!=0):((idx)==16?(s.lt>100):(s.rt>100)))
          if(PD(ct2_pmap[0])) b&=~(1u<<4); if(PD(ct2_pmap[1])) b&=~(1u<<5); if(PD(ct2_pmap[2])) b&=~(1u<<6); if(PD(ct2_pmap[3])) b&=~(1u<<7);
          if(PD(ct2_pmap[6])) b&=~(1u<<2); if(PD(ct2_pmap[7])) b&=~(1u<<1); if(PD(ct2_pmap[8])) b&=~(1u<<10); if(PD(ct2_pmap[9])) b&=~(1u<<9);
          if(PD(ct2_pmap[10])) b&=~(1u<<3);   /* Start */
          if(!cu&&(s.btn&0x0020)) b&=~(1u<<3);   /* Back/Select tambem abre a pausa (so no padrao) */
          if(PD(ct2_pmap[5])) l=(ct2_pmap[5]==16&&s.lt>l)?s.lt:255; if(PD(ct2_pmap[4])) r=(ct2_pmap[4]==17&&s.rt>r)?s.rt:255;
          if(!cu){ if(s.btn&0x0100) l=255; if(s.btn&0x0200) r=255; if(s.lt>l) l=s.lt; if(s.rt>r) r=s.rt; }   /* LB/RB e gatilhos analogicos */
          static unsigned pprev=0; static int pw=0; if(ct2_pad_cap){ unsigned now=(unsigned)s.btn&0xFFFFu; if(s.lt>100) now|=1u<<16; if(s.rt>100) now|=1u<<17; if(!pw){ pprev=now; pw=1; } unsigned nw=now&~pprev; if(nw&&ct2_pad_got==-2){ for(int k=0;k<18;k++) if(nw&(1u<<k)){ if(k==10||k==11) continue; ct2_pad_got=k; break; } } pprev=now; } else pw=0;
          #undef PD
        }
        if(s.lx>8000||s.lx<-8000) x=(s.lx+32768)>>8;
        if(s.ly>8000||s.ly<-8000) y=255-((s.ly+32768)>>8);
    } }
    if(!got&&win_active()){
        if(!jtried){ jtried=1; HMODULE m=LoadLibraryA("winmm.dll"); if(m) jget=(JoyGetPosEx)GetProcAddress(m,"joyGetPosEx"); }
        if(jget){ for(UINT id=0;id<4;id++){ JoyEx j; memset(&j,0,sizeof j); j.dwSize=sizeof j; j.dwFlags=0xFF; if(jget(id,&j)!=0) continue;
            if(j.dwButtons&1) b&=~(1u<<2); if(j.dwButtons&2) b&=~(1u<<1); if(j.dwButtons&4) b&=~(1u<<10); if(j.dwButtons&8) b&=~(1u<<9);
            if(j.dwButtons&(1u<<7)||j.dwButtons&(1u<<9)||j.dwButtons&(1u<<6)) b&=~(1u<<3);
            if(j.dwButtons&(1u<<4)) l=255; if(j.dwButtons&(1u<<5)) r=255;
            if(j.dwPOV!=0xFFFF&&j.dwPOV!=0xFFFFFFFFu){ DWORD p=j.dwPOV; if(p>=31500||p<=4500) b&=~(1u<<4); if(p>=4500&&p<=13500) b&=~(1u<<7); if(p>=13500&&p<=22500) b&=~(1u<<5); if(p>=22500&&p<=31500) b&=~(1u<<6); }
            int ax=(int)(j.dwXpos>>8), ay=(int)(j.dwYpos>>8); if(ax>160||ax<96) x=ax; if(ay>160||ay<96) y=ay;
            break; } }
    }
    { static int fp[3]; const int fk[3]={VK_F5,VK_F6,VK_F7}; for(int q=0;q<3;q++){ int kk=key(fk[q]); if(kk&&!fp[q]) win_status(ta_toggle_dbg(q)); fp[q]=kk; } }   /* diagnostico grafico */
    { static int f4p=0; extern int ta_gpu_mode; int k4=key(VK_F4); if(k4&&!f4p){ ta_gpu_mode=!ta_gpu_mode; ta_save_settings(); win_status(ta_gpu_mode?"F4: renderizador Direct3D 11 (GPU)":"F4: renderizador por SOFTWARE (CPU)"); } f4p=k4; }   /* v15zw */
    { static int f9p=0; int k9=key(VK_F9); if(k9&&!f9p){ int on=ta_toggle_timer(); win_status(on?"F9: CRONOMETRO CONGELADO (cheat ligado)":"F9: cronometro normal (cheat desligado)"); } f9p=k9; }
    { static int f8p=0; int k8=key(VK_F8); if(k8&&!f8p){ extern void aica_voz_nota(const char*); aica_voz_nota("F8 - fala ouvida agora"); win_status("marca gravada em voz.txt"); } f8p=k8; }   /* F8: marca no voz.txt o instante em que a fala foi ouvida */
    { static int f11p=0; int k11=key(VK_F11); if(k11&&!f11p){ ta_res_i=(ta_res_i+10)%20; ta_disp_gen++; ta_save_settings(); } f11p=k11; }   /* F11: alterna janela / tela cheia na mesma resolucao */
    { static int f10p=0; extern int ct2_hoptrace_req; int k10=key(VK_F10); if(k10&&!f10p){ ct2_hoptrace_req=1; win_status("F10: pronto - aperte Y UMA vez (grava hop_trace.txt)"); } f10p=k10; }
    { static int f12p=0; int k12=key(VK_F12); if(k12&&!f12p){ ta_dump_req=1; win_status("F12: quadro salvo em glitch_dump_NUMERO.bin (nome novo a cada vez)"); } f12p=k12; }
    /* o analogico tambem move o cursor dos menus */
    { extern int ta_ingame_play(void); if(!ta_ingame_play()){   /* nos menus; em jogo o analogico segue analogico (sensibilidade) */
        if(y<64){ b&=~(1u<<4); y=128; } if(y>192){ b&=~(1u<<5); y=128; } if(x<64){ b&=~(1u<<6); x=128; } if(x>192){ b&=~(1u<<7); x=128; } } }   /* v15q: stick forte vira so D-pad (sem enviar a mesma direcao no analogico) */
    /* trava: cada botao digital fica ativo por no minimo 3 quadros (o jogo le o controle a cada 1-2 quadros; mais que isso fazia o START reabrir/fechar a pausa e atrasava os menus) (o jogo le o controle em intervalos e perdia toques curtos) */
    { static int hold[16]; static const int bits[]={1,2,3,4,5,6,7,9,10}; for(unsigned k=0;k<sizeof bits/sizeof bits[0];k++){ extern int ta_ingame_play(void); int bi=bits[k]; int cap=(bi==3||(!ta_ingame_play()&&(bi==1||bi==2||bi==9||bi==10)));   /* v15zt: em jogo A/B/X/Y seguem o dedo (o toque fixo de 3 quadros impedia o Crazy Hop carregado: segurar Y >= ~8 quadros da um salto bem mais alto); nos menus e o START continuam com 3 quadros */   /* v15t: A/B/X/Y/START viram um toque de EXATAMENTE 3 quadros (o log mostrou o jogo recebendo START 2x em quadros seguidos e pausa abrindo/fechando; e A segurado pegava a proxima tela); direcionais seguem o dedo (repeticao automatica) */
        if(!(b&(1u<<bi))){ if(hold[bi]<100) hold[bi]++; if(cap&&hold[bi]>3) b|=(1u<<bi); } else if(hold[bi]>0&&hold[bi]<3){ hold[bi]++; b&=~(1u<<bi); } else hold[bi]=0; } }
    { static int px=1,nx=0; int dx=!(b&(1u<<10)); if(dx&&!px&&nx<60){ nx++; printf("[pad] X (destination display) enviado ao jogo\n"); fflush(stdout); } px=dx; }
    { uint8_t rr=(uint8_t)r,ll=(uint8_t)l; menux_filter(&b,&rr,&ll); r=rr; l=ll; }
    { extern int ct2_quit_req; if(ct2_quit_req){ ct2_quit_req=0; win_request_quit(); } }   /* v15zz: EXIT do menu principal fecha o jogo (no Windows o menux_filter roda aqui, nao no boot.c) */
    *buttons=b; *rt=(uint8_t)r; *lt=(uint8_t)l; *jx=(uint8_t)x; *jy=(uint8_t)y;
}

/* ---- audio: waveOut carregado dinamicamente (winmm.dll); uma thread renderiza a AICA em blocos e os enfileira ---- */
#define ABUF 4
#define ASAMP 1024
typedef MMRESULT (WINAPI *PwOpen)(LPHWAVEOUT,UINT,LPCWAVEFORMATEX,DWORD_PTR,DWORD_PTR,DWORD);
typedef MMRESULT (WINAPI *PwHdr)(HWAVEOUT,LPWAVEHDR,UINT);
static void (*a_render)(int16_t*,int); static HWAVEOUT a_hwo; static WAVEHDR a_hdr[ABUF]; static int16_t a_buf[ABUF][ASAMP*2]; static PwHdr a_write;
static DWORD WINAPI audio_thread(LPVOID p){
    (void)p;
    for(;;){
        int any=0;
        for(int i=0;i<ABUF;i++){
            if(a_hdr[i].dwFlags&WHDR_INQUEUE) continue;
            a_render(a_buf[i],ASAMP); a_hdr[i].dwBufferLength=ASAMP*4; a_write(a_hwo,&a_hdr[i],sizeof(WAVEHDR)); any=1; }
        if(!any) Sleep(4);
    }
    return 0;
}
void win_audio_start(void (*render)(int16_t*,int)){
    HMODULE m=LoadLibraryA("winmm.dll"); if(!m) return;
    PwOpen op=(PwOpen)GetProcAddress(m,"waveOutOpen"); PwHdr prep=(PwHdr)GetProcAddress(m,"waveOutPrepareHeader"); a_write=(PwHdr)GetProcAddress(m,"waveOutWrite");
    if(!op||!prep||!a_write) return;
    WAVEFORMATEX f; memset(&f,0,sizeof f); f.wFormatTag=WAVE_FORMAT_PCM; f.nChannels=2; f.nSamplesPerSec=44100; f.wBitsPerSample=16; f.nBlockAlign=4; f.nAvgBytesPerSec=44100*4;
    if(op(&a_hwo,WAVE_MAPPER,&f,0,0,CALLBACK_NULL)!=MMSYSERR_NOERROR) return;
    a_render=render; memset(a_hdr,0,sizeof a_hdr);
    for(int i=0;i<ABUF;i++){ a_hdr[i].lpData=(LPSTR)a_buf[i]; a_hdr[i].dwBufferLength=ASAMP*4; prep(a_hwo,&a_hdr[i],sizeof(WAVEHDR)); }
    CreateThread(0,0,audio_thread,0,0,0);
}

/* v15zzn: abre o argumento idx da linha de comando (Unicode) como arquivo binario de leitura; 0 se falhar */
#include <shellapi.h>
FILE *ct2_open_arg_w(int idx){ int n=0; wchar_t **a=CommandLineToArgvW(GetCommandLineW(),&n); FILE *f=0; if(a&&n>idx) f=_wfopen(a[idx],L"rb"); if(a) LocalFree(a); return f; }
/* v15zzn: fallback: caminho UTF-8 do track03.bin gravado pelo launcher em launcher.ini (o .bat pode ter estragado acentos) */
FILE *ct2_open_ini_track(void){ FILE *i=fopen("launcher.ini","rb"); if(!i) return 0; char l[2200]; FILE *f=0; while(fgets(l,sizeof l,i)){ size_t n=strlen(l); while(n&&(l[n-1]=='\\r'||l[n-1]=='\\n')) l[--n]=0; if(!strncmp(l,"track=",6)){ wchar_t w[1100]; if(MultiByteToWideChar(CP_UTF8,0,l+6,-1,w,1100)) f=_wfopen(w,L"rb"); break; } } fclose(i); return f; }
