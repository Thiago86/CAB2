#ifndef CT2_WIN_H
#define CT2_WIN_H
#include <stdint.h>
int  win_present(const uint32_t *argb640x480);   /* mostra um quadro; 0 se a janela foi fechada */
int  win_pump(void);                              /* processa mensagens; 0 se fechada */
void win_throttle(void);                          /* limita a ~60 quadros/s */
int win_hotkeys(void);
void win_status(const char*s);
void win_pad(uint16_t *buttons,uint8_t *rt,uint8_t *lt,uint8_t *jx,uint8_t *jy);   /* teclado/XInput -> estado do controle do DC */
void win_audio_start(void (*render)(int16_t *stereo,int nsamples));   /* thread de audio (waveOut, 44100 Hz estereo 16 bits) */
#endif
