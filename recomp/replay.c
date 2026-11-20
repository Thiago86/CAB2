/* replay.c - reproduz um quadro salvo (glitch_dump.bin, tecla F12) sem o jogo: gcc -O1 -w -I. replay.c ta.c -o replay -lm -lpthread */
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include "ta.h"
int ct2_quit_req=0; int ct2_free_mute=0; void aica_voz_nota(const char*t){} void aica_aviso(unsigned f){}
int ct2_cheat_unlock=0, ct2_cheat_notimer=0; int ct2_p3_on=0; unsigned short ct2_p3_btn=0xFFFF; unsigned char ct2_p3_lt=0, ct2_p3_rt=0;
extern unsigned ta_cur_frame; extern uint32_t ta_reg_backgnd_t,ta_reg_param_base,ta_reg_backgnd_d;
int main(int argc,char**argv){
    FILE*f=fopen(argc>1?argv[1]:"glitch_dump.bin","rb"); if(!f){ perror("dump"); return 1; }
    uint32_t h[8]; if(fread(h,4,8,f)!=8||h[0]!=0x32544344u){ fprintf(stderr,"arquivo invalido\n"); return 1; }
    uint8_t*buf=malloc(h[5]); uint8_t*vram=malloc(16u<<20); uint32_t*pal=malloc(4096);
    if(fread(buf,1,h[5],f)!=h[5]||fread(vram,1,16u<<20,f)!=(16u<<20)||fread(pal,4,1024,f)!=1024){ fprintf(stderr,"truncado\n"); return 1; }
    ta_cur_frame=h[6]; ta_reg_backgnd_t=h[1]; ta_reg_param_base=h[2]; ta_reg_backgnd_d=h[3];
    { extern int ta_upscale,ta_aspect,ta_res_i; if(getenv("CT2_RUP")) ta_upscale=atoi(getenv("CT2_RUP")); if(getenv("CT2_RASP")) ta_aspect=atoi(getenv("CT2_RASP")); if(getenv("CT2_RRES")) ta_res_i=atoi(getenv("CT2_RRES")); }
    ta_reset(); ta_feed(buf,h[5]); unsigned nt=0;
    int ok=ta_render(argc>2?argv[2]:"replay.ppm",vram,pal,h[4],&nt); printf("render %s: %u triangulos\n",ok?"ok":"FALHOU",nt); return 0; }
