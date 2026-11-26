# Round 8 — HDMA/CGRAM, grupos CLUT e precisão BRR

Patch incremental sobre a Round 7, não sobre a base original. Não depende de CRC/nome de ROM. Não modifica configuração de CI nem a fila de áudio da Round 7.

## Alterações

- `sndma.cpp`: especialização A->B para HDMA modos 2/6 na porta $2122. Evita a tentativa de fila que seria sempre rejeitada. Mantém o caminho de leitura A-bus (inclusive endereços proibidos), a escrita genérica CPU/B-bus, a ordem e os oito clocks emulados por byte. Endereços diretos/indiretos continuam com wrap de 16 bits; os outros modos permanecem no caminho existente.
- `snes.cpp`, `snppu.cpp`, `snppu.h`: despacha CGDATA diretamente para `WriteCGRAMTimed`, sem passar pelo switch genérico. Mantém a barreira da fila PPU, relógio H/V ao vivo, latch, redirecionamento durante exibição e contador de endereço. Não altera o modelo temporal nem promete corrigir todos os efeitos raster.
- `snppublend_gs.cpp`, `.h`, `snppupalettedirty.h`: mantém a projeção dos bits de paleta suja em dois words de 32 bits, atualizada junto com o bitmap de entradas. Evita reexaminar as entradas a cada consulta de grupos GIF. Invalidação completa e limpeza continuam sincronizadas. Não modifica conteúdo de CLUT, DMA chains, waits ou limite de upload parcial. O ganho em ciclos ainda precisa ser medido.
- `snspcbrr.c`: corrige a aritmética dos filtros BRR 1/2/3 para os arredondamentos intermediários do S-DSP em vez de multiplicações aproximadas. Preserva expansão dos nibbles, ranges inválidos, saturação de 16 bits, wrap após dobrar, flags e buffers sobrepostos. O filtro 0 não muda. Pode alterar PCM de instrumentos que usem preditores; não confirma causa do áudio granulado. Não altera echo, FIR, interpolação, scheduler nem backend.

## Referências

- https://snes.nesdev.org/wiki/DMA — padrões de HDMA e modos 2/6.
- https://snes.nesdev.org/wiki/Reading_and_writing_PPU_memory — transferências CGRAM.
- https://www.sneslab.net/wiki/BRR — expansão, filtros e wrap de BRR.
- https://github.com/snes9xgit/snes9x/blob/master/apu/bapu/dsp/SPC_DSP.cpp — referência comportamental para a ordem e arredondamento do preditor; implementação local adaptada à interface existente.

## Testes executados no host

- `bash tools/pputest/test_spc_brr_hotpath.sh`: 18.432 casos SPC, 3.002 eventos I/O, 131.072 blocos BRR e 131.072 blocos sobrepostos; PASS. Referência de BRR usa divisão inteira com floor explícito, não o helper do decoder.
- `bash tools/pputest/test_mixer_feedback.sh`: seis hashes PCM/estado existentes e readback de echo desabilitado para escrita; PASS. Não prova equivalência de todos os jogos ou do DSP inteiro.
- `bash tools/pputest/test_hotpath.sh`: três configurações dos testes BG/CHR/máscaras, 1.048.576 atualizações de grupos por configuração, 480 casos HDMA CGRAM (fixture de porta B, clocks, acessos proibidos, wrap, modo direto/indireto), 4096 casos OAM, 528 prefixos VRAM; PASS. O teste de grupos não executa o GS real.
- `bash tools/romlab/build.sh`: compilação portátil (reexecutada após todas as alterações). Nenhuma ROM de jogo executada aqui.
- `git diff --check`: PASS.

Não há compilador PS2DEV ou NetherSX2 neste ambiente. Não há ELF, benchmark de FPS, validação do GS real, teste de áudio audível ou teste de compatibilidade de ROM comercial. Não foi feito push remoto. Pendências: Mode 1/7 adicionais, sprites, CPU/scheduler, timing do DSP/echo em ARAM, DSP-4, HostFS, VU0/VU1, etc.

## Teste e aplicação

Use o ZIP somente sobre a Round 7 já aplicada. Preserve os ELFs e logs de Round 7 e 8 separadamente, para comparação A/B. Recomenda-se cold boot, mesma ROM, configuração e percurso; compare `snes-frame`, `ps2-cadence`, `ps2-late-work`, `ps2-audio`, `snes-hdma`, `snes-gs` e hashes. Observe áudio com cadência estável e sem underruns. Se a CI falhar, não force push: guarde o erro e compare os commits. O pacote contém apenas arquivos modificados/novos, não binários nem ROMs.
