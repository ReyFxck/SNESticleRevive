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

/* Para a reproducao e libera o decoder (chamado ao lancar uma ROM). */
void BgmStop(void);

/* Liga/desliga a trilha (persistencia fica a cargo do Video Config). */
void BgmSetEnabled(Bool bEnable);
Bool BgmIsEnabled(void);

#ifdef __cplusplus
}
#endif

#endif /* _MAINLOOP_BGM_H */
