/* gpu11.inc (v15zw) - renderizador Direct3D 11 do TA/PVR. Incluido por ta.c.
   Mesmo papel do renderizador por software (faixas + shade()), mas na GPU: o quadro sai em fb[] (RW x RH, 0xAARRGGBB) e o resto
   (antialiasing, escala, lastscene, writeback, menus) continua igual.
   Alvo: GPUs modestas (ex.: Intel HD 4000, 3a geracao): feature level 10_0+, shaders SM 4.0 (vs_4_0/ps_4_0), sem compute,
   D24_UNORM_S8_UINT, texturas B8G8R8A8, compilador de shaders carregado em tempo de execucao (d3dcompiler_47/46/43.dll).
   Qualquer falha ao iniciar => gpu.log explica e o jogo volta sozinho para o software.
   PREMISSAS (nao testadas no hardware real): ordem/ties de profundidade, sombras por volume modificador via stencil
   (paridade por malha, uniao das malhas, so em pixels receptores = RT1), translucido nao grava Z. */
int ta_gpu_mode=1;     /* 0 = software, 1 = Direct3D 11 (padrao; cai no software se falhar) */
#ifndef _WIN32
static int gpu_ready(void){ return 0; }
static int gpu_bg_add(const Tri*t){ (void)t; return 0; }
static int gpu_draw(const uint8_t*vram,const uint32_t*pal,uint32_t palctl){ (void)vram;(void)pal;(void)palctl; return 0; }
const char *ta_gpu_name(void){ return "SOFTWARE"; }
static int gpu_direct_ok(void){ return 0; }
static void gpu_direct_off(void){}
static void gpu_snap_capture(void){}
static void gpu_post_set(int m,int rw,int rh,int ow,int oh,float g){ (void)m;(void)rw;(void)rh;(void)ow;(void)oh;(void)g; }
int ta_gpu_present(int redo){ (void)redo; return 0; }
int ta_direct_active(void){ return 0; }
#else
#define COBJMACROS
#include <stdarg.h>
#include <d3d11.h>
#include <dxgi.h>
typedef HRESULT (WINAPI *PFN_D3DCOMPILE_)(LPCVOID,SIZE_T,LPCSTR,const void*,void*,LPCSTR,LPCSTR,UINT,UINT,ID3DBlob**,ID3DBlob**);

typedef struct { float x,y,z, r,g,b,a, uz,vz,iz, c0,c1,c2,c3; unsigned int f; } GV;   /* 60 bytes por vertice */
#define GF_TEX   (1u<<0)
#define GF_SHD(s) ((unsigned)(s)<<1)          /* bits 1-2: modo de sombreamento (0..3) */
#define GF_USEA  (1u<<3)
#define GF_IGNTA (1u<<4)
#define GF_CLAMP (1u<<5)
#define GF_PUNCH (1u<<6)
#define GF_SNAP  (1u<<7)
#define GF_SHADOW (1u<<8)

static int g_dead=0, g_init=0; static FILE *g_log=0;
static HMODULE g_dll=0, g_cmp=0;
static ID3D11Device *g_dev=0; static ID3D11DeviceContext *g_ctx=0;
static ID3D11VertexShader *g_vs=0,*g_vsq=0; static ID3D11PixelShader *g_ps=0,*g_psf=0,*g_psz=0; static ID3D11InputLayout *g_lay=0;
static ID3D11Buffer *g_vb=0,*g_cb=0; static size_t g_vbcap=0;
static ID3D11Texture2D *g_rt0=0,*g_rt1=0,*g_ds=0,*g_stg=0; static ID3D11RenderTargetView *g_rtv0=0,*g_rtv1=0; static ID3D11DepthStencilView *g_dsv=0; static ID3D11ShaderResourceView *g_srv1=0;
static int g_w=0,g_h=0;
static ID3D11Texture2D *g_whiteT=0; static ID3D11ShaderResourceView *g_white=0;
static ID3D11Texture2D *g_snapT=0; static ID3D11ShaderResourceView *g_snap=0; static int g_snapw=0,g_snaph=0;
static ID3D11SamplerState *g_smp[2][3][3][2]; static ID3D11SamplerState *g_smpSnap=0;
static ID3D11BlendState *g_bsOpq[2],*g_bsTr[8][8],*g_bsShadow=0,*g_bsNone=0;
static ID3D11DepthStencilState *g_dsS[8][2],*g_dsOff=0,*g_dsMark=0,*g_dsAcc=0,*g_dsClr=0,*g_dsFin=0;
static ID3D11RasterizerState *g_rs=0,*g_rsSc=0;
static char g_adapter[128]="?"; static int g_fl=0;
static GV *g_gv=0; static size_t g_gvn=0,g_gvcap=0;
typedef struct { size_t start,count; int list; const TC*tc; int snap; int fil,um,vm,bias; int dm,zw; int sf,df; } GBatch;
static GBatch *g_gb=0; static size_t g_gbn=0,g_gbcap=0;
static Tri g_bg[2]; static int g_nbg=0;
/* v15zzze: apresentacao direta (sem ler o quadro de volta para a CPU) */
static int g_direct=0;   /* 0 = ainda nao tentou, 1 = ativa, -1 = desligada */
static IDXGISwapChain *g_sc=0; static ID3D11RenderTargetView *g_bbrtv=0; static int g_swW=0,g_swH=0;
static ID3D11ShaderResourceView *g_srv0=0, *g_psrc=0, *g_snapsrc=0;
static ID3D11Texture2D *g_rt2=0; static ID3D11RenderTargetView *g_rtv2=0; static ID3D11ShaderResourceView *g_srv2=0; static int g_rt2w=0,g_rt2h=0;
static ID3D11VertexShader *g_vsp=0; static ID3D11PixelShader *g_psp=0,*g_pspr=0; static ID3D11Buffer *g_cbp=0; static ID3D11BlendState *g_bsP=0; static ID3D11SamplerState *g_smpL=0;
static ID3D11Texture2D *g_lastT=0; static ID3D11ShaderResourceView *g_lastSRV=0; static int g_lastw=0,g_lasth=0,g_lastok=0;
static int g_pm=0,g_prw=0,g_prh=0,g_pow=0,g_poh=0; static float g_pgain=1.f;

static void glog(const char*f,...){ if(!g_log) g_log=fopen("gpu.log","w"); if(!g_log) return; va_list ap; va_start(ap,f); vfprintf(g_log,f,ap); va_end(ap); fflush(g_log); }
#define GREL(p) do{ if(p){ IUnknown *u_=(IUnknown*)(p); u_->lpVtbl->Release(u_); (p)=0; } }while(0)
#define GCK(x,msg) do{ HRESULT hr_=(x); if(FAILED(hr_)){ glog("ERRO %s (hr=0x%08lx)\n",msg,(unsigned long)hr_); return 0; } }while(0)

static const char *g_hlsl =
"cbuffer C:register(b0){ float2 inv; float zsc; float shad; };\n"
"struct VI{ float3 p:POSITION; float4 c:COLOR0; float3 t:TEXCOORD0; float4 cb:TEXCOORD1; uint f:FLAGS; };\n"
"struct VO{ float4 p:SV_Position; noperspective float4 c:COLOR0; noperspective float3 t:TEXCOORD0; nointerpolation float4 cb:TEXCOORD1; nointerpolation uint f:FLAGS; };\n"
"VO vs(VI i){ VO o; o.p=float4(i.p.x*inv.x*2.0-1.0, 1.0-i.p.y*inv.y*2.0, saturate(i.p.z*zsc)*0.999+0.0005, 1.0); o.c=i.c; o.t=i.t; o.cb=i.cb; o.f=i.f; return o; }\n"
"Texture2D tx:register(t0); SamplerState sm:register(s0);\n"
"struct PO{ float4 c:SV_Target0; float s:SV_Target1; };\n"
"float4 sh(VO i){ uint f=i.f; bool tex=(f&1u)!=0u; uint shd=(f>>1u)&3u; bool usea=(f&8u)!=0u; bool igta=(f&16u)!=0u; bool clm=(f&32u)!=0u; bool pun=(f&64u)!=0u; bool snp=(f&128u)!=0u;\n"
"  float2 uv=i.t.xy/max(i.t.z,1e-12); if(snp) uv.y*=512.0/480.0; if(clm){ uv=float2(clamp(uv.x,i.cb.x,i.cb.y),clamp(uv.y,i.cb.z,i.cb.w)); }\n"
"  float4 tc=tx.Sample(sm,uv); if(snp) tc.a=1.0; if(igta) tc.a=1.0;\n"
"  float3 vc=i.c.rgb; float va=usea?i.c.a:1.0; float3 rgb=vc; float al=va;\n"
"  if(tex){ if(pun && tc.a<0.5) discard;\n"
"    if(shd==0u){ rgb=tc.rgb; al=tc.a; } else if(shd==1u){ rgb=vc*tc.rgb; al=tc.a; } else if(shd==2u){ rgb=vc*(1.0-tc.a)+tc.rgb*tc.a; al=va; } else { rgb=vc*tc.rgb; al=va*tc.a; } }\n"
"  else { al=usea?i.c.a:1.0; }\n"
"  if(pun && al<0.5) discard;\n"
"  return float4(saturate(rgb),saturate(al)); }\n"
"PO ps(VO i){ PO o; o.c=sh(i); o.s=((i.f&256u)!=0u)?1.0:0.0; return o; }\n"
"PO psz(VO i){ PO o; float4 c=sh(i); if(c.a<0.99) discard; o.c=c; o.s=0.0; return o; }\n"
"float4 vsq(uint id:SV_VertexID):SV_Position{ float2 p=float2((id<<1u)&2u,id&2u); return float4(p*float2(2.0,-2.0)+float2(-1.0,1.0),0.0,1.0); }\n"
"Texture2D<float> fl:register(t1);\n"
"float4 psf(float4 p:SV_Position):SV_Target{ if(fl.Load(int3((int)p.x,(int)p.y,0))<0.5) discard; return float4(shad,shad,shad,1.0); }\n";

static ID3DBlob *g_cmpblob(const char*ent,const char*prof){
    PFN_D3DCOMPILE_ fn=(PFN_D3DCOMPILE_)GetProcAddress(g_cmp,"D3DCompile"); if(!fn) return 0;
    ID3DBlob *code=0,*err=0; HRESULT hr=fn(g_hlsl,strlen(g_hlsl),"ct2.hlsl",0,0,ent,prof,0,0,&code,&err);
    if(FAILED(hr)){ glog("shader %s/%s falhou: %s\n",ent,prof,err?(const char*)err->lpVtbl->GetBufferPointer(err):"?"); if(err) err->lpVtbl->Release(err); return 0; }
    if(err) err->lpVtbl->Release(err); return code; }

static int g_mkdss(ID3D11DepthStencilState**out,int de,int dw,D3D11_COMPARISON_FUNC fn,int se,UINT rm,UINT wm,D3D11_COMPARISON_FUNC sfn,D3D11_STENCIL_OP pass){
    D3D11_DEPTH_STENCIL_DESC d; memset(&d,0,sizeof d); d.DepthEnable=de; d.DepthWriteMask=dw?D3D11_DEPTH_WRITE_MASK_ALL:D3D11_DEPTH_WRITE_MASK_ZERO; d.DepthFunc=fn; d.StencilEnable=se; d.StencilReadMask=(UINT8)rm; d.StencilWriteMask=(UINT8)wm;
    d.FrontFace.StencilFailOp=D3D11_STENCIL_OP_KEEP; d.FrontFace.StencilDepthFailOp=D3D11_STENCIL_OP_KEEP; d.FrontFace.StencilPassOp=pass; d.FrontFace.StencilFunc=sfn; d.BackFace=d.FrontFace;
    GCK(ID3D11Device_CreateDepthStencilState(g_dev,&d,out),"CreateDepthStencilState"); return 1; }

static int gpu_init(void){
    g_init=1; glog("# gpu.log - Direct3D 11 (v15zw)\n");
    g_dll=LoadLibraryA("d3d11.dll"); if(!g_dll){ glog("d3d11.dll nao encontrada\n"); return 0; }
    PFN_D3D11_CREATE_DEVICE cd=(PFN_D3D11_CREATE_DEVICE)GetProcAddress(g_dll,"D3D11CreateDevice"); if(!cd){ glog("D3D11CreateDevice ausente\n"); return 0; }
    static const D3D_FEATURE_LEVEL lv[3]={D3D_FEATURE_LEVEL_11_0,D3D_FEATURE_LEVEL_10_1,D3D_FEATURE_LEVEL_10_0}; D3D_FEATURE_LEVEL got=0;
    HRESULT hr=cd(0,D3D_DRIVER_TYPE_HARDWARE,0,0,lv,3,D3D11_SDK_VERSION,&g_dev,&got,&g_ctx);
    if(FAILED(hr)){ glog("D3D11CreateDevice (hardware) falhou hr=0x%08lx\n",(unsigned long)hr); return 0; }
    g_fl=(int)got; glog("dispositivo criado, feature level 0x%x\n",(unsigned)got);
    { IDXGIDevice *dd=0; /* nome do adaptador: so informativo; sem dxguid usamos o GUID na mao */
      static const GUID iid={0x54ec77fa,0x1377,0x44e6,{0x8c,0x32,0x88,0xfd,0x5f,0x44,0xc8,0x4c}};
      if(SUCCEEDED(((IUnknown*)g_dev)->lpVtbl->QueryInterface((IUnknown*)g_dev,&iid,(void**)&dd))&&dd){ IDXGIAdapter *ad=0; if(SUCCEEDED(dd->lpVtbl->GetAdapter(dd,&ad))&&ad){ DXGI_ADAPTER_DESC desc; if(SUCCEEDED(ad->lpVtbl->GetDesc(ad,&desc))){ WideCharToMultiByte(CP_ACP,0,desc.Description,-1,g_adapter,sizeof g_adapter,0,0); glog("adaptador: %s (VRAM dedicada %u MB)\n",g_adapter,(unsigned)(desc.DedicatedVideoMemory>>20)); } ad->lpVtbl->Release(ad); } dd->lpVtbl->Release(dd); } }
    { static const char*nm[3]={"d3dcompiler_47.dll","d3dcompiler_46.dll","d3dcompiler_43.dll"}; for(int i=0;i<3&&!g_cmp;i++) g_cmp=LoadLibraryA(nm[i]); }
    if(!g_cmp){ glog("nenhum d3dcompiler_4x.dll encontrado (necessario para compilar os shaders)\n"); return 0; }
    ID3DBlob *bvs=g_cmpblob("vs","vs_4_0"),*bps=g_cmpblob("ps","ps_4_0"),*bvq=g_cmpblob("vsq","vs_4_0"),*bpf=g_cmpblob("psf","ps_4_0"),*bpz=g_cmpblob("psz","ps_4_0"); if(!bvs||!bps||!bvq||!bpf||!bpz) return 0;
    GCK(ID3D11Device_CreateVertexShader(g_dev,bvs->lpVtbl->GetBufferPointer(bvs),bvs->lpVtbl->GetBufferSize(bvs),0,&g_vs),"CreateVertexShader");
    GCK(ID3D11Device_CreateVertexShader(g_dev,bvq->lpVtbl->GetBufferPointer(bvq),bvq->lpVtbl->GetBufferSize(bvq),0,&g_vsq),"CreateVertexShader q");
    GCK(ID3D11Device_CreatePixelShader(g_dev,bps->lpVtbl->GetBufferPointer(bps),bps->lpVtbl->GetBufferSize(bps),0,&g_ps),"CreatePixelShader");
    GCK(ID3D11Device_CreatePixelShader(g_dev,bpf->lpVtbl->GetBufferPointer(bpf),bpf->lpVtbl->GetBufferSize(bpf),0,&g_psf),"CreatePixelShader f");
    GCK(ID3D11Device_CreatePixelShader(g_dev,bpz->lpVtbl->GetBufferPointer(bpz),bpz->lpVtbl->GetBufferSize(bpz),0,&g_psz),"CreatePixelShader z");
    { D3D11_INPUT_ELEMENT_DESC el[5]={ {"POSITION",0,DXGI_FORMAT_R32G32B32_FLOAT,0,0,D3D11_INPUT_PER_VERTEX_DATA,0},{"COLOR",0,DXGI_FORMAT_R32G32B32A32_FLOAT,0,12,D3D11_INPUT_PER_VERTEX_DATA,0},
        {"TEXCOORD",0,DXGI_FORMAT_R32G32B32_FLOAT,0,28,D3D11_INPUT_PER_VERTEX_DATA,0},{"TEXCOORD",1,DXGI_FORMAT_R32G32B32A32_FLOAT,0,40,D3D11_INPUT_PER_VERTEX_DATA,0},{"FLAGS",0,DXGI_FORMAT_R32_UINT,0,56,D3D11_INPUT_PER_VERTEX_DATA,0} };
      GCK(ID3D11Device_CreateInputLayout(g_dev,el,5,bvs->lpVtbl->GetBufferPointer(bvs),bvs->lpVtbl->GetBufferSize(bvs),&g_lay),"CreateInputLayout"); }
    { D3D11_BUFFER_DESC bd; memset(&bd,0,sizeof bd); bd.ByteWidth=16; bd.Usage=D3D11_USAGE_DEFAULT; bd.BindFlags=D3D11_BIND_CONSTANT_BUFFER; GCK(ID3D11Device_CreateBuffer(g_dev,&bd,0,&g_cb),"CreateBuffer cb"); }
    { D3D11_RASTERIZER_DESC rd; memset(&rd,0,sizeof rd); rd.FillMode=D3D11_FILL_SOLID; rd.CullMode=D3D11_CULL_NONE; rd.DepthClipEnable=TRUE; GCK(ID3D11Device_CreateRasterizerState(g_dev,&rd,&g_rs),"rs"); rd.ScissorEnable=TRUE; GCK(ID3D11Device_CreateRasterizerState(g_dev,&rd,&g_rsSc),"rs sc"); }
    /* profundidade: z do PVR = 1/w (maior = mais perto); modos ISP 0..7 */
    { static const D3D11_COMPARISON_FUNC fn[8]={D3D11_COMPARISON_NEVER,D3D11_COMPARISON_LESS,D3D11_COMPARISON_EQUAL,D3D11_COMPARISON_LESS_EQUAL,D3D11_COMPARISON_GREATER_EQUAL,D3D11_COMPARISON_NOT_EQUAL,D3D11_COMPARISON_GREATER_EQUAL,D3D11_COMPARISON_ALWAYS};
      for(int m=0;m<8;m++) for(int w=0;w<2;w++) if(!g_mkdss(&g_dsS[m][w],1,w,fn[m],0,0,0,D3D11_COMPARISON_ALWAYS,D3D11_STENCIL_OP_KEEP)) return 0; }
    if(!g_mkdss(&g_dsOff,0,0,D3D11_COMPARISON_ALWAYS,0,0,0,D3D11_COMPARISON_ALWAYS,D3D11_STENCIL_OP_KEEP)) return 0;
    if(!g_mkdss(&g_dsMark,1,0,D3D11_COMPARISON_GREATER,1,0xFF,0x01,D3D11_COMPARISON_ALWAYS,D3D11_STENCIL_OP_INVERT)) return 0;           /* paridade no bit 0 */
    if(!g_mkdss(&g_dsAcc,0,0,D3D11_COMPARISON_ALWAYS,1,0x01,0x02,D3D11_COMPARISON_EQUAL,D3D11_STENCIL_OP_REPLACE)) return 0;           /* bit0==1 => acc (bit 1) = 1 (ref=3) */
    if(!g_mkdss(&g_dsClr,0,0,D3D11_COMPARISON_ALWAYS,1,0x00,0x01,D3D11_COMPARISON_ALWAYS,D3D11_STENCIL_OP_REPLACE)) return 0;          /* zera o bit 0 (ref=0) */
    if(!g_mkdss(&g_dsFin,0,0,D3D11_COMPARISON_ALWAYS,1,0x02,0x00,D3D11_COMPARISON_EQUAL,D3D11_STENCIL_OP_KEEP)) return 0;              /* so onde acc (ref=2) */
    /* mistura */
    { D3D11_BLEND_DESC b; memset(&b,0,sizeof b); b.IndependentBlendEnable=TRUE;
      for(int w=0;w<2;w++){ memset(&b,0,sizeof b); b.IndependentBlendEnable=TRUE; for(int k=0;k<2;k++){ b.RenderTarget[k].SrcBlend=D3D11_BLEND_ONE; b.RenderTarget[k].DestBlend=D3D11_BLEND_ZERO; b.RenderTarget[k].BlendOp=D3D11_BLEND_OP_ADD; b.RenderTarget[k].SrcBlendAlpha=D3D11_BLEND_ONE; b.RenderTarget[k].DestBlendAlpha=D3D11_BLEND_ZERO; b.RenderTarget[k].BlendOpAlpha=D3D11_BLEND_OP_ADD; }
        b.RenderTarget[0].RenderTargetWriteMask=D3D11_COLOR_WRITE_ENABLE_ALL; b.RenderTarget[1].RenderTargetWriteMask=w?D3D11_COLOR_WRITE_ENABLE_ALL:0; GCK(ID3D11Device_CreateBlendState(g_dev,&b,&g_bsOpq[w]),"blend opq"); }
      static const D3D11_BLEND sfm[8]={D3D11_BLEND_ZERO,D3D11_BLEND_ONE,D3D11_BLEND_DEST_COLOR,D3D11_BLEND_INV_DEST_COLOR,D3D11_BLEND_SRC_ALPHA,D3D11_BLEND_INV_SRC_ALPHA,D3D11_BLEND_ONE,D3D11_BLEND_ZERO};
      static const D3D11_BLEND dfm[8]={D3D11_BLEND_ZERO,D3D11_BLEND_ONE,D3D11_BLEND_SRC_COLOR,D3D11_BLEND_INV_SRC_COLOR,D3D11_BLEND_SRC_ALPHA,D3D11_BLEND_INV_SRC_ALPHA,D3D11_BLEND_ONE,D3D11_BLEND_ZERO};   /* mesmos fatores do shade(): 6 = 1, 7 = 0 */
      for(int s=0;s<8;s++) for(int d=0;d<8;d++){ memset(&b,0,sizeof b); b.IndependentBlendEnable=TRUE; b.RenderTarget[0].BlendEnable=TRUE; b.RenderTarget[0].SrcBlend=sfm[s]; b.RenderTarget[0].DestBlend=dfm[d]; b.RenderTarget[0].BlendOp=D3D11_BLEND_OP_ADD;
          b.RenderTarget[0].SrcBlendAlpha=D3D11_BLEND_ONE; b.RenderTarget[0].DestBlendAlpha=D3D11_BLEND_ZERO; b.RenderTarget[0].BlendOpAlpha=D3D11_BLEND_OP_ADD; b.RenderTarget[0].RenderTargetWriteMask=D3D11_COLOR_WRITE_ENABLE_ALL; b.RenderTarget[1].RenderTargetWriteMask=0;
          GCK(ID3D11Device_CreateBlendState(g_dev,&b,&g_bsTr[s][d]),"blend tr"); }
      memset(&b,0,sizeof b); b.RenderTarget[0].BlendEnable=TRUE; b.RenderTarget[0].SrcBlend=D3D11_BLEND_ZERO; b.RenderTarget[0].DestBlend=D3D11_BLEND_SRC_COLOR; b.RenderTarget[0].BlendOp=D3D11_BLEND_OP_ADD; b.RenderTarget[0].SrcBlendAlpha=D3D11_BLEND_ZERO; b.RenderTarget[0].DestBlendAlpha=D3D11_BLEND_ONE; b.RenderTarget[0].BlendOpAlpha=D3D11_BLEND_OP_ADD; b.RenderTarget[0].RenderTargetWriteMask=D3D11_COLOR_WRITE_ENABLE_ALL;
      GCK(ID3D11Device_CreateBlendState(g_dev,&b,&g_bsShadow),"blend sombra");
      memset(&b,0,sizeof b); GCK(ID3D11Device_CreateBlendState(g_dev,&b,&g_bsNone),"blend none"); }
    /* textura branca 1x1 (triangulos sem textura) */
    { unsigned int px=0xFFFFFFFFu; D3D11_TEXTURE2D_DESC td; memset(&td,0,sizeof td); td.Width=1; td.Height=1; td.MipLevels=1; td.ArraySize=1; td.Format=DXGI_FORMAT_B8G8R8A8_UNORM; td.SampleDesc.Count=1; td.Usage=D3D11_USAGE_IMMUTABLE; td.BindFlags=D3D11_BIND_SHADER_RESOURCE;
      D3D11_SUBRESOURCE_DATA sd; sd.pSysMem=&px; sd.SysMemPitch=4; sd.SysMemSlicePitch=0; GCK(ID3D11Device_CreateTexture2D(g_dev,&td,&sd,&g_whiteT),"white tex"); GCK(ID3D11Device_CreateShaderResourceView(g_dev,(ID3D11Resource*)g_whiteT,0,&g_white),"white srv"); }
    glog("inicializacao concluida\n"); return 1;
}

static ID3D11SamplerState *gpu_sampler(int fil,int um,int vm,int bias){
    ID3D11SamplerState **s=&g_smp[fil][um][vm][bias]; if(*s) return *s;
    D3D11_SAMPLER_DESC d; memset(&d,0,sizeof d); d.Filter=fil?D3D11_FILTER_ANISOTROPIC:D3D11_FILTER_MIN_MAG_MIP_POINT;   /* v15zz: anisotropico nativo 16x nas texturas filtradas (custo so na GPU) */
    static const D3D11_TEXTURE_ADDRESS_MODE am[3]={D3D11_TEXTURE_ADDRESS_WRAP,D3D11_TEXTURE_ADDRESS_CLAMP,D3D11_TEXTURE_ADDRESS_MIRROR};
    d.AddressU=am[um]; d.AddressV=am[vm]; d.AddressW=D3D11_TEXTURE_ADDRESS_CLAMP; d.MipLODBias=fil?(bias?ta_bias_mip:ta_bias_nomip):0.f; d.MaxAnisotropy=fil?16:1; d.ComparisonFunc=D3D11_COMPARISON_NEVER; d.MinLOD=0; d.MaxLOD=fil?D3D11_FLOAT32_MAX:0.f;
    if(FAILED(ID3D11Device_CreateSamplerState(g_dev,&d,s))) *s=0; return *s; }

/* textura da GPU para uma entrada do cache de texturas (recria se o conteudo foi redecodificado) */
static ID3D11ShaderResourceView *gpu_tex(TC*e){
    if(!e||!e->d) return 0;
    if(e->gsrv&&e->ggen==e->gen&&e->gw==e->sw&&e->gh==e->sh) return (ID3D11ShaderResourceView*)e->gsrv;
    GREL(e->gsrv); GREL(e->gtex);
    int lv=e->nl+1; if(lv>12) lv=12; D3D11_SUBRESOURCE_DATA sd[12]; int w=e->sw,h=e->sh;
    for(int l=0;l<lv;l++){ int lw=e->sw>>l; if(lw<1) lw=1; int lh=e->sh>>l; if(lh<1) lh=1; sd[l].pSysMem=l?(const void*)e->mp[l]:(const void*)e->d; sd[l].SysMemPitch=(UINT)(lw*4); sd[l].SysMemSlicePitch=0; }
    D3D11_TEXTURE2D_DESC td; memset(&td,0,sizeof td); td.Width=w; td.Height=h; td.MipLevels=lv; td.ArraySize=1; td.Format=DXGI_FORMAT_B8G8R8A8_UNORM; td.SampleDesc.Count=1; td.Usage=D3D11_USAGE_IMMUTABLE; td.BindFlags=D3D11_BIND_SHADER_RESOURCE;
    ID3D11Texture2D *t=0; if(FAILED(ID3D11Device_CreateTexture2D(g_dev,&td,sd,&t))) return 0;
    ID3D11ShaderResourceView *v=0; if(FAILED(ID3D11Device_CreateShaderResourceView(g_dev,(ID3D11Resource*)t,0,&v))){ GREL(t); return 0; }
    e->gtex=t; e->gsrv=v; e->ggen=e->gen; e->gw=e->sw; e->gh=e->sh; return v; }

static int gpu_targets(void){
    if(g_w==RW&&g_h==RH&&g_rt0) return 1;
    GREL(g_srv0); g_psrc=0; GREL(g_srv1); GREL(g_rtv0); GREL(g_rtv1); GREL(g_dsv); GREL(g_rt0); GREL(g_rt1); GREL(g_ds); GREL(g_stg);
    D3D11_TEXTURE2D_DESC td; memset(&td,0,sizeof td); td.Width=RW; td.Height=RH; td.MipLevels=1; td.ArraySize=1; td.SampleDesc.Count=1; td.Usage=D3D11_USAGE_DEFAULT;
    td.Format=DXGI_FORMAT_B8G8R8A8_UNORM; td.BindFlags=D3D11_BIND_RENDER_TARGET|D3D11_BIND_SHADER_RESOURCE; GCK(ID3D11Device_CreateTexture2D(g_dev,&td,0,&g_rt0),"rt0");
    td.Format=DXGI_FORMAT_R8_UNORM; td.BindFlags=D3D11_BIND_RENDER_TARGET|D3D11_BIND_SHADER_RESOURCE; GCK(ID3D11Device_CreateTexture2D(g_dev,&td,0,&g_rt1),"rt1");
    td.Format=DXGI_FORMAT_D24_UNORM_S8_UINT; td.BindFlags=D3D11_BIND_DEPTH_STENCIL; GCK(ID3D11Device_CreateTexture2D(g_dev,&td,0,&g_ds),"ds");
    td.Format=DXGI_FORMAT_B8G8R8A8_UNORM; td.BindFlags=0; td.Usage=D3D11_USAGE_STAGING; td.CPUAccessFlags=D3D11_CPU_ACCESS_READ; GCK(ID3D11Device_CreateTexture2D(g_dev,&td,0,&g_stg),"staging");
    GCK(ID3D11Device_CreateRenderTargetView(g_dev,(ID3D11Resource*)g_rt0,0,&g_rtv0),"rtv0"); GCK(ID3D11Device_CreateRenderTargetView(g_dev,(ID3D11Resource*)g_rt1,0,&g_rtv1),"rtv1");
    GCK(ID3D11Device_CreateDepthStencilView(g_dev,(ID3D11Resource*)g_ds,0,&g_dsv),"dsv"); GCK(ID3D11Device_CreateShaderResourceView(g_dev,(ID3D11Resource*)g_rt1,0,&g_srv1),"srv1");
    if(FAILED(ID3D11Device_CreateShaderResourceView(g_dev,(ID3D11Resource*)g_rt0,0,&g_srv0))){ g_srv0=0; glog("aviso: srv do rt0 indisponivel (apresentacao direta desligada)\n"); }
    g_w=RW; g_h=RH; glog("alvos %dx%d\n",RW,RH); return 1; }

static int gpu_ready(void){
    if(g_dead||ta_gpu_mode!=1) return 0; static int envd=0; if(!envd){ envd=1; const char*e=GENV("CT2_GPU"); if(e&&e[0]=='0'){ g_dead=1; return 0; } }
    if(!g_init){ if(!gpu_init()){ g_dead=1; glog("=> usando o renderizador por software\n"); return 0; } }
    return g_dev!=0; }
const char *ta_gpu_name(void){ return (!g_dead&&ta_gpu_mode==1)?"D3D11":"SOFTWARE"; }
static int gpu_bg_add(const Tri*t){ if(g_nbg<2) g_bg[g_nbg++]=*t; return 1; }

static void gv_push(const Tri*t,unsigned flags){
    if(g_gvn+3>g_gvcap){ g_gvcap=g_gvcap*2+16384; g_gv=(GV*)realloc(g_gv,g_gvcap*sizeof(GV)); }
    for(int k=0;k<3;k++){ const Vtx*v=&t->v[k]; GV*g=&g_gv[g_gvn++]; g->x=v->x; g->y=v->y; g->z=v->z; g->r=v->r; g->g=v->g; g->b=v->b; g->a=v->a; g->uz=v->u*v->z; g->vz=v->v*v->z; g->iz=v->z;
        g->c0=t->cu0; g->c1=t->cu1; g->c2=t->cv0; g->c3=t->cv1; g->f=flags; } }
static void gb_add(const GBatch*b){ if(g_gbn>=g_gbcap){ g_gbcap=g_gbcap*2+256; g_gb=(GBatch*)realloc(g_gb,g_gbcap*sizeof(GBatch)); } g_gb[g_gbn++]=*b; }

static int gpu_cull(const Tri*t){
    const Vtx *a=&t->v[0],*b=&t->v[1],*c=&t->v[2];
    double den=((double)b->y-c->y)*((double)a->x-c->x)+((double)c->x-b->x)*((double)a->y-c->y); if(fabs(den)<1e-6) return 1;
    int cm=(t->isp>>27)&3; if(cm>=2 && !ta_dbg[1] && !GENV("CT2_NOCULL")){ if((cm==2&&den<0)||(cm==3&&den>0)) return 1; }
    return 0; }

static void gpu_set_cb(void){ float c[4]={1.f/(float)RW,1.f/(float)RH,0.25f,ta_shad}; ID3D11DeviceContext_UpdateSubresource(g_ctx,(ID3D11Resource*)g_cb,0,0,c,0,0); }
static void gpu_quad(ID3D11DepthStencilState*ds,UINT ref,int scissor,int x0,int y0,int x1,int y1,ID3D11PixelShader*ps,ID3D11BlendState*bs,int rtvs){
    D3D11_RECT r; r.left=x0; r.top=y0; r.right=x1; r.bottom=y1; ID3D11DeviceContext_RSSetScissorRects(g_ctx,1,&r);
    ID3D11DeviceContext_RSSetState(g_ctx,scissor?g_rsSc:g_rs); ID3D11DeviceContext_OMSetDepthStencilState(g_ctx,ds,ref); float bf[4]={0,0,0,0}; ID3D11DeviceContext_OMSetBlendState(g_ctx,bs,bf,0xFFFFFFFFu);
    ID3D11DeviceContext_IASetInputLayout(g_ctx,0); ID3D11DeviceContext_IASetPrimitiveTopology(g_ctx,D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    ID3D11DeviceContext_VSSetShader(g_ctx,g_vsq,0,0); ID3D11DeviceContext_PSSetShader(g_ctx,ps,0,0); (void)rtvs;
    ID3D11DeviceContext_Draw(g_ctx,3,0); }

static void gpu_draw_batches(int list){
    for(size_t i=0;i<g_gbn;i++){ const GBatch*b=&g_gb[i]; if(b->list!=list) continue;
        ID3D11ShaderResourceView *srv=g_white; ID3D11SamplerState *sm=0;
        if(b->snap){ srv=g_snapsrc?g_snapsrc:(g_snap?g_snap:g_white); sm=g_smpSnap; }
        else if(b->tc){ ID3D11ShaderResourceView *v=gpu_tex((TC*)b->tc); if(v){ srv=v; sm=gpu_sampler(b->fil,b->um,b->vm,b->bias); } }
        if(!sm) sm=gpu_sampler(0,1,1,0);
        ID3D11DeviceContext_PSSetShaderResources(g_ctx,0,1,&srv); ID3D11DeviceContext_PSSetSamplers(g_ctx,0,1,&sm);
        ID3D11BlendState *bs; if(list==2) bs=g_bsTr[b->sf&7][b->df&7]; else bs=g_bsOpq[b->zw];
        float bf[4]={0,0,0,0}; ID3D11DeviceContext_OMSetBlendState(g_ctx,bs,bf,0xFFFFFFFFu);
        ID3D11DeviceContext_OMSetDepthStencilState(g_ctx,g_dsS[b->dm&7][list==2?0:b->zw],0);
        ID3D11DeviceContext_Draw(g_ctx,(UINT)b->count,(UINT)b->start);
        /* translucido com alfa ~1 (corpo dos carros) tambem grava Z, como no software: segunda passada so de profundidade */
        if(list==2&&b->zw&&!b->snap){ float bf2[4]={0,0,0,0}; ID3D11DeviceContext_OMSetBlendState(g_ctx,g_bsNone,bf2,0xFFFFFFFFu); ID3D11DeviceContext_OMSetDepthStencilState(g_ctx,g_dsS[b->dm&7][1],0);
            ID3D11DeviceContext_PSSetShader(g_ctx,g_psz,0,0); ID3D11DeviceContext_Draw(g_ctx,(UINT)b->count,(UINT)b->start); ID3D11DeviceContext_PSSetShader(g_ctx,g_ps,0,0); } } }

static int gpu_draw(const uint8_t*vram,const uint32_t*pal,uint32_t palctl){
    (void)vram;(void)pal;(void)palctl;
    if(!gpu_targets()){ g_dead=1; glog("=> usando o renderizador por software\n"); g_nbg=0; return 0; }
    g_gvn=0; g_gbn=0;
    /* fundo: 2 triangulos sem profundidade */
    size_t bgcount=0; for(int k=0;k<g_nbg;k++){ gv_push(&g_bg[k],0); bgcount+=3; }
    GBatch bg; memset(&bg,0,sizeof bg); bg.start=0; bg.count=bgcount; bg.list=9; bg.dm=7; bg.zw=0;
    /* listas 0 (opaco), 4 (recorte) e 2 (translucido, ja ordenado) */
    static const int order[3]={0,4,2}; size_t vstart[3];
    for(int p=0;p<3;p++){ int list=order[p]; vstart[p]=g_gvn; GBatch cur; memset(&cur,0,sizeof cur); int open=0;
        for(size_t i=0;i<ntri;i++){ const Tri*t=&tris[i]; if(t->list!=list) continue; if(gpu_cull(t)) continue;
            int textured=t->textured&&(t->tc||t->snap); const TC*tc=textured&&!t->snap?(const TC*)t->tc:0; uint32_t tsp=t->tsp;
            int dm=(t->isp>>29)&7; if(dm==0&&list==2) dm=6; int zw=!((t->isp>>26)&1);
            int fil=((tsp>>13)&3)?1:0; int um=((tsp>>16)&1)?1:(((tsp>>18)&1)?2:0), vm=((tsp>>15)&1)?1:(((tsp>>17)&1)?2:0); int bias=((t->tex>>31)&1)?1:0;
            if(!textured){ fil=0; um=vm=1; bias=0; }
            int sf=(tsp>>29)&7, df=(tsp>>26)&7;
            unsigned fl=0; if(t->textured) fl|=GF_TEX; fl|=GF_SHD((tsp>>6)&3); if((tsp>>20)&1) fl|=GF_USEA; if((tsp>>19)&1) fl|=GF_IGNTA; if(t->clampuv) fl|=GF_CLAMP; if(list==4) fl|=GF_PUNCH; if(t->snap) fl|=GF_SNAP; if(t->shadow&&zw) fl|=GF_SHADOW;
            if(!t->textured) fl&=~(GF_IGNTA);
            if(!(open && cur.tc==tc && cur.snap==t->snap && cur.fil==fil && cur.um==um && cur.vm==vm && cur.bias==bias && cur.dm==dm && cur.zw==zw && (list!=2||(cur.sf==sf&&cur.df==df)))){
                if(open) gb_add(&cur); memset(&cur,0,sizeof cur); cur.start=g_gvn; cur.list=list; cur.tc=tc; cur.snap=t->snap; cur.fil=fil; cur.um=um; cur.vm=vm; cur.bias=bias; cur.dm=dm; cur.zw=zw; cur.sf=sf; cur.df=df; open=1; }
            gv_push(t,fl); cur.count+=3; }
        if(open) gb_add(&cur); }
    /* volumes modificadores: z reduzido pela tolerancia (face "atras da superficie": z > zb*(1+tol)) */
    size_t mvstart=g_gvn; int nmesh=0; { Tri tt; memset(&tt,0,sizeof tt); float k=1.f/(1.f+ta_shtol);
        for(size_t i=0;i<nmv;i++){ const MV*m=&mvs[i]; for(int q=0;q<3;q++){ tt.v[q].x=(float)m->x[q]; tt.v[q].y=(float)m->y[q]; tt.v[q].z=(float)m->z[q]*k; } gv_push(&tt,0); } if(nmv) nmesh=mvs[nmv-1].mesh+1; }
    /* envia vertices */
    size_t need=g_gvn*sizeof(GV); if(need==0) need=sizeof(GV);
    if(need>g_vbcap){ GREL(g_vb); g_vbcap=need*2; D3D11_BUFFER_DESC bd; memset(&bd,0,sizeof bd); bd.ByteWidth=(UINT)g_vbcap; bd.Usage=D3D11_USAGE_DYNAMIC; bd.BindFlags=D3D11_BIND_VERTEX_BUFFER; bd.CPUAccessFlags=D3D11_CPU_ACCESS_WRITE; if(FAILED(ID3D11Device_CreateBuffer(g_dev,&bd,0,&g_vb))){ g_dead=1; glog("ERRO vertex buffer\n"); return 0; } }
    { D3D11_MAPPED_SUBRESOURCE ms; if(FAILED(ID3D11DeviceContext_Map(g_ctx,(ID3D11Resource*)g_vb,0,D3D11_MAP_WRITE_DISCARD,0,&ms))){ g_dead=1; glog("ERRO Map vb\n"); return 0; } memcpy(ms.pData,g_gv,g_gvn*sizeof(GV)); ID3D11DeviceContext_Unmap(g_ctx,(ID3D11Resource*)g_vb,0); }
    /* snapshot do ultimo quadro 3D (fundo do menu de pausa) */
    g_snapsrc=0;
    if(snap_used&&have_scene&&g_direct>0&&g_lastok&&g_lastSRV&&g_lastw==ls_w&&g_lasth==ls_h){ g_snapsrc=g_lastSRV; if(!g_smpSnap) g_smpSnap=gpu_sampler(0,1,1,0); }   /* v15zzze: fundo do menu de pausa copiado na propria GPU */
    else if(snap_used&&have_scene){ if(!g_snapT||g_snapw!=ls_w||g_snaph!=ls_h){ GREL(g_snap); GREL(g_snapT); D3D11_TEXTURE2D_DESC td; memset(&td,0,sizeof td); td.Width=ls_w; td.Height=ls_h; td.MipLevels=1; td.ArraySize=1; td.Format=DXGI_FORMAT_B8G8R8A8_UNORM; td.SampleDesc.Count=1; td.Usage=D3D11_USAGE_DEFAULT; td.BindFlags=D3D11_BIND_SHADER_RESOURCE;
            if(SUCCEEDED(ID3D11Device_CreateTexture2D(g_dev,&td,0,&g_snapT))&&SUCCEEDED(ID3D11Device_CreateShaderResourceView(g_dev,(ID3D11Resource*)g_snapT,0,&g_snap))){ g_snapw=ls_w; g_snaph=ls_h; } else { GREL(g_snapT); } }
        if(g_snapT) ID3D11DeviceContext_UpdateSubresource(g_ctx,(ID3D11Resource*)g_snapT,0,0,lastscene,(UINT)(ls_w*4),0);
        if(!g_smpSnap) g_smpSnap=gpu_sampler(0,1,1,0); }
    else if(!g_smpSnap) g_smpSnap=gpu_sampler(0,1,1,0);
    /* estado base */
    ID3D11DeviceContext_ClearState(g_ctx);
    { float cc[4]={ta_bars_black?0.f:0x20/255.f,ta_bars_black?0.f:0x20/255.f,ta_bars_black?0.f:0x20/255.f,1.f}, z4[4]={0,0,0,0}; ID3D11DeviceContext_ClearRenderTargetView(g_ctx,g_rtv0,cc); ID3D11DeviceContext_ClearRenderTargetView(g_ctx,g_rtv1,z4); ID3D11DeviceContext_ClearDepthStencilView(g_ctx,g_dsv,D3D11_CLEAR_DEPTH|D3D11_CLEAR_STENCIL,0.f,0); }
    { D3D11_VIEWPORT vp; vp.TopLeftX=0; vp.TopLeftY=0; vp.Width=(float)RW; vp.Height=(float)RH; vp.MinDepth=0; vp.MaxDepth=1; ID3D11DeviceContext_RSSetViewports(g_ctx,1,&vp); }
    gpu_set_cb();
    { ID3D11RenderTargetView *rt[2]={g_rtv0,g_rtv1}; ID3D11DeviceContext_OMSetRenderTargets(g_ctx,2,rt,g_dsv); }
    ID3D11DeviceContext_RSSetState(g_ctx,g_rs);
    { UINT stride=sizeof(GV),off=0; ID3D11DeviceContext_IASetVertexBuffers(g_ctx,0,1,&g_vb,&stride,&off); ID3D11DeviceContext_IASetInputLayout(g_ctx,g_lay); ID3D11DeviceContext_IASetPrimitiveTopology(g_ctx,D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST); }
    ID3D11DeviceContext_VSSetShader(g_ctx,g_vs,0,0); ID3D11DeviceContext_VSSetConstantBuffers(g_ctx,0,1,&g_cb); ID3D11DeviceContext_PSSetShader(g_ctx,g_ps,0,0); ID3D11DeviceContext_PSSetConstantBuffers(g_ctx,0,1,&g_cb);
    /* fundo */
    if(bg.count){ ID3D11ShaderResourceView *w=g_white; ID3D11SamplerState *sm=gpu_sampler(0,1,1,0); ID3D11DeviceContext_PSSetShaderResources(g_ctx,0,1,&w); ID3D11DeviceContext_PSSetSamplers(g_ctx,0,1,&sm);
        float bf[4]={0,0,0,0}; ID3D11DeviceContext_OMSetBlendState(g_ctx,g_bsOpq[0],bf,0xFFFFFFFFu); ID3D11DeviceContext_OMSetDepthStencilState(g_ctx,g_dsOff,0); ID3D11DeviceContext_Draw(g_ctx,(UINT)bg.count,0); }
    gpu_draw_batches(0); gpu_draw_batches(4);
    /* sombras: paridade por malha (stencil bit 0), uniao das malhas (bit 1), aplicada so onde o pixel e receptor (RT1) */
    if(nmv>0&&ta_shad<0.999f){
        { ID3D11DeviceContext_OMSetRenderTargets(g_ctx,0,0,g_dsv); }
        ID3D11DeviceContext_PSSetShader(g_ctx,0,0,0); ID3D11DeviceContext_RSSetState(g_ctx,g_rs);
        size_t k=0; while(k<nmv){ int mid=mvs[k].mesh; size_t e=k; float bx0=1e9f,by0=1e9f,bx1=-1e9f,by1=-1e9f;
            for(;e<nmv&&mvs[e].mesh==mid;e++) for(int q=0;q<3;q++){ float x=(float)mvs[e].x[q],y=(float)mvs[e].y[q]; if(x<bx0)bx0=x; if(x>bx1)bx1=x; if(y<by0)by0=y; if(y>by1)by1=y; }
            int x0=(int)floorf(bx0)-1,y0=(int)floorf(by0)-1,x1=(int)ceilf(bx1)+2,y1=(int)ceilf(by1)+2; if(x0<0)x0=0; if(y0<0)y0=0; if(x1>RW)x1=RW; if(y1>RH)y1=RH;
            if(x1>x0&&y1>y0){
                D3D11_RECT r; r.left=x0; r.top=y0; r.right=x1; r.bottom=y1; ID3D11DeviceContext_RSSetScissorRects(g_ctx,1,&r); ID3D11DeviceContext_RSSetState(g_ctx,g_rsSc);
                { UINT stride=sizeof(GV),off=0; ID3D11DeviceContext_IASetVertexBuffers(g_ctx,0,1,&g_vb,&stride,&off); ID3D11DeviceContext_IASetInputLayout(g_ctx,g_lay); ID3D11DeviceContext_VSSetShader(g_ctx,g_vs,0,0); }
                float bf[4]={0,0,0,0}; ID3D11DeviceContext_OMSetBlendState(g_ctx,g_bsNone,bf,0xFFFFFFFFu); ID3D11DeviceContext_OMSetDepthStencilState(g_ctx,g_dsMark,0);
                ID3D11DeviceContext_Draw(g_ctx,(UINT)((e-k)*3),(UINT)(mvstart+k*3));
                gpu_quad(g_dsAcc,3,1,x0,y0,x1,y1,0,g_bsNone,0);     /* acc |= paridade */
                gpu_quad(g_dsClr,0,1,x0,y0,x1,y1,0,g_bsNone,0);     /* zera a paridade */
            }
            k=e; }
        /* aplica: pixels com acc e receptores */
        { ID3D11RenderTargetView *rt[1]={g_rtv0}; ID3D11DeviceContext_OMSetRenderTargets(g_ctx,1,rt,g_dsv); }
        ID3D11DeviceContext_PSSetShaderResources(g_ctx,1,1,&g_srv1);
        gpu_quad(g_dsFin,2,0,0,0,RW,RH,g_psf,g_bsShadow,1);
        { ID3D11ShaderResourceView *nul=0; ID3D11DeviceContext_PSSetShaderResources(g_ctx,1,1,&nul); }
        { ID3D11RenderTargetView *rt[2]={g_rtv0,g_rtv1}; ID3D11DeviceContext_OMSetRenderTargets(g_ctx,2,rt,g_dsv); }
        { UINT stride=sizeof(GV),off=0; ID3D11DeviceContext_IASetVertexBuffers(g_ctx,0,1,&g_vb,&stride,&off); ID3D11DeviceContext_IASetInputLayout(g_ctx,g_lay); ID3D11DeviceContext_VSSetShader(g_ctx,g_vs,0,0); ID3D11DeviceContext_PSSetShader(g_ctx,g_ps,0,0); ID3D11DeviceContext_RSSetState(g_ctx,g_rs); }
    }
    gpu_draw_batches(2);
    (void)vstart; (void)nmesh;
    if(g_direct>0){ g_nbg=0; { static int first=1; if(first){ first=0; glog("primeiro quadro desenhado na GPU em modo de apresentacao direta (%dx%d)\n",RW,RH); } } return 1; }   /* v15zzze */
    /* devolve o quadro */
    ID3D11DeviceContext_CopyResource(g_ctx,(ID3D11Resource*)g_stg,(ID3D11Resource*)g_rt0);
    { D3D11_MAPPED_SUBRESOURCE ms; if(FAILED(ID3D11DeviceContext_Map(g_ctx,(ID3D11Resource*)g_stg,0,D3D11_MAP_READ,0,&ms))){ g_dead=1; glog("ERRO Map staging\n"); g_nbg=0; return 0; }
        for(int y=0;y<RH;y++) memcpy(fb+(size_t)y*RW,(const uint8_t*)ms.pData+(size_t)y*ms.RowPitch,(size_t)RW*4); ID3D11DeviceContext_Unmap(g_ctx,(ID3D11Resource*)g_stg,0); }
    g_nbg=0;
    { static int first=1; if(first){ first=0; glog("primeiro quadro desenhado na GPU (%dx%d, %zu vertices, %zu lotes, %d malhas de sombra)\n",RW,RH,g_gvn,g_gbn,nmesh); } }
    return 1; }

/* ===== v15zzze: apresentacao direta =====
   Antes: GPU -> leitura de volta para a CPU (33 MB em 4K) -> antialiasing/brilho na CPU -> copia -> GDI.
   Agora: o quadro fica na GPU; um passe de pos-processamento (BASIC / FXAA / reducao SSAA + brilho, mesmas formulas da CPU) e um passe
   de escala (barras pretas conforme o aspecto) desenham direto numa swap chain da janela. Qualquer falha desliga o modo e volta ao
   caminho antigo. Desligar na mao: variavel CT2_GPUDIRECT=0 ou arquivo vazio "no_direct.txt" ao lado do exe. */
extern void *win_hwnd(void);
static const char *g_hlsl_post =
"cbuffer P:register(b0){ uint mode; uint rw; uint rh; uint ow; uint oh; float gain; float2 pad; };\n"
"Texture2D tx:register(t0); SamplerState sm:register(s0);\n"
"struct VP{ float4 p:SV_Position; float2 uv:TEXCOORD0; };\n"
"VP vsp(uint id:SV_VertexID){ VP o; float2 t=float2((id<<1u)&2u,id&2u); o.p=float4(t*float2(2.0,-2.0)+float2(-1.0,1.0),0.0,1.0); o.uv=t; return o; }\n"
"float3 ld(int2 q){ q=clamp(q,int2(0,0),int2((int)rw-1,(int)rh-1)); return tx.Load(int3(q,0)).rgb; }\n"
"float lm(float3 c){ return dot(c,float3(0.30078125,0.5859375,0.11328125))*255.0; }\n"
"float4 psp(VP i):SV_Target{\n"
"  int2 q=int2(i.p.xy); float3 o;\n"
"  if(mode>=3u){ uint x=(uint)q.x; uint y=(uint)q.y; uint sx0=x*rw/ow; uint sx1=(x+1u)*rw/ow; uint sy0=y*rh/oh; uint sy1=(y+1u)*rh/oh;\n"
"    if(sx1<=sx0) sx1=sx0+1u; if(sx1>rw) sx1=rw; if(sy1<=sy0) sy1=sy0+1u; if(sy1>rh) sy1=rh;\n"
"    float3 acc=float3(0.0,0.0,0.0); uint cnt=0u;\n"
"    [loop] for(uint yy=sy0;yy<sy1;yy++){ [loop] for(uint xx=sx0;xx<sx1;xx++){ acc+=tx.Load(int3((int)xx,(int)yy,0)).rgb; cnt++; } }\n"
"    o=acc/(float)max(cnt,1u); }\n"
"  else { float3 c=ld(q); o=c;\n"
"    if(mode!=0u){ float3 n=ld(q+int2(0,-1)); float3 s=ld(q+int2(0,1)); float3 w=ld(q+int2(-1,0)); float3 e=ld(q+int2(1,0));\n"
"      float lc=lm(c); float ln=lm(n); float ls=lm(s); float lw=lm(w); float le=lm(e);\n"
"      float mx=max(max(max(lc,ln),max(ls,lw)),le); float mn=min(min(min(lc,ln),min(ls,lw)),le); float rg=mx-mn;\n"
"      if(mode==1u){ if(rg>=24.0) o=(c*4.0+n+s+w+e)*0.125; }\n"
"      else { float thr=max(mx*0.125,8.0);\n"
"        if(rg>=thr){ float3 nw=ld(q+int2(-1,-1)); float3 ne=ld(q+int2(1,-1)); float3 sw=ld(q+int2(-1,1)); float3 se=ld(q+int2(1,1));\n"
"          float3 av=(n+s+w+e+nw+ne+sw+se)*0.125; float avl=(ln+ls+lw+le)*0.25; float d=abs(avl-lc); float bl=min(d/max(rg,1.0),1.0);\n"
"          bl=bl*0.75+(bl>0.0?0.125:0.0); bl=min(bl,1.0); o=lerp(c,av,bl); } } } }\n"
"  return float4(saturate(o*gain),1.0); }\n"
"float4 pspr(VP i):SV_Target{ return float4(tx.Sample(sm,i.uv).rgb,1.0); }\n";

static ID3DBlob *g_cmpblob2(const char*src,const char*ent,const char*prof){
    PFN_D3DCOMPILE_ fn=(PFN_D3DCOMPILE_)GetProcAddress(g_cmp,"D3DCompile"); if(!fn) return 0;
    ID3DBlob *code=0,*err=0; HRESULT hr=fn(src,strlen(src),"ct2post.hlsl",0,0,ent,prof,0,0,&code,&err);
    if(FAILED(hr)){ glog("shader %s/%s falhou: %s\n",ent,prof,err?(const char*)err->lpVtbl->GetBufferPointer(err):"?"); if(err) err->lpVtbl->Release(err); return 0; }
    if(err) err->lpVtbl->Release(err); return code; }

static int gpu_post_init(void){
    if(g_vsp) return 1; if(!g_cmp||!g_dev) return 0;
    ID3DBlob *bv=g_cmpblob2(g_hlsl_post,"vsp","vs_4_0"),*bp=g_cmpblob2(g_hlsl_post,"psp","ps_4_0"),*br=g_cmpblob2(g_hlsl_post,"pspr","ps_4_0"); if(!bv||!bp||!br) return 0;
    GCK(ID3D11Device_CreateVertexShader(g_dev,bv->lpVtbl->GetBufferPointer(bv),bv->lpVtbl->GetBufferSize(bv),0,&g_vsp),"CreateVertexShader vsp");
    GCK(ID3D11Device_CreatePixelShader(g_dev,bp->lpVtbl->GetBufferPointer(bp),bp->lpVtbl->GetBufferSize(bp),0,&g_psp),"CreatePixelShader psp");
    GCK(ID3D11Device_CreatePixelShader(g_dev,br->lpVtbl->GetBufferPointer(br),br->lpVtbl->GetBufferSize(br),0,&g_pspr),"CreatePixelShader pspr");
    { D3D11_BUFFER_DESC bd; memset(&bd,0,sizeof bd); bd.ByteWidth=32; bd.Usage=D3D11_USAGE_DEFAULT; bd.BindFlags=D3D11_BIND_CONSTANT_BUFFER; GCK(ID3D11Device_CreateBuffer(g_dev,&bd,0,&g_cbp),"CreateBuffer cbp"); }
    { D3D11_BLEND_DESC b; memset(&b,0,sizeof b); b.RenderTarget[0].BlendEnable=FALSE; b.RenderTarget[0].SrcBlend=D3D11_BLEND_ONE; b.RenderTarget[0].DestBlend=D3D11_BLEND_ZERO; b.RenderTarget[0].BlendOp=D3D11_BLEND_OP_ADD;
      b.RenderTarget[0].SrcBlendAlpha=D3D11_BLEND_ONE; b.RenderTarget[0].DestBlendAlpha=D3D11_BLEND_ZERO; b.RenderTarget[0].BlendOpAlpha=D3D11_BLEND_OP_ADD; b.RenderTarget[0].RenderTargetWriteMask=D3D11_COLOR_WRITE_ENABLE_ALL;
      GCK(ID3D11Device_CreateBlendState(g_dev,&b,&g_bsP),"blend post"); }
    { D3D11_SAMPLER_DESC d; memset(&d,0,sizeof d); d.Filter=D3D11_FILTER_MIN_MAG_MIP_LINEAR; d.AddressU=D3D11_TEXTURE_ADDRESS_CLAMP; d.AddressV=D3D11_TEXTURE_ADDRESS_CLAMP; d.AddressW=D3D11_TEXTURE_ADDRESS_CLAMP; d.ComparisonFunc=D3D11_COMPARISON_NEVER; d.MaxLOD=D3D11_FLOAT32_MAX; d.MaxAnisotropy=1;
      GCK(ID3D11Device_CreateSamplerState(g_dev,&d,&g_smpL),"sampler linear"); }
    return 1; }

static int gpu_sc_create(HWND h,int w,int ht){
    static const GUID iidD={0x54ec77fa,0x1377,0x44e6,{0x8c,0x32,0x88,0xfd,0x5f,0x44,0xc8,0x4c}};
    static const GUID iidF={0x7b7166ec,0x21c7,0x44ae,{0xb2,0x1a,0xc9,0xae,0x32,0x1a,0xe3,0x69}};
    IDXGIDevice *dd=0; IDXGIAdapter *ad=0; IDXGIFactory *fa=0; int ok=0;
    if(SUCCEEDED(((IUnknown*)g_dev)->lpVtbl->QueryInterface((IUnknown*)g_dev,&iidD,(void**)&dd))&&dd){
        if(SUCCEEDED(dd->lpVtbl->GetAdapter(dd,&ad))&&ad){
            if(SUCCEEDED(ad->lpVtbl->GetParent(ad,&iidF,(void**)&fa))&&fa){
                DXGI_SWAP_CHAIN_DESC sd; memset(&sd,0,sizeof sd); sd.BufferDesc.Width=(UINT)w; sd.BufferDesc.Height=(UINT)ht; sd.BufferDesc.Format=DXGI_FORMAT_B8G8R8A8_UNORM;
                sd.SampleDesc.Count=1; sd.BufferUsage=DXGI_USAGE_RENDER_TARGET_OUTPUT; sd.BufferCount=1; sd.OutputWindow=h; sd.Windowed=TRUE; sd.SwapEffect=DXGI_SWAP_EFFECT_DISCARD;
                if(SUCCEEDED(fa->lpVtbl->CreateSwapChain(fa,(IUnknown*)g_dev,&sd,&g_sc))&&g_sc){ fa->lpVtbl->MakeWindowAssociation(fa,h,3u); ok=1; } } } }
    if(fa) ((IUnknown*)fa)->lpVtbl->Release((IUnknown*)fa); if(ad) ((IUnknown*)ad)->lpVtbl->Release((IUnknown*)ad); if(dd) ((IUnknown*)dd)->lpVtbl->Release((IUnknown*)dd);
    if(ok){ g_swW=w; g_swH=ht; } return ok; }

static void gpu_direct_off(void){
    if(!g_sc&&g_direct!=1) return;
    ID3D11DeviceContext_ClearState(g_ctx); GREL(g_bbrtv); GREL(g_sc); g_swW=g_swH=0; g_psrc=0; g_lastok=0; if(g_direct>0) g_direct=0; }

static int gpu_direct_ok(void){
    if(g_direct) return g_direct>0;
    { const char*e=GENV("CT2_GPUDIRECT"); if(e&&e[0]=='0'){ g_direct=-1; return 0; } }
    { FILE*nf=fopen("no_direct.txt","rb"); if(nf){ fclose(nf); g_direct=-1; glog("apresentacao direta desligada por no_direct.txt\n"); return 0; } }
    if(!g_dev||!g_cmp) return 0;
    HWND h=(HWND)win_hwnd(); if(!h) return 0;               /* ainda sem janela: tenta no proximo quadro */
    RECT rc; GetClientRect(h,&rc); if(rc.right<=0||rc.bottom<=0) return 0;   /* minimizada: tenta depois */
    if(!gpu_targets()||!g_srv0){ g_direct=-1; glog("apresentacao direta indisponivel (alvos)\n"); return 0; }
    if(!gpu_post_init()){ g_direct=-1; glog("apresentacao direta indisponivel (shaders de pos-processamento)\n"); return 0; }
    if(!gpu_sc_create(h,rc.right,rc.bottom)){ g_direct=-1; glog("apresentacao direta indisponivel (swap chain)\n"); return 0; }
    g_direct=1; glog("apresentacao direta LIGADA (swap chain %dx%d)\n",rc.right,rc.bottom); return 1; }

int ta_direct_active(void){ return g_direct>0&&g_sc!=0; }

/* copia o quadro bruto (antes do AA) para a GPU: fundo do menu de pausa */
static void gpu_snap_capture(void){
    if(!g_rt0||!g_dev) return;
    if(!g_lastT||g_lastw!=RW||g_lasth!=RH){ GREL(g_lastSRV); GREL(g_lastT); g_lastok=0;
        D3D11_TEXTURE2D_DESC td; memset(&td,0,sizeof td); td.Width=RW; td.Height=RH; td.MipLevels=1; td.ArraySize=1; td.Format=DXGI_FORMAT_B8G8R8A8_UNORM; td.SampleDesc.Count=1; td.Usage=D3D11_USAGE_DEFAULT; td.BindFlags=D3D11_BIND_SHADER_RESOURCE;
        if(FAILED(ID3D11Device_CreateTexture2D(g_dev,&td,0,&g_lastT))||FAILED(ID3D11Device_CreateShaderResourceView(g_dev,(ID3D11Resource*)g_lastT,0,&g_lastSRV))){ GREL(g_lastSRV); GREL(g_lastT); return; }
        g_lastw=RW; g_lasth=RH; }
    ID3D11DeviceContext_CopyResource(g_ctx,(ID3D11Resource*)g_lastT,(ID3D11Resource*)g_rt0); g_lastok=1; }

static void gpu_post_set(int m,int rw,int rh,int ow,int oh,float g){ g_pm=m; g_prw=rw; g_prh=rh; g_pow=ow; g_poh=oh; g_pgain=g; }

static void gpu_fs_pass(ID3D11RenderTargetView *rtv,float x,float y,float w,float h,ID3D11PixelShader *ps,ID3D11ShaderResourceView *srv,int linear){
    D3D11_VIEWPORT vp; vp.TopLeftX=x; vp.TopLeftY=y; vp.Width=w; vp.Height=h; vp.MinDepth=0.f; vp.MaxDepth=1.f; float bf[4]={0,0,0,0};
    ID3D11DeviceContext_OMSetRenderTargets(g_ctx,1,&rtv,0); ID3D11DeviceContext_RSSetViewports(g_ctx,1,&vp); ID3D11DeviceContext_RSSetState(g_ctx,g_rs);
    ID3D11DeviceContext_OMSetBlendState(g_ctx,g_bsP,bf,0xFFFFFFFFu); ID3D11DeviceContext_OMSetDepthStencilState(g_ctx,g_dsOff,0);
    ID3D11DeviceContext_IASetInputLayout(g_ctx,0); ID3D11DeviceContext_IASetPrimitiveTopology(g_ctx,D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    ID3D11DeviceContext_VSSetShader(g_ctx,g_vsp,0,0); ID3D11DeviceContext_PSSetShader(g_ctx,ps,0,0); ID3D11DeviceContext_PSSetConstantBuffers(g_ctx,0,1,&g_cbp);
    if(linear) ID3D11DeviceContext_PSSetSamplers(g_ctx,0,1,&g_smpL);
    ID3D11DeviceContext_PSSetShaderResources(g_ctx,0,1,&srv); ID3D11DeviceContext_Draw(g_ctx,3,0);
    { ID3D11ShaderResourceView *nv=0; ID3D11DeviceContext_PSSetShaderResources(g_ctx,0,1,&nv); } }

/* redo=0: quadro novo (pos-processa e mostra); redo=1: so mostra de novo o ultimo (WM_PAINT). Retorna 0 se o modo caiu (o chamador usa o caminho antigo). */
int ta_gpu_present(int redo){
    if(g_direct<=0||!g_sc||!g_dev) return 0;
    HWND h=(HWND)win_hwnd(); if(!h) return 0; RECT rc; GetClientRect(h,&rc); int cw=rc.right,ch=rc.bottom; if(cw<=0||ch<=0) return 1;
    if(cw!=g_swW||ch!=g_swH){ ID3D11DeviceContext_ClearState(g_ctx); GREL(g_bbrtv);
        if(FAILED(g_sc->lpVtbl->ResizeBuffers(g_sc,0,(UINT)cw,(UINT)ch,DXGI_FORMAT_UNKNOWN,0))){ glog("ERRO ResizeBuffers (apresentacao direta desligada)\n"); g_direct=-1; return 0; }
        g_swW=cw; g_swH=ch; }
    if(!g_bbrtv){ static const GUID iidT={0x6f15aaf2,0xd208,0x4e89,{0x9a,0xb4,0x48,0x95,0x35,0xd3,0x4f,0x9c}}; ID3D11Texture2D *bb=0;
        if(FAILED(g_sc->lpVtbl->GetBuffer(g_sc,0,&iidT,(void**)&bb))||!bb){ glog("ERRO GetBuffer\n"); g_direct=-1; return 0; }
        HRESULT hr=ID3D11Device_CreateRenderTargetView(g_dev,(ID3D11Resource*)bb,0,&g_bbrtv); GREL(bb); if(FAILED(hr)){ glog("ERRO rtv do backbuffer\n"); g_direct=-1; return 0; } }
    if(!redo){
        int need=(g_pm!=0)||(g_pgain<0.999f)||(g_pgain>1.001f)||g_pow!=g_prw||g_poh!=g_prh;
        if(!g_srv0){ g_direct=-1; return 0; }
        if(need){
            if(!g_rt2||g_rt2w!=g_pow||g_rt2h!=g_poh){ GREL(g_srv2); GREL(g_rtv2); GREL(g_rt2); D3D11_TEXTURE2D_DESC td; memset(&td,0,sizeof td); td.Width=g_pow; td.Height=g_poh; td.MipLevels=1; td.ArraySize=1; td.Format=DXGI_FORMAT_B8G8R8A8_UNORM; td.SampleDesc.Count=1; td.Usage=D3D11_USAGE_DEFAULT; td.BindFlags=D3D11_BIND_RENDER_TARGET|D3D11_BIND_SHADER_RESOURCE;
                if(FAILED(ID3D11Device_CreateTexture2D(g_dev,&td,0,&g_rt2))||FAILED(ID3D11Device_CreateRenderTargetView(g_dev,(ID3D11Resource*)g_rt2,0,&g_rtv2))||FAILED(ID3D11Device_CreateShaderResourceView(g_dev,(ID3D11Resource*)g_rt2,0,&g_srv2))){ glog("ERRO alvo de pos-processamento\n"); GREL(g_srv2); GREL(g_rtv2); GREL(g_rt2); g_direct=-1; return 0; }
                g_rt2w=g_pow; g_rt2h=g_poh; }
            struct { unsigned int mode,rw,rh,ow,oh; float gain,p0,p1; } pc; pc.mode=(unsigned)g_pm; pc.rw=(unsigned)g_prw; pc.rh=(unsigned)g_prh; pc.ow=(unsigned)g_pow; pc.oh=(unsigned)g_poh; pc.gain=g_pgain; pc.p0=pc.p1=0.f;
            ID3D11DeviceContext_ClearState(g_ctx); ID3D11DeviceContext_UpdateSubresource(g_ctx,(ID3D11Resource*)g_cbp,0,0,&pc,0,0);
            gpu_fs_pass(g_rtv2,0.f,0.f,(float)g_pow,(float)g_poh,g_psp,g_srv0,0); g_psrc=g_srv2; }
        else g_psrc=g_srv0; }
    if(!g_psrc) return 1;
    { int dx=0,dy=0,dw=cw,dh=ch; if(ta_aspect!=0&&g_poh>0){ float ca=(float)g_pow/(float)g_poh; if((float)dw/(float)dh>ca){ int nw=(int)(dh*ca+.5f); dx=(dw-nw)/2; dw=nw; } else { int nh=(int)(dw/ca+.5f); dy=(dh-nh)/2; dh=nh; } }
      float blk[4]={0,0,0,1}; ID3D11DeviceContext_ClearState(g_ctx); ID3D11DeviceContext_ClearRenderTargetView(g_ctx,g_bbrtv,blk);
      gpu_fs_pass(g_bbrtv,(float)dx,(float)dy,(float)dw,(float)dh,g_pspr,g_psrc,1); }
    { HRESULT hr=g_sc->lpVtbl->Present(g_sc,0,0); if(hr==(HRESULT)0x887A0005||hr==(HRESULT)0x887A0007){ glog("dispositivo perdido ao apresentar (voltando ao software)\n"); g_dead=1; g_direct=-1; return 0; } }
    return 1; }
#endif
