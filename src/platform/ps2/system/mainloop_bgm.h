/*
 * Copyright (c) 1997-2004-2022 Icer Addis
 * Re-Worked By ReyFxck, Claude Aí, ChatGPT
 *
 * Description:
 *   Declares the mainloop bgm interface for the PlayStation 2 application runtime.
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

/* Mark menu entry without doing discovery or file I/O. This lets L2+R2 show
   its first frame immediately; normal BgmUpdate performs lazy loading. */
void BgmMenuEnter(void);

/* Para a reproducao (chamado ao lancar uma ROM).  NAO libera o decoder:
   mantem a faixa carregada para reabrir o menu sem reler do disco. */
void BgmStop(void);

/* Avanca explicitamente para a proxima faixa. So' troca se houver 2+
   faixas; a retomada normal do menu preserva o decoder e nao chama isto. */
void BgmNext(void);

/* Scope synchronous UI/file operations. While at least one scope is active,
   a small EE helper keeps an already-loaded tracker feeding audsrv without
   touching the filesystem. Calls may be nested. */
void BgmIOBegin(void);
void BgmIOEnd(void);

/* Volume da trilha de menu: 0 = OFF (libera o decoder, nao carrega nem
   consome RAM), 1..100 = liga e toca nesse volume.  Vale para SNES e NES
   (a trilha do menu e' compartilhada). */
void BgmSetVolume(int vol);
int  BgmGetVolume(void);

/* Numero de faixas .mod/.xm achadas. Dispositivos locais sao escaneados
   imediatamente; o CD/DVD e' acrescentado depois de uma sondagem segura. */
int  BgmTrackCount(void);
int  BgmIsSearching(void);

/* Frequencia de sintese (Hz).  A saida e' sempre 48 kHz (reamostrada).
   BgmCycleRate(+1/-1) percorre a lista de frequencias oferecidas. */
int  BgmGetRate(void);
void BgmSetRate(int hz);
void BgmCycleRate(int dir);

#ifdef __cplusplus
}
#endif

#endif /* _MAINLOOP_BGM_H */
