/* mainloop_bgm.h
 *
 * Trilha sonora de fundo do menu (browser de ROMs e menu de pausa).
 *
 * Toca modulos de tracker .mod (Amiga ProTracker) e .xm (FastTracker II)
 * via os single-header players jar_mod / jar_xm (em src/third_party/jar).
 * O PCM e' gerado na CPU (EE) a cada frame de menu e empurrado para o
 * audsrv pela API Aud_* (48000 Hz / 16-bit / stereo).
 *
 * Uso:
 *   - BgmUpdate() e' chamado a cada frame enquanto o menu esta visivel
 *     (MainLoopRender, bloco `if (_bMenu)`).  Ele faz lazy-load da
 *     primeira faixa achada e alimenta o audsrv com o que couber.
 *   - BgmStop() para a reproducao ao iniciar uma ROM.
 *
 * Os arquivos sao procurados em BGM_PATH (define do Makefile, espelha
 * COVERS_PATH) e em algumas pastas padrao (ver mainloop_bgm.cpp).
 */

#ifndef _MAINLOOP_BGM_H
#define _MAINLOOP_BGM_H

#include "types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Alimenta o audsrv com PCM da faixa atual.  Faz lazy-load na 1a
   chamada.  Seguro chamar todo frame; nao faz nada se desabilitado,
   se nao houver faixa, ou se o audio ainda nao esta pronto. */
void BgmUpdate(void);

/* Para a reproducao (chamado ao lancar uma ROM).  NAO libera o decoder:
   mantem a faixa carregada para reabrir o menu sem reler do disco. */
void BgmStop(void);

/* Volume da trilha de menu: 0 = OFF (libera o decoder, nao carrega nem
   consome RAM), 1..100 = liga e toca nesse volume.  Vale para SNES e NES
   (a trilha do menu e' compartilhada). */
void BgmSetVolume(int vol);
int  BgmGetVolume(void);

/* Frequencia de sintese (Hz).  A saida e' sempre 48 kHz (reamostrada).
   BgmCycleRate(+1/-1) percorre a lista de frequencias oferecidas. */
int  BgmGetRate(void);
void BgmSetRate(int hz);
void BgmCycleRate(int dir);

#ifdef __cplusplus
}
#endif

#endif /* _MAINLOOP_BGM_H */
