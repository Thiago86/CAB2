#ifndef AICA_H
#define AICA_H
#include <stdint.h>
void aica_init(uint8_t *ram2mb);
uint32_t aica_read(uint32_t off,int size);          /* off = endereco - 0x00700000 */
void aica_write(uint32_t off,uint32_t v,int size);
void aica_render(int16_t *out,int n);                /* n amostras estereo (44,1 kHz), 2*n int16 */
int  aica_sh4_irq(void);
int  aica_arm_running(void);
void aica_stats(void);
#endif
