#ifndef CT2_TA_H
#define CT2_TA_H
#include <stdint.h>
#include <stddef.h>
extern void (*ta_eol_cb)(int list);
void ta_reset(void); void ta_feed(const uint8_t *p,size_t n);
const uint32_t *ta_render_frame(const uint8_t *vram,const uint32_t *pal,uint32_t palctl,unsigned *ntris_out);   /* ARGB 640x480, ou NULL se a lista estiver vazia */
int ta_render(const char *path,const uint8_t *vram,const uint32_t *pal,uint32_t palctl,unsigned *ntris_out);
void menux_filter(uint16_t *pb,uint8_t *rt,uint8_t *lt);   /* entrada dos menus nativos (CHEATS / PLAY AS YOU LIKE / TYPE C) */
#endif

void ta_writeback(uint8_t *vram,const uint32_t *fb,uint32_t sof,uint32_t stride8,uint32_t ctrl);
void ta_writeback_cur(uint8_t *vram,uint32_t sof,uint32_t stride8,uint32_t ctrl);
