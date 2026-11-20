/* Launcher - nucleo sem interface (portavel; testado no Linux contra o disco real).
   Faz: achar a trilha de dados (track03.bin) a partir de um .gdi ou do proprio .bin, validar o disco
   (volume + SHA-1 do 1ST_READ.BIN) e extrair os arquivos para a pasta de saida, informando o progresso.
   Mesma logica do tools/gdextract.py (setor bruto de 2352 bytes, dados a partir do byte 16; trilha de dados em LBA 45000). */
#ifndef LAUNCHER_CORE_H
#define LAUNCHER_CORE_H
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <errno.h>
#ifdef _WIN32
#include <windows.h>

#else
#include <sys/stat.h>
#include <sys/types.h>
#endif

#define LC_BASE_LBA 45000
#define LC_SHA1_1STREAD "bcff06e9defcd454d99ada84b9d26757e8ab4221"   /* Crazy Taxi 2 US v1.004 (a unica versao que a recompilacao conhece) */

typedef struct {
    volatile long long total, done;     /* bytes a extrair / extraidos */
    volatile int stage;                 /* 0 parado, 1 validando, 2 extraindo, 3 pronto, -1 erro */
    char msg[512];                      /* mensagem de erro (stage==-1) */
    char cur[260];                      /* arquivo sendo extraido */
    char track[1024];                   /* trilha de dados resolvida (UTF-8) */
    char outdir[1024];                  /* pasta de saida (UTF-8) */
    char listfile[1024];                /* opcional: lista dos arquivos extraidos (formato do gdextract.py) */
    volatile int cancel;
} LCore;

/* ---------- utilitarios de arquivo (UTF-8 -> API ampla no Windows) ---------- */
#ifdef _WIN32
static wchar_t *lc_w(const char *u){ int n=MultiByteToWideChar(CP_UTF8,0,u,-1,0,0); wchar_t *w=(wchar_t*)malloc((size_t)(n+1)*sizeof(wchar_t)); MultiByteToWideChar(CP_UTF8,0,u,-1,w,n+1); return w; }
static FILE *lc_fopen(const char *p,const char *m){ wchar_t *w=lc_w(p), *mm=lc_w(m); FILE *f=_wfopen(w,mm); free(w); free(mm); return f; }
static int lc_seek(FILE *f,long long o){ return _fseeki64(f,o,SEEK_SET); }
static int lc_mkdir1(const char *p){ wchar_t *w=lc_w(p); int r=CreateDirectoryW(w,NULL)?0:(GetLastError()==ERROR_ALREADY_EXISTS?0:-1); free(w); return r; }
#else
static FILE *lc_fopen(const char *p,const char *m){ return fopen(p,m); }
static int lc_seek(FILE *f,long long o){ return fseeko(f,(off_t)o,SEEK_SET); }
static int lc_mkdir1(const char *p){ return (mkdir(p,0777)==0||errno==EEXIST)?0:-1; }
#endif
static void lc_mkdirs(const char *path){ char b[1100]; size_t n=strlen(path); if(n>=sizeof b) return; memcpy(b,path,n+1); for(size_t i=1;i<n;i++){ if(b[i]=='/'||b[i]=='\\'){ char c=b[i]; b[i]=0; if(b[i-1]!=':') lc_mkdir1(b); b[i]=c; } } lc_mkdir1(b); }

/* ---------- SHA-1 ---------- */
typedef struct { uint32_t h[5]; uint64_t n; uint8_t b[64]; int l; } LcSha;
static uint32_t lc_rol(uint32_t x,int s){ return (x<<s)|(x>>(32-s)); }
static void lc_sha_blk(LcSha *s,const uint8_t *p){ uint32_t w[80],a,b,c,d,e,t; int i; for(i=0;i<16;i++) w[i]=((uint32_t)p[4*i]<<24)|((uint32_t)p[4*i+1]<<16)|((uint32_t)p[4*i+2]<<8)|p[4*i+3]; for(;i<80;i++) w[i]=lc_rol(w[i-3]^w[i-8]^w[i-14]^w[i-16],1);
    a=s->h[0];b=s->h[1];c=s->h[2];d=s->h[3];e=s->h[4];
    for(i=0;i<80;i++){ uint32_t f,k; if(i<20){f=(b&c)|(~b&d);k=0x5A827999u;} else if(i<40){f=b^c^d;k=0x6ED9EBA1u;} else if(i<60){f=(b&c)|(b&d)|(c&d);k=0x8F1BBCDCu;} else {f=b^c^d;k=0xCA62C1D6u;} t=lc_rol(a,5)+f+e+k+w[i]; e=d;d=c;c=lc_rol(b,30);b=a;a=t; }
    s->h[0]+=a;s->h[1]+=b;s->h[2]+=c;s->h[3]+=d;s->h[4]+=e; }
static void lc_sha_init(LcSha *s){ s->h[0]=0x67452301u;s->h[1]=0xEFCDAB89u;s->h[2]=0x98BADCFEu;s->h[3]=0x10325476u;s->h[4]=0xC3D2E1F0u; s->n=0; s->l=0; }
static void lc_sha_add(LcSha *s,const uint8_t *p,size_t n){ s->n+=n; while(n){ size_t k=64-(size_t)s->l; if(k>n) k=n; memcpy(s->b+s->l,p,k); s->l+=(int)k; p+=k; n-=k; if(s->l==64){ lc_sha_blk(s,s->b); s->l=0; } } }
static void lc_sha_hex(LcSha *s,char *out){ uint64_t bits=s->n*8; uint8_t pad=0x80; lc_sha_add(s,&pad,1); pad=0; while(s->l!=56) lc_sha_add(s,&pad,1); uint8_t L[8]; for(int i=0;i<8;i++) L[i]=(uint8_t)(bits>>(56-8*i)); lc_sha_add(s,L,8); for(int i=0;i<5;i++) sprintf(out+8*i,"%08x",s->h[i]); out[40]=0; }

/* ---------- leitura de setores ---------- */
typedef struct { FILE *f; int ssize; int off; } LcTrk;   /* ssize 2352 (off 16) ou 2048 (off 0) */
static int lc_sec(LcTrk *t,uint32_t lba,int n,uint8_t *out){
    for(int i=0;i<n;i++){ long long o=(long long)((long long)lba-LC_BASE_LBA+i)*t->ssize+t->off; if(o<0||lc_seek(t->f,o)||fread(out+(size_t)i*2048,1,2048,t->f)!=2048) return -1; }
    return 0; }

/* ---------- .gdi -> trilha de dados ---------- */
static void lc_dirname(const char *p,char *out,size_t n){ size_t l=strlen(p); while(l>0&&p[l-1]!='/'&&p[l-1]!='\\') l--; if(l>=n) l=n-1; memcpy(out,p,l); out[l]=0; }
static int lc_ends_ci(const char *s,const char *e){ size_t a=strlen(s),b=strlen(e); if(b>a) return 0; for(size_t i=0;i<b;i++){ char x=s[a-b+i],y=e[i]; if(x>='A'&&x<='Z') x+=32; if(y>='A'&&y<='Z') y+=32; if(x!=y) return 0; } return 1; }

/* sel = .gdi ou o .bin da trilha de dados. Devolve 0 e preenche track; senao escreve o motivo em err. */
static int lc_resolve(const char *sel,char *track,size_t tn,char *err,size_t en){
    if(lc_ends_ci(sel,".gdi")){
        FILE *f=lc_fopen(sel,"rb"); if(!f){ snprintf(err,en,"Could not open the .gdi file."); return -1; }
        char line[1200], dir[1100]; lc_dirname(sel,dir,sizeof dir); int found=0;
        while(fgets(line,sizeof line,f)){
            int num=0,lba=0,type=0,ss=0; char name[1000]; name[0]=0; char *p=line;
            if(sscanf(p,"%d %d %d %d",&num,&lba,&type,&ss)<4) continue;
            for(int k=0;k<4;k++){ while(*p==' '||*p=='\t') p++; while(*p&&*p!=' '&&*p!='\t') p++; }
            while(*p==' '||*p=='\t') p++;
            if(*p=='"'){ p++; size_t i=0; while(*p&&*p!='"'&&i<sizeof name-1) name[i++]=*p++; name[i]=0; } else { size_t i=0; while(*p&&*p!=' '&&*p!='\t'&&*p!='\r'&&*p!='\n'&&i<sizeof name-1) name[i++]=*p++; name[i]=0; }
            if(lba==LC_BASE_LBA&&type==4&&name[0]){ snprintf(track,tn,"%s%s",dir,name); found=1; break; }
        }
        fclose(f);
        if(!found){ snprintf(err,en,"This .gdi has no data track (LBA 45000)."); return -1; }
    } else { snprintf(track,tn,"%s",sel); }
    return 0; }

/* abre a trilha e acha o PVD; define o tamanho de setor */
static int lc_open(const char *track,LcTrk *t,char *err,size_t en){
    t->f=lc_fopen(track,"rb"); if(!t->f){ snprintf(err,en,"Could not open:\n%s",track); return -1; }
    static const int ss[2]={2352,2048}, of[2]={16,0};
    for(int k=0;k<2;k++){ t->ssize=ss[k]; t->off=of[k]; uint8_t pvd[2048]; if(lc_sec(t,LC_BASE_LBA+16,1,pvd)==0&&memcmp(pvd+1,"CD001",5)==0) return 0; }
    fclose(t->f); t->f=0; snprintf(err,en,"This file does not look like the data track of a GD-ROM (track03.bin).\nSelect the .gdi or the track03.bin of Crazy Taxi 2."); return -1; }

/* ---------- lista de arquivos ---------- */
typedef struct { uint32_t lba, size; char path[260]; } LcEnt;
typedef struct { LcEnt *e; int n, cap; } LcList;
static int lc_walk(LcTrk *t,LcList *L,uint32_t lba,uint32_t size,const char *path,int depth){
    if(depth>16||size>64u*1024*1024) return -1;
    uint8_t *d=(uint8_t*)malloc(((size+2047)/2048)*2048); if(!d) return -1;
    if(lc_sec(t,lba,(int)((size+2047)/2048),d)){ free(d); return -1; }
    uint32_t i=0; int rc=0;
    while(i<size){
        int l=d[i]; if(l==0){ i=(i/2048+1)*2048; continue; }
        if(i+(uint32_t)l>size||l<34) break;
        const uint8_t *r=d+i; i+=(uint32_t)l;
        uint32_t ext=r[2]|(r[3]<<8)|(r[4]<<16)|((uint32_t)r[5]<<24), sz=r[10]|(r[11]<<8)|(r[12]<<16)|((uint32_t)r[13]<<24); int fl=r[25], nl=r[32];
        if(nl<=0||33+nl>l) continue;
        if(nl==1&&(r[33]==0||r[33]==1)) continue;
        char nm[260]; int k=0; for(int q=0;q<nl&&k<200;q++){ if(r[33+q]==';') break; nm[k++]=(char)r[33+q]; } nm[k]=0;
        if(!nm[0]||strchr(nm,'/')||strchr(nm,'\\')||!strcmp(nm,"..")||!strcmp(nm,".")) continue;
        char p[260]; if(path[0]) snprintf(p,sizeof p,"%s/%s",path,nm); else snprintf(p,sizeof p,"%s",nm);
        if(fl&2){ if(lc_walk(t,L,ext,sz,p,depth+1)){ rc=-1; break; } }
        else { if(L->n==L->cap){ L->cap=L->cap?L->cap*2:256; L->e=(LcEnt*)realloc(L->e,(size_t)L->cap*sizeof(LcEnt)); } LcEnt *e=&L->e[L->n++]; e->lba=ext; e->size=sz; snprintf(e->path,sizeof e->path,"%s",p); }
    }
    free(d); return rc; }

static void lc_fail(LCore *s,const char *m){ snprintf(s->msg,sizeof s->msg,"%s",m); s->stage=-1; }

/* Valida e extrai. Chamar numa thread; acompanhar s->stage / done / total. */
static int lc_extract(LCore *s){
    char err[512]; LcTrk t; s->stage=1; s->done=0; s->total=0; s->cur[0]=0; s->msg[0]=0;
    if(lc_open(s->track,&t,err,sizeof err)){ lc_fail(s,err); return -1; }
    uint8_t pvd[2048]; lc_sec(&t,LC_BASE_LBA+16,1,pvd);
    char vol[40]; memcpy(vol,pvd+40,32); vol[32]=0; for(int i=31;i>=0&&(vol[i]==' '||vol[i]==0);i--) vol[i]=0;
    if(strcmp(vol,"CRAZY_TAXI_2")){ fclose(t.f); snprintf(err,sizeof err,"This disc is not Crazy Taxi 2 (volume \"%s\").",vol); lc_fail(s,err); return -1; }
    const uint8_t *root=pvd+156; uint32_t rl=root[2]|(root[3]<<8)|(root[4]<<16)|((uint32_t)root[5]<<24), rs=root[10]|(root[11]<<8)|(root[12]<<16)|((uint32_t)root[13]<<24);
    LcList L={0,0,0}; if(lc_walk(&t,&L,rl,rs,"",0)){ fclose(t.f); free(L.e); lc_fail(s,"Could not read the files on this disc. Select the .gdi or the track03.bin (the data track) of Crazy Taxi 2."); return -1; }
    int idx=-1; for(int i=0;i<L.n;i++) if(!strcmp(L.e[i].path,"1ST_READ.BIN")) idx=i;
    if(idx<0){ fclose(t.f); free(L.e); lc_fail(s,"1ST_READ.BIN not found on the disc."); return -1; }
    /* confere a versao: SHA-1 do 1ST_READ.BIN */
    { LcSha h; lc_sha_init(&h); uint8_t buf[2048*32]; uint32_t left=L.e[idx].size, lba=L.e[idx].lba; while(left){ int n=(int)((left+2047)/2048); if(n>32) n=32; if(lc_sec(&t,lba,n,buf)){ fclose(t.f); free(L.e); lc_fail(s,"Disc read error."); return -1; } size_t b=(size_t)n*2048; if(b>left) b=left; lc_sha_add(&h,buf,b); left-=(uint32_t)b; lba+=(uint32_t)n; }
      char hex[41]; lc_sha_hex(&h,hex); if(strcmp(hex,LC_SHA1_1STREAD)){ fclose(t.f); free(L.e); snprintf(err,sizeof err,"Disc version differs from the one the recompilation knows (Crazy Taxi 2 US v1.004).\n1ST_READ.BIN: %s",hex); lc_fail(s,err); return -1; } }
    long long total=16*2048; for(int i=0;i<L.n;i++) total+=L.e[i].size;
    s->total=total; s->stage=2; lc_mkdirs(s->outdir);
    FILE *lf=0; if(s->listfile[0]) lf=lc_fopen(s->listfile,"wb");
    if(lf) fprintf(lf,"VOLUME %s | created %.16s\n",vol,(const char*)pvd+813);
    static uint8_t buf[2048*64]; char op[1400];
    { uint8_t ip[16*2048]; snprintf(s->cur,sizeof s->cur,"IP.BIN"); if(lc_sec(&t,LC_BASE_LBA,16,ip)){ fclose(t.f); free(L.e); lc_fail(s,"Disc read error."); return -1; } snprintf(op,sizeof op,"%s/IP.BIN",s->outdir); FILE *o=lc_fopen(op,"wb"); if(!o||fwrite(ip,1,sizeof ip,o)!=sizeof ip){ if(o) fclose(o); fclose(t.f); free(L.e); lc_fail(s,"Could not write to the destination folder (no permission or disk full?)."); return -1; } fclose(o); s->done+=sizeof ip; }
    for(int i=0;i<L.n;i++){
        LcEnt *e=&L.e[i]; if(s->cancel){ fclose(t.f); free(L.e); if(lf) fclose(lf); lc_fail(s,"Cancelled."); return -1; }
        snprintf(s->cur,sizeof s->cur,"%s",e->path);
        snprintf(op,sizeof op,"%s/%s",s->outdir,e->path); { char dd[1400]; lc_dirname(op,dd,sizeof dd); size_t dl=strlen(dd); if(dl&&(dd[dl-1]=='/'||dd[dl-1]=='\\')) dd[dl-1]=0; lc_mkdirs(dd); }
        FILE *o=lc_fopen(op,"wb"); if(!o){ fclose(t.f); free(L.e); if(lf) fclose(lf); lc_fail(s,"Could not write to the destination folder (no permission or disk full?)."); return -1; }
        uint32_t left=e->size, lba=e->lba;
        while(left){ int n=(int)((left+2047)/2048); if(n>64) n=64; if(lc_sec(&t,lba,n,buf)){ fclose(o); fclose(t.f); free(L.e); if(lf) fclose(lf); lc_fail(s,"Disc read error."); return -1; } size_t b=(size_t)n*2048; if(b>left) b=left; if(fwrite(buf,1,b,o)!=b){ fclose(o); fclose(t.f); free(L.e); if(lf) fclose(lf); lc_fail(s,"Write error (disk full?)."); return -1; } left-=(uint32_t)b; lba+=(uint32_t)n; s->done+=(long long)b; }
        fclose(o); if(lf) fprintf(lf,"%7u %10u %s\n",e->lba,e->size,e->path);
    }
    if(lf) fclose(lf); fclose(t.f); free(L.e); s->done=s->total; s->stage=3; return 0; }
#endif
