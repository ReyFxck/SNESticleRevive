# Round 7 — DMA, PPU, Mode 7 e áudio

Base: ZIP SNESticleRevive-main (3).zip, commit abac0ade6f44e635d01762f22b41b69e4a946528. Patch experimental, sem alterações específicas de ROM.

## Alterações

- MDMA de destino fixo nos modos 2/6: reaproveita o caminho OAM/CGRAM do modo 0 somente fora da exibição ativa. O caminho ativo, os ciclos e o open bus permanecem intactos. Teste compara os modos 0/2/6, portas 04/22, endereços e linhas diferentes com a referência byte a byte.
- PPU: evita sincronizações sem linha pendente e sem escritas na fila. A ordem de escritas, inclusive na mesma linha e durante VBlank, permanece a mesma. O contador `snes-sync ppu calls` agora conta chamadas efetivamente realizadas em SyncPPU, não solicitações redundantes; considerar isso ao comparar logs.
- PPU: verifica acesso VRAM apenas para as quatro portas que precisam dessa informação. CGRAM continua usando H/V original.
- Mode 7 clamp/black: combina os testes de coordenadas 24.8 e reaproveita os campos de endereço e cache de tile da variante repeat. Não muda os coeficientes ou arredondamentos. O caminho repeat existente permanece intacto.
- RFAuds2: capacidade de admissão de 2048 para 3072 frames (43 para 64 ms nominais); prebuffer de 1536 para 2560 frames. Aumenta a tolerância a atrasos curtos, ao custo de até cerca de 21 ms adicionais de fila nominal. Não altera BRR, interpolação, PCM ou frequência do DSP. Não corrige underruns devidos a lentidão sustentada. O teste do adaptador foi atualizado para a nova capacidade e para a API shutdown já usada pelo código original.
- Log: `[ps2-audio]` a cada janela diagnóstica, com ocupação EE/IOP, underruns, frames silenciosos, backpressure e missed-refills. Contadores acumulados e valores da última resposta RPC; não são medições instantâneas nem deltas da janela.

## Referências técnicas

- https://snes.nesdev.org/wiki/DMA — padrões de transferência.
- https://snes.nesdev.org/wiki/Reading_and_writing_PPU_memory — OAM/CGRAM.
- https://snes.nesdev.org/wiki/PPU_registers — restrições de acesso CGRAM.
- https://snes.nesdev.org/wiki/S-DSP_registers — interpolação gaussiana e taxa nominal.
- Implementação local do RFAuds2: `src/third_party/rfauds2/src/iop/src/main.c`, arredondamento em blocos de 512 frames; não foi alterado o IRX.

## Validação realizada

- `tools/pputest/build.sh` e 22 executáveis da suíte: PASS (inclui testes novos OAM/CGRAM e sincronização).
- `tools/pputest/test_hotpath.sh`: PASS (antes da alteração final de Mode 7; Mode 7 foi testado posteriormente pela suíte completa).
- `tools/rfauds2test/build.sh`: PASS.
- `tools/romlab/build.sh`: compilação portátil PASS; sem ROM de jogo executada.
- `git diff --check`: PASS.

Não há PS2DEV/NetherSX2 disponível neste ambiente. Não há ELF compilado, medição de FPS, teste auditivo, benchmark R5900/MMI ou validação em console físico. Não foi alterada a main remota. Não foi feita correção completa do scheduler, GS/GIF, CPU/SPC700, DSP-4, HostFS, VU0/VU1 ou todos os modos PPU. O resampler e mixer passaram os testes existentes, mas isso não confirma ausência de granulado no PS2.

## Teste no Debian/Termux

Extraia o ZIP sobre a raiz da árvore (faça backup ou commit primeiro). Compile com:

```
make fast JOBS=2 SNES_DIAGNOSTICS=1 SNES_TARGET_PROFILE=0 AUDIO_BACKEND=rfauds2
```

O alvo é `build/SNESticle.elf`; preserve uma cópia com nome distinto antes de alternar configurações. Para release use `SNES_DIAGNOSTICS=0 SNES_TARGET_PROFILE=0`. Para comparação direta com diag2 anterior use `SNES_DIAGNOSTICS=2 SNES_TARGET_PROFILE=0`. Faça teste A/B com mesma configuração, mesma sequência de inputs e mesmas cenas (intro Mode 7, corrida Mode 1). Compare `[snes-frame]`, `[ps2-cadence]`, `[ps2-late-work]`, `[snes-oam-block]`, `[ps2-audio]`, hashes e imagens. Registre se o granulado acontece mesmo com cadência de 60 Hz e sem novos underruns. Testar Top Gear 2/3000 e jogos de referência; o DSP-4 não foi alterado.
