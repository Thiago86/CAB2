/* Crazy Taxi 2 - launcher (Windows, so GDI/Win32).  Compilar:
     launcher\build.bat   (gera CrazyTaxi2.exe na pasta raiz, com o icone embutido)
   Imagens em launcher\assets\*.raw (BGRA pre-multiplicado, geradas das artes do usuario) + icon.ico.
   Fluxo: [SELECT FILES] escolhe o .gdi ou track03.bin -> barra de progresso extrai o disco para disc\ -> libera [PLAY].
   PLAY: abre o jogar.bat com a pasta da trilha (ele gera o C se preciso, compila e roda o jogo) e o launcher fecha. */
#include <windows.h>
#include <commdlg.h>
#include "launcher_core.h"

#define DW 1372
#define DH 784
static const RECT R_PLAY={624,175,624+579,175+210};
static const RECT R_SEL ={714,399,714+536,399+115};
static const RECT R_PAN ={700,528,1262,640};

typedef struct { HBITMAP bmp; HDC dc; int w,h; } Img;
static Img g_bg, g_play, g_off, g_sel;          /* originais */
static Img s_bg, s_play, s_off, s_sel;          /* na escala da janela */
static double g_sc=1.0; static int g_cw=DW, g_ch=DH;
static HWND g_hw; static HFONT g_f1, g_f2;
static LCore g_core; static HANDLE g_th=0;
static int g_ready=0, g_busy=0, g_exit=1;
static wchar_t g_status[600]; static int g_err=0;
static char g_exedir[1100];                     /* pasta do .exe, UTF-8, com '/' no fim */
static char g_track[1024];
static int g_phase=0;                           /* 1 extraindo, 2 gerando/compilando o jogo */
static HANDLE g_bp=0; static DWORD g_bt1=0; static int g_bpct=0, g_bbase=-1; static wchar_t g_bmsg[120];

static wchar_t *u8w(const char *u){ static wchar_t b[4][1200]; static int k=0; k=(k+1)&3; MultiByteToWideChar(CP_UTF8,0,u,-1,b[k],1199); b[k][1199]=0; return b[k]; }
static void w8(const wchar_t *w,char *o,size_t n){ WideCharToMultiByte(CP_UTF8,0,w,-1,o,(int)n,0,0); o[n-1]=0; }

static int load_raw(const char *name,Img *im){
    char p[1200]; snprintf(p,sizeof p,"%slauncher/assets/%s",g_exedir,name); FILE *f=lc_fopen(p,"rb"); if(!f) return 0;
    char hd[12]; int w,h; if(fread(hd,1,12,f)!=12||memcmp(hd,"CT2R",4)){ fclose(f); return 0; } memcpy(&w,hd+4,4); memcpy(&h,hd+8,4);
    BITMAPINFO bi; memset(&bi,0,sizeof bi); bi.bmiHeader.biSize=sizeof(BITMAPINFOHEADER); bi.bmiHeader.biWidth=w; bi.bmiHeader.biHeight=-h; bi.bmiHeader.biPlanes=1; bi.bmiHeader.biBitCount=32; bi.bmiHeader.biCompression=BI_RGB;
    void *bits=0; HDC sd=GetDC(0); im->bmp=CreateDIBSection(sd,&bi,DIB_RGB_COLORS,&bits,0,0); im->dc=CreateCompatibleDC(sd); ReleaseDC(0,sd);
    if(!im->bmp||!bits){ fclose(f); return 0; } SelectObject(im->dc,im->bmp); size_t n=(size_t)w*h*4; size_t r=fread(bits,1,n,f); fclose(f); im->w=w; im->h=h; return r==n; }

static void scale_img(const Img *s,Img *d,int w,int h){
    if(w==s->w&&h==s->h){ *d=*s; return; }
    BITMAPINFO bi; memset(&bi,0,sizeof bi); bi.bmiHeader.biSize=sizeof(BITMAPINFOHEADER); bi.bmiHeader.biWidth=w; bi.bmiHeader.biHeight=-h; bi.bmiHeader.biPlanes=1; bi.bmiHeader.biBitCount=32; bi.bmiHeader.biCompression=BI_RGB;
    void *bits=0; HDC sd=GetDC(0); d->bmp=CreateDIBSection(sd,&bi,DIB_RGB_COLORS,&bits,0,0); d->dc=CreateCompatibleDC(sd); ReleaseDC(0,sd); SelectObject(d->dc,d->bmp); d->w=w; d->h=h;
    SetStretchBltMode(d->dc,HALFTONE); SetBrushOrgEx(d->dc,0,0,0); StretchBlt(d->dc,0,0,w,h,s->dc,0,0,s->w,s->h,SRCCOPY); }

static int sx(int v){ return (int)(v*g_sc+0.5); }

static void blend(HDC dst,const Img *im,int dx,int dy){
    BLENDFUNCTION bf; bf.BlendOp=AC_SRC_OVER; bf.BlendFlags=0; bf.SourceConstantAlpha=255; bf.AlphaFormat=AC_SRC_ALPHA;
    AlphaBlend(dst,sx(dx),sx(dy),im->w,im->h,im->dc,0,0,im->w,im->h,bf); }

static void text(HDC dc,const wchar_t *t,RECT r,HFONT f,COLORREF c,UINT fl){
    SelectObject(dc,f); SetBkMode(dc,TRANSPARENT); RECT s=r; OffsetRect(&s,2,2); SetTextColor(dc,RGB(0,0,0)); DrawTextW(dc,t,-1,&s,fl); SetTextColor(dc,c); DrawTextW(dc,t,-1,&r,fl); }

static void paint(HDC wdc){
    HDC m=CreateCompatibleDC(wdc); HBITMAP bm=CreateCompatibleBitmap(wdc,g_cw,g_ch); HGDIOBJ old=SelectObject(m,bm);
    BitBlt(m,0,0,g_cw,g_ch,s_bg.dc,0,0,SRCCOPY);
    blend(m,&s_sel,R_SEL.left,R_SEL.top);
    blend(m,g_ready&&!g_busy?&s_play:&s_off,R_PLAY.left,R_PLAY.top);
    /* painel de status / barra de progresso */
    int st=g_core.stage; const wchar_t *line1=g_status; wchar_t l2[400]; l2[0]=0; int show=(g_status[0]!=0)||g_busy;
    if(show){
        RECT pn={sx(R_PAN.left),sx(R_PAN.top),sx(R_PAN.right),sx(R_PAN.bottom)};
        { HDC d=CreateCompatibleDC(m); HBITMAP b=CreateCompatibleBitmap(m,1,1); SelectObject(d,b); SetPixel(d,0,0,RGB(0,0,0)); BLENDFUNCTION bf; bf.BlendOp=AC_SRC_OVER; bf.BlendFlags=0; bf.SourceConstantAlpha=150; bf.AlphaFormat=0; AlphaBlend(m,pn.left,pn.top,pn.right-pn.left,pn.bottom-pn.top,d,0,0,1,1,bf); DeleteDC(d); DeleteObject(b); }
        RECT t1={pn.left+sx(12),pn.top+sx(8),pn.right-sx(12),pn.top+sx(44)};
        if(g_busy){
            long long tot=g_core.total, dn=g_core.done; if(g_phase==2){ tot=100; dn=g_bpct; } int pct=tot>0?(int)(dn*100/tot):0; if(pct>100) pct=100;
            wchar_t h[200]; if(g_phase==2) wsprintfW(h,L"%s  %d%%",g_bmsg,pct); else if(st==1) lstrcpyW(h,L"VERIFYING DISC..."); else wsprintfW(h,L"EXTRACTING GAME...  %d%%",pct);
            text(m,h,t1,g_f1,RGB(255,255,255),DT_LEFT|DT_VCENTER|DT_SINGLELINE|DT_END_ELLIPSIS);
            RECT bar={pn.left+sx(12),pn.top+sx(48),pn.right-sx(12),pn.top+sx(78)};
            HBRUSH bk=CreateSolidBrush(RGB(20,20,20)); HPEN pe=CreatePen(PS_SOLID,2,RGB(255,200,30)); SelectObject(m,bk); SelectObject(m,pe); RoundRect(m,bar.left,bar.top,bar.right,bar.bottom,10,10); DeleteObject(bk); DeleteObject(pe);
            int iw=bar.right-bar.left-6, fw=tot>0?(int)((long long)iw*dn/tot):0;
            if(fw>0){ RECT a={bar.left+3,bar.top+3,bar.left+3+fw,(bar.top+bar.bottom)/2}, b={bar.left+3,(bar.top+bar.bottom)/2,bar.left+3+fw,bar.bottom-3}; HBRUSH b1=CreateSolidBrush(RGB(255,205,40)), b2=CreateSolidBrush(RGB(240,140,0)); FillRect(m,&a,b1); FillRect(m,&b,b2); DeleteObject(b1); DeleteObject(b2);
                int sq=(bar.bottom-bar.top-6)/2; if(sq<4) sq=4; HBRUSH bb=CreateSolidBrush(RGB(30,20,0)); for(int x=0,c=0;x<fw;x+=sq,c++){ if(c%2) continue; RECT q={bar.left+3+x,bar.top+3,bar.left+3+(x+sq<fw?x+sq:fw),bar.top+3+sq}; FillRect(m,&q,bb); } DeleteObject(bb); }
            if(g_phase==2) lstrcpyW(l2,L"Building the game from your disc. The first time takes a few minutes - please wait."); else wsprintfW(l2,L"%s   (%d / %d MB)",u8w(g_core.cur),(int)(dn>>20),(int)(tot>>20));
            RECT t2={pn.left+sx(12),pn.top+sx(82),pn.right-sx(12),pn.bottom-sx(4)}; text(m,l2,t2,g_f2,RGB(255,230,150),DT_LEFT|DT_VCENTER|DT_SINGLELINE|DT_END_ELLIPSIS);
        } else {
            RECT t={pn.left+sx(12),pn.top+sx(8),pn.right-sx(12),pn.bottom-sx(8)};
            text(m,line1,t,g_err?g_f2:g_f1,g_err?RGB(255,140,120):RGB(255,255,255),DT_LEFT|DT_VCENTER|DT_WORDBREAK);
        }
    }
    BitBlt(wdc,0,0,g_cw,g_ch,m,0,0,SRCCOPY); SelectObject(m,old); DeleteObject(bm); DeleteDC(m); }

static void set_status(const wchar_t *s,int err){ wcsncpy(g_status,s,599); g_status[599]=0; g_err=err; }

static void save_ini(void){
    char p[1200]; snprintf(p,sizeof p,"%slauncher.ini",g_exedir); FILE *f=lc_fopen(p,"wb"); if(!f) return; fprintf(f,"ok=1\ntrack=%s\n",g_track); fclose(f); }
static int load_ini(void){
    char p[1200]; snprintf(p,sizeof p,"%slauncher.ini",g_exedir); FILE *f=lc_fopen(p,"rb"); if(!f) return 0; char l[1200]; int ok=0; g_track[0]=0;
    while(fgets(l,sizeof l,f)){ size_t n=strlen(l); while(n&&(l[n-1]=='\r'||l[n-1]=='\n')) l[--n]=0; if(!strcmp(l,"ok=1")) ok=1; else if(!strncmp(l,"track=",6)) snprintf(g_track,sizeof g_track,"%s",l+6); }
    fclose(f); if(!ok||!g_track[0]) return 0;
    FILE *t=lc_fopen(g_track,"rb"); if(!t) return 0; fclose(t);
    char a[1200]; snprintf(a,sizeof a,"%sdisc/1ST_READ.BIN",g_exedir); t=lc_fopen(a,"rb"); if(!t) return 0; fclose(t);
    snprintf(a,sizeof a,"%sdisc/IP.BIN",g_exedir); t=lc_fopen(a,"rb"); if(!t) return 0; fclose(t); return 1; }


/* ---------- fase 2: gerar o C a partir do disco e compilar (jogar.bat em modo CT2_BUILD_ONLY, janela oculta) ---------- */
static int read_small(const char *rel,char *out,size_t n){ char p[1200]; snprintf(p,sizeof p,"%s%s",g_exedir,rel); FILE *f=lc_fopen(p,"rb"); if(!f) return 0; size_t r=fread(out,1,n-1,f); fclose(f); out[r]=0; while(r&&(out[r-1]=='\r'||out[r-1]=='\n'||out[r-1]==' ')) out[--r]=0; return 1; }
static int needs_build(void){ char a[200],b[200]; if(!read_small("ct2.exe",a,2)) return 1; if(!read_small("ct2.ver",a,sizeof a)||!read_small("VERSION.txt",b,sizeof b)) return 1; return strcmp(a,b)!=0; }
static void kill_build(void){ if(!g_bp) return; DWORD pid=GetProcessId(g_bp); wchar_t c[100]; wsprintfW(c,L"taskkill /PID %lu /T /F",pid); STARTUPINFOW si; PROCESS_INFORMATION pi; memset(&si,0,sizeof si); si.cb=sizeof si; memset(&pi,0,sizeof pi); if(CreateProcessW(0,c,0,0,FALSE,CREATE_NO_WINDOW,0,0,&si,&pi)){ WaitForSingleObject(pi.hProcess,5000); CloseHandle(pi.hThread); CloseHandle(pi.hProcess); } }
static void build_done(int ok){ if(g_bp){ CloseHandle(g_bp); g_bp=0; } g_busy=0; g_phase=0; KillTimer(g_hw,1);
    if(ok){ g_ready=1; set_status(L"Game ready! Click PLAY.",0); } else { g_ready=0; set_status(L"The build failed. Open build.log (in the game folder) to see why, then try SELECT FILES again.",1); } }
static const wchar_t *bat_rel(void){ char p[1200]; snprintf(p,sizeof p,"%slauncher/run.bat",g_exedir); FILE *f=lc_fopen(p,"rb"); if(f){ fclose(f); return L"launcher\\run.bat"; } return L"jogar.bat"; }
static void start_build(void){
    char d[1200]; lc_dirname(g_track,d,sizeof d); size_t n=strlen(d); while(n>3&&(d[n-1]=='/'||d[n-1]=='\\')) d[--n]=0;
    wchar_t ex[1200], dw[1200], lg[1300]; lstrcpyW(ex,u8w(g_exedir)); lstrcpyW(dw,u8w(d));
    for(wchar_t *c=ex;*c;c++) if(*c==L'/') *c=L'\\';
    for(wchar_t *c=dw;*c;c++) if(*c==L'/') *c=L'\\';
    wsprintfW(lg,L"%sbuild.log",ex);
    SECURITY_ATTRIBUTES sa; sa.nLength=sizeof sa; sa.lpSecurityDescriptor=0; sa.bInheritHandle=TRUE;
    HANDLE h=CreateFileW(lg,GENERIC_WRITE,FILE_SHARE_READ|FILE_SHARE_WRITE,&sa,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,0);
    HANDLE hin=CreateFileW(L"NUL",GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_WRITE,&sa,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,0);
    SetEnvironmentVariableW(L"CT2_SKIP_EXTRACT",L"1"); SetEnvironmentVariableW(L"CT2_BUILD_ONLY",L"1");
    static wchar_t cmd[3000]; wsprintfW(cmd,L"cmd.exe /c \"\"%s%s\" \"%s\"\"",ex,bat_rel(),dw);
    STARTUPINFOW si; PROCESS_INFORMATION pi; memset(&si,0,sizeof si); si.cb=sizeof si; si.dwFlags=STARTF_USESTDHANDLES|STARTF_USESHOWWINDOW; si.wShowWindow=SW_HIDE; si.hStdOutput=h; si.hStdError=h; si.hStdInput=hin; memset(&pi,0,sizeof pi);
    BOOL ok=h!=INVALID_HANDLE_VALUE&&CreateProcessW(0,cmd,0,0,TRUE,CREATE_NO_WINDOW,0,ex,&si,&pi);
    SetEnvironmentVariableW(L"CT2_BUILD_ONLY",NULL);
    if(h!=INVALID_HANDLE_VALUE) CloseHandle(h); if(hin!=INVALID_HANDLE_VALUE) CloseHandle(hin);
    if(!ok){ g_busy=0; g_phase=0; g_ready=0; KillTimer(g_hw,1); set_status(L"Could not start the build (launcher\\run.bat is missing next to CrazyTaxi2.exe).",1); return; }
    CloseHandle(pi.hThread); g_bp=pi.hProcess; g_phase=2; g_busy=1; g_bpct=0; g_bbase=-1; g_bt1=GetTickCount(); lstrcpyW(g_bmsg,L"STARTING..."); set_status(L"",0); SetTimer(g_hw,1,500,0); }
static void build_poll(void){
    static char buf[1<<17]; char p[1200]; snprintf(p,sizeof p,"%sbuild.log",g_exedir); FILE *f=lc_fopen(p,"rb"); size_t r=0; if(f){ r=fread(buf,1,sizeof buf-1,f); fclose(f); } buf[r]=0;
    int base=2; const wchar_t *m=L"STARTING...";
    if(strstr(buf,"[1/3] Gerando")){ base=5; m=L"ANALYZING GAME CODE..."; }
    if(strstr(buf,"[2/5]")){ base=8; m=L"MAPPING FUNCTIONS..."; }
    if(strstr(buf,"[3/5]")){ base=18; m=L"TRANSLATING CODE..."; }
    if(strstr(buf,"[4/5]")){ base=28; m=L"PREPARING TO COMPILE..."; }
    if(strstr(buf,"[2/3] Compilando")){ int c=0; for(const char *q=buf;(q=strstr(q,"\ncompilando "));q++) c++; base=30+c*65/14; if(base>95) base=95; m=L"COMPILING GAME..."; }
    if(strstr(buf,"ligando ct2.exe")){ base=96; m=L"LINKING..."; }
    lstrcpyW(g_bmsg,m);
    DWORD now=GetTickCount(); if(base!=g_bbase){ g_bbase=base; g_bt1=now; }
    int creep=(int)((now-g_bt1)/5000); if(creep>7) creep=7; if(base>=96) creep=0; int pct=base+creep; if(pct>g_bpct) g_bpct=pct; if(g_bpct>99) g_bpct=99;
    DWORD ec=STILL_ACTIVE; if(g_bp&&GetExitCodeProcess(g_bp,&ec)&&ec!=STILL_ACTIVE){ int ok=(ec==0)&&!needs_build(); build_done(ok); } }

static DWORD WINAPI worker(LPVOID p){ lc_extract(&g_core); return 0; }

static void pick_files(void){
    wchar_t fn[1100]; fn[0]=0; OPENFILENAMEW o; memset(&o,0,sizeof o); o.lStructSize=sizeof o; o.hwndOwner=g_hw;
    o.lpstrFilter=L"Crazy Taxi 2 disc (*.gdi;track03.bin)\0*.gdi;track03.bin\0All files\0*.*\0\0"; o.lpstrFile=fn; o.nMaxFile=1100; o.lpstrTitle=L"Select the .gdi (or track03.bin) of Crazy Taxi 2";
    o.Flags=OFN_FILEMUSTEXIST|OFN_PATHMUSTEXIST|OFN_EXPLORER|OFN_HIDEREADONLY;
    if(!GetOpenFileNameW(&o)) return;
    char sel[1100], err[512]; w8(fn,sel,sizeof sel); memset(&g_core,0,sizeof g_core);
    if(lc_resolve(sel,g_core.track,sizeof g_core.track,err,sizeof err)){ set_status(u8w(err),1); g_ready=0; InvalidateRect(g_hw,0,FALSE); return; }
    { size_t n=strlen(g_core.track); const char *b=g_core.track+n; while(b>g_core.track&&b[-1]!='/'&&b[-1]!='\\') b--; if(!lc_ends_ci(b,"track03.bin")){ set_status(L"The data track file must be named track03.bin (as listed in the .gdi).",1); g_ready=0; InvalidateRect(g_hw,0,FALSE); return; } }
    snprintf(g_track,sizeof g_track,"%s",g_core.track);
    snprintf(g_core.outdir,sizeof g_core.outdir,"%sdisc",g_exedir);
    { char o2[1200]; snprintf(o2,sizeof o2,"%sout",g_exedir); lc_mkdirs(o2); snprintf(g_core.listfile,sizeof g_core.listfile,"%sout/disc_filelist.txt",g_exedir); }
    g_ready=0; g_busy=1; g_phase=1; set_status(L"",0); g_core.stage=1;
    g_th=CreateThread(0,0,worker,0,0,0); SetTimer(g_hw,1,40,0); InvalidateRect(g_hw,0,FALSE); }

static void do_play(void){
    char d[1200]; lc_dirname(g_track,d,sizeof d); size_t n=strlen(d); while(n>3&&(d[n-1]=='/'||d[n-1]=='\\')) d[--n]=0;
    wchar_t ex[1200], dw[1200]; lstrcpyW(ex,u8w(g_exedir)); lstrcpyW(dw,u8w(d));
    for(wchar_t *c=ex;*c;c++) if(*c==L'/') *c=L'\\';
    for(wchar_t *c=dw;*c;c++) if(*c==L'/') *c=L'\\';
    SetEnvironmentVariableW(L"CT2_SKIP_EXTRACT",L"1"); SetEnvironmentVariableW(L"CT2_BUILD_ONLY",NULL);
    static wchar_t cmd[3000]; wsprintfW(cmd,L"cmd.exe /c \"\"%s%s\" \"%s\"\"",ex,bat_rel(),dw);
    STARTUPINFOW si; PROCESS_INFORMATION pi; memset(&si,0,sizeof si); si.cb=sizeof si; memset(&pi,0,sizeof pi);
    if(!CreateProcessW(0,cmd,0,0,FALSE,CREATE_NEW_CONSOLE,0,ex,&si,&pi)){ set_status(L"Could not start the game (launcher\\run.bat is missing).",1); InvalidateRect(g_hw,0,FALSE); return; }
    CloseHandle(pi.hThread); CloseHandle(pi.hProcess);
    g_exit=0; DestroyWindow(g_hw); }

static int over(int mx,int my,const RECT *r){ double x=mx/g_sc, y=my/g_sc; return x>=r->left&&x<r->right&&y>=r->top&&y<r->bottom; }

static LRESULT CALLBACK wp(HWND h,UINT m,WPARAM w,LPARAM l){
    switch(m){
    case WM_PAINT: { PAINTSTRUCT ps; HDC dc=BeginPaint(h,&ps); paint(dc); EndPaint(h,&ps); return 0; }
    case WM_ERASEBKGND: return 1;
    case WM_TIMER:
        if(g_busy&&g_phase==1&&(g_core.stage==3||g_core.stage<0)){
            WaitForSingleObject(g_th,2000); CloseHandle(g_th); g_th=0;
            if(g_core.stage==3){ save_ini(); start_build(); }
            else { g_busy=0; g_phase=0; KillTimer(h,1); g_ready=0; set_status(u8w(g_core.msg),1); }
        } else if(g_busy&&g_phase==2) build_poll();
        InvalidateRect(h,0,FALSE); return 0;
    case WM_LBUTTONDOWN: { int x=(short)LOWORD(l), y=(short)HIWORD(l);
        if(g_busy) return 0;
        if(over(x,y,&R_SEL)) pick_files(); else if(g_ready&&over(x,y,&R_PLAY)) do_play();
        return 0; }
    case WM_SETCURSOR: { POINT p; GetCursorPos(&p); ScreenToClient(h,&p); if(LOWORD(l)==HTCLIENT&&!g_busy&&(over(p.x,p.y,&R_SEL)||(g_ready&&over(p.x,p.y,&R_PLAY)))){ SetCursor(LoadCursor(0,IDC_HAND)); return TRUE; } break; }
    case WM_KEYDOWN: if(w==VK_RETURN&&g_ready&&!g_busy) do_play(); else if(w==VK_ESCAPE) PostMessage(h,WM_CLOSE,0,0); return 0;
    case WM_CLOSE: if(g_busy){ g_core.cancel=1; if(g_th) WaitForSingleObject(g_th,5000); kill_build(); } DestroyWindow(h); return 0;
    case WM_DESTROY: PostQuitMessage(0); return 0;
    }
    return DefWindowProcW(h,m,w,l); }

int main(void){
    { typedef BOOL (WINAPI *F)(void); HMODULE u=GetModuleHandleW(L"user32.dll"); F f=u?(F)GetProcAddress(u,"SetProcessDPIAware"):0; if(f) f(); }
    { wchar_t p[1100]; GetModuleFileNameW(0,p,1100); wchar_t *s=wcsrchr(p,L'\\'); if(s) s[1]=0; SetCurrentDirectoryW(p); char u[1100]; w8(p,u,sizeof u); for(char *c=u;*c;c++) if(*c=='\\') *c='/'; snprintf(g_exedir,sizeof g_exedir,"%s",u); }
    if(!load_raw("bg.raw",&g_bg)||!load_raw("play.raw",&g_play)||!load_raw("play_off.raw",&g_off)||!load_raw("select.raw",&g_sel)){ MessageBoxW(0,L"Could not find the images in the launcher\\assets\\ folder (bg.raw, play.raw, play_off.raw, select.raw).",L"Crazy Taxi 2",MB_ICONERROR); return 1; }
    RECT wa; SystemParametersInfoW(SPI_GETWORKAREA,0,&wa,0); double aw=wa.right-wa.left, ah=wa.bottom-wa.top; g_sc=1.0;
    if(aw*0.94/DW<g_sc) g_sc=aw*0.94/DW; if(ah*0.88/DH<g_sc) g_sc=ah*0.88/DH;
    g_cw=sx(DW); g_ch=sx(DH);
    scale_img(&g_bg,&s_bg,g_cw,g_ch); scale_img(&g_play,&s_play,sx(g_play.w),sx(g_play.h)); scale_img(&g_off,&s_off,sx(g_off.w),sx(g_off.h)); scale_img(&g_sel,&s_sel,sx(g_sel.w),sx(g_sel.h));
    g_f1=CreateFontW(-sx(30),0,0,0,FW_HEAVY,TRUE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,ANTIALIASED_QUALITY,DEFAULT_PITCH,L"Arial Black");
    g_f2=CreateFontW(-sx(20),0,0,0,FW_BOLD,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,ANTIALIASED_QUALITY,DEFAULT_PITCH,L"Arial");
    WNDCLASSW wc; memset(&wc,0,sizeof wc); wc.lpfnWndProc=wp; wc.hInstance=GetModuleHandleW(0); wc.hCursor=LoadCursor(0,IDC_ARROW); wc.lpszClassName=L"CT2Launcher"; wc.hIcon=LoadIconW(wc.hInstance,MAKEINTRESOURCEW(1)); if(!wc.hIcon) wc.hIcon=LoadIcon(0,IDI_APPLICATION); RegisterClassW(&wc);
    DWORD st=WS_OVERLAPPED|WS_CAPTION|WS_SYSMENU|WS_MINIMIZEBOX; RECT r={0,0,g_cw,g_ch}; AdjustWindowRect(&r,st,FALSE);
    int ww=r.right-r.left, wh=r.bottom-r.top, x=wa.left+((int)aw-ww)/2, y=wa.top+((int)ah-wh)/2; if(y<wa.top) y=wa.top;
    g_hw=CreateWindowW(L"CT2Launcher",L"Crazy Taxi 2",st,x,y,ww,wh,0,0,wc.hInstance,0);
    { HICON ic=LoadIconW(wc.hInstance,MAKEINTRESOURCEW(1)); if(ic){ SendMessageW(g_hw,WM_SETICON,0,(LPARAM)ic); SendMessageW(g_hw,WM_SETICON,1,(LPARAM)ic); } }
    int auto_build=0; if(load_ini()) { if(needs_build()) auto_build=1; else { g_ready=1; set_status(L"Game ready. Click PLAY (or SELECT FILES to choose another disc).",0); } }
    else set_status(L"Click SELECT FILES and choose the .gdi (or track03.bin) of your Crazy Taxi 2 disc. PLAY unlocks after the extraction.",0);
    ShowWindow(g_hw,SW_SHOW); UpdateWindow(g_hw); if(auto_build) start_build();
    MSG msg; while(GetMessageW(&msg,0,0,0)>0){ TranslateMessage(&msg); DispatchMessageW(&msg); }
    return g_exit; }
