# Investigação de workloads — 2026-10-01

## Resultado e limites da investigação

Base: `main` em `6c6eb4ae7337e9f206f1f560ac09f6ff3af11c83`.
A main original foi compilada para PS2 antes de qualquer alteração.

Foram implementadas quatro mudanças pequenas e gerais:

| Commit | Resultado |
|---|---|
| `816975c` | Recuperação conservadora de headers lineares com checksum desatualizado, preservando mapper e SRAM. |
| `9a2168b` | Feedback de voz por amostra calculado somente quando a próxima voz usa PMON; OUTX continua atualizado. |
| `621e7a8` | S-DD1 preserva MEMSEL/FastROM, não remapeia escritas idênticas e restaura os vínculos físicos no reset. |
| `5aee9bf` | Uma alteração de MMC do S-DD1 atualiza apenas sua janela de 1 MiB. |

A tela preta de **Tales of Phantasia fornecido** foi reproduzida e sua causa
isolada no carregamento do header. O arquivo original inicia com a correção.
Star Ocean revelou trabalho redundante de remapeamento e um erro de timing
FastROM. O mixer revelou cálculo redundante introduzido junto da correção
necessária de PMON.

**Não foi medido FPS no PS2 real.** Este ambiente dispõe do compilador EE e
do core portátil, mas não de um console conectado. A causa do relato de
Mode 7 passando de 60 para 30 FPS permanece aberta. Há um candidato histórico
concreto no caminho GS, descrito abaixo; não há evidência suficiente para
atribuí-lo como causa ou revertê-lo. Nenhuma melhoria de FPS no console é
declarada a partir das medições de host.

As alterações estão preparadas para revisão e validação no hardware. Compilar
e passar os testes de host não demonstra equivalência do mixer MMI nem do
renderer GS em execução real, e não justifica integração imediata na main.

## Como os componentes existentes se relacionam

O projeto combina o core legado de Icer Addis, renderização por scanline,
código EE especializado, listas GIF/GS próprias e frontend PS2 mais recente.
O ROM Lab exerce o core portátil e outro backend de composição. Não é uma
simulação dos custos de EE, scratchpad, DMA e GS.

| Componente | Caminho ativo e dependências relevantes |
|---|---|
| Frontend PS2 | `mainloop_exec.cpp` escolhe `SNCPUExecute_ASM_Plain` para cartuchos comuns e `SNCPUExecute_ASM` para o bookkeeping de barramento do SA-1. SPC700 usa `SNSPCExecute_C`. A existência de código MIPS SPC700 não significa que ele seja o executor ativo. |
| S-CPU e memória | Bancos de 8 KiB, com registros de 16 bytes no EE; ponteiros diretos para RAM/ROM e traps para I/O. O assembly conserva o contador de ciclos em registrador. Open bus, interrupções e esperas do barramento são comportamento observável. |
| Scheduler | `SnesSystem::ExecuteFrame` coordena fatias da CPU, scanlines, IRQ/NMI, MDMA/HDMA, SPC e mixagem. WAI e portas APU dependem dessa ordem. |
| PPU | Escritas de controle ficam em fila; portas com efeitos temporais sincronizam escritas anteriores. Alterar essa ordem pode modificar DMA, janelas, sprites e raster effects. |
| Fetch gráfico | `RenderLine8` prepara BG/OBJ, buffers de índices main/sub e máscaras. Mode 7 faz transformação inteira e busca afim, com reutilização do byte de tile quando permanece no mesmo tile. Main/sub compartilham o fetch. |
| Caches | Cache físico CHR compartilhado ocupa 448.512 bytes; cache de linhas BG tem duas vias. Invalidação por VRAM e estado visual faz parte da correção, não é trabalho removível sem prova. |
| Composição PS2 | `snppublend_gs.cpp` transfere índices/paletas e compõe main/sub/color math com listas GIF reutilizáveis. Há um único staging de scanline no scratchpad. |
| Áudio | SPC700 alimenta a fila de escritas DSP. `SNSpcDspMixFull::Mix` sincroniza por blocos, decodifica/interpola BRR, aplica envelopes, ruído, PMON, mixagem e echo. O final da mixagem usa MMI específico do PS2. |

### Restrições que impedem otimizações isoladas

- Áudio e vídeo compartilham temporalmente o scratchpad: buffers do mixer
  ocupam aproximadamente os primeiros 10 KiB; staging gráfico usa a região
  intermediária; tabelas gráficas ocupam aproximadamente 11–15,5 KiB.
  O layout atual não comporta simplesmente adicionar um segundo staging.
- `DmaSyncGIF` antes de sobrescrever/reparametrizar o staging protege dados
  ainda consumidos pelo DMA. O encerramento do renderer também protege a
  reutilização pelo mixer. Remover essas esperas sem mudar o protocolo seria
  uma corrida, não uma otimização equivalente.
- A primeira scanline renova a CLUT completa porque o scratchpad também foi
  usado pelo áudio. As demais já usam atualização esparsa quando apropriado.
- O Makefile compila `snspcmix.cpp` com `-O1`: há histórico de corrupção do
  assembly de 128 bits com GCC 15.2 em `-O2`. Essa exceção, o assembly e os
  flags foram preservados.
- Registrar um novo mapper ou mudar ponteiros de bancos não deve mudar os
  ciclos de acesso selecionados por `$420D`.
- O formato de save state não foi alterado. O estado atual não serializa
  completamente o S-DD1; portanto os testes desse coprocessador começam no
  reset. Save states no meio de descompressão exigem uma investigação própria.

## Tales of Phantasia: causa da tela preta

O arquivo traduzido tem 6 MiB, mapa `$35` e headers em `$00FFC0` e
`$40FFC0`. A cópia estendida identifica a tradução; ambas têm checksum
`$FFFF` e complemento `$9FB2`, que não são complementares.

A main descartava esses headers. O carregamento aparentemente terminava,
mas sem os metadados corretos, com mapa incorreto e vídeo preto. Não havia
evidência de que CPU, Mode 7 ou custo de áudio fossem a causa inicial.

Experimento de controle: em uma cópia privada, foram alterados **somente os
pares checksum/complemento**, mantendo todo o restante. A main passou a
iniciar. A correção do loader, aplicada ao arquivo original, produziu os
mesmos checkpoints de core e vídeo desse controle durante 3.600 frames.
Isso isola a decisão do loader; não é um patch distribuído para a ROM.

A recuperação nova só ocorre depois de falharem as decisões existentes com
checksum válido e recuperação de formato de copiador. Exige evidências
independentes: mapper coerente com a posição física, título majoritariamente
imprimível, metadados plausíveis, vetor de reset e opcode inicial plausíveis.
Não usa título, CRC ou identidade como condição; não embaralha uma imagem
linear com base em um header sem checksum confiável.

O ares foi consultado exclusivamente para confirmar a premissa: checksum é
evidência para identificação da imagem, não uma exigência do barramento do
SNES. Seu `scoreHeader` atribui bônus ao par válido sem torná-lo obrigatório.
O algoritmo e a arquitetura de carregamento do SNESticle foram mantidos.

Além do controle de 3.600 frames, o original corrigido foi executado duas
vezes por 7.200 frames com mixer completo, chegando à sequência de
biblioteca/diálogo com vídeo visível e checkpoints determinísticos. O detector
genérico de possível deadlock também sinaliza telas estáveis aguardando
entrada; esse sinal sozinho não comprova travamento. A conclusão de boot não
equivale a uma validação do jogo inteiro.

## Star Ocean: MMC, FastROM e custo real por escrita

### Timing incorreto

`RemapSDD1` chamava `SNCPUSetMemSpeed(..., SNCPU_CYCLE_SLOW)` ao mudar
segmentos. Isso desfazia FastROM mesmo se `$420D` continuasse habilitado.
MMC `$4804`–`$4807` seleciona dados, não altera MEMSEL da CPU.

No ares, a decisão de espera de ROM depende do endereço e de `io.romSpeed`;
`$420D` seleciona 6/8 ciclos, enquanto a escrita MMC altera somente a seleção
de ROM. A correção local remove a atualização indevida de velocidade e
mantém o mecanismo existente de mapeamento.

Essa é uma mudança observável de timing: o trace de Star Ocean diverge da
main já no índice de frame 2. Não se exige igualdade com uma referência
incorreta nem se apresenta essa diferença como simples ganho de host FPS.
O mapa e os ciclos de todos os bancos são verificados por fixture sintética;
o workload corrigido foi repetido deterministicamente por 10.800 frames.

### Trabalho redundante

Contadores mostravam cerca de 480 remaps por janela de 120 frames. O estado
final `0/1/2/3` sugeria inicialmente escritas repetidas, mas o trace por escrita
mostrou alternância real: por exemplo, segmentos 2/3 mudam para 4/5 e depois
retornam. Essas mudanças não podem ser omitidas.

O desperdício comprovado era reconstruir **todas as quatro janelas** para
cada alteração de **um** registrador. Cada janela tem 128 bancos de 8 KiB.
Agora uma escrita MMC atualiza 128 entradas em vez de 512: **75% menos
entradas de banco**, sem modificar a frequência de mudanças reais. Isso
reduz chamadas internas, stores e registros de bancos tocados no EE. O
contador de remaps continua contando chamadas; não mede essas entradas.

Escritas do mesmo valor também deixam de marcar o mapa como sujo. Uma marca
já pendente não é apagada por uma escrita idêntica. Como o reset do chip
restaura seus registradores, o reset do sistema agora restaura explicitamente
os vínculos físicos; antes, remaps incondicionais posteriores mascaravam essa
dependência. Inicialização e reset conservam o remapeamento das quatro janelas.

A otimização de janela foi comparada separadamente com a versão que ainda
reconstrói as quatro janelas, **já com FastROM corrigido**: igualdade de
10.800 frames de core/vídeo e de 90 checkpoints cumulativos de PCM. O áudio
também coincide com ambas as repetições da referência.

Descompressão S-DD1 aparece em rajadas, mas muitos intervalos observados têm
zero descompressões e continuam exercitando CPU/renderização/remapeamento.
O tamanho de 6 MiB não demonstra que descompressão ou áudio dominem as cenas
relatadas. Para localizar as cenas de gameplay de 30 FPS é necessário replay
ou log do console, além do attract mode executado aqui.

## Mixer: manter PMON e eliminar feedback sem consumidor

A correção recente de PMON passou a construir `m_iVoiceOutput` para cada
amostra de cada voz. O buffer serve à modulação da voz seguinte; OUTX observa
apenas a última amostra do bloco. Main/echo possuem sua própria matemática
de envelope e volume e não consomem esse buffer completo.

A mudança lê a máscara PMON **após** sincronizar as escritas DSP de cada
bloco. Quando a próxima voz usa PMON, executa o cálculo completo original.
Caso contrário, calcula apenas a amostra final para OUTX. Predecessor silencioso
zera o buffer sempre que há consumidor PMON; voz 0 continua ignorando PMON.
Mudanças da máscara dentro de uma chamada continuam sendo vistas nos blocos
seguintes. Gaussian, BRR, ruído, echo e mixagem MMI não foram simplificados.

Exemplo de quantidade de trabalho: oito vozes ativas, PMON zero, 533 amostras
divididas em oito blocos. O feedback passa de 4.264 cálculos por frame para
64, evitando 4.200 cálculos e stores de feedback. Isso **não** representa
98% do custo total do DSP nem uma previsão de ganho de FPS. Com muitos
consumidores PMON, o ganho diminui conforme necessário para preservar o som.

Fixture de mixer completo, com BRR sintético e hashes da main original:
256 máscaras PMON, 32/44,1/48 kHz, blocos de 0/1/2/67/68/69/137/533 amostras,
vozes ativas/silenciosas, rekey, volume positivo/negativo, ruído, echo,
escritas enfileiradas e observação de ENVX/OUTX/ENDX. Todos os hashes coincidem.
Nos workloads, Trials coincide com a main em 90 checkpoints PCM cumulativos
(10.800 frames); Star coincide em 60 checkpoints (7.200 frames) antes da
correção independente de FastROM.

**Limite importante do ROM Lab:** o runner padrão passa mixer nulo e usa o
mixer silencioso determinístico. Ele não mede o custo do mixer completo.
Para esta comparação foram usadas cópias instrumentadas fora do repositório,
com stereo 32 kHz e 533/534 amostras por frame NTSC. Tempos de host ficaram
fora das comparações de igualdade. Não foi adicionada instrumentação ao
executável normal.

## Trials of Mana: sequência e investigação histórica

Replay desde reset, sem entrada, índices de frame começando em zero:

| Intervalo observado | Caminho |
|---|---|
| 4.295–5.280 | Mode 7, mapa/mundo na introdução. |
| 5.281–7.179 | Mode 1, paisagem/nuvens e árvore de Mana. |
| 7.180–9.993 | Segunda sequência de Mode 7, céu/estrelas/orbe. |

Esses intervalos identificam o replay testado, não garantem correspondência
exata com cada cena de 30 FPS relatada no console. Frame 4.800 mostra o mapa;
5.520 a paisagem com nuvens; 6.120 a árvore; 7.320 céu escuro/orbe. É útil
separar esses caminhos ao coletar o próximo log.

### O que o histórico demonstra

- `3ae48cd`: correção do arredondamento da transformação Mode 7. É necessária
  para o comportamento gráfico e não foi removida.
- `434a4e3`: redução de custo de Mode 7/HDMA, incluindo reutilização do byte
  do tile. Dele até a main analisada, `snppumode7.h` mudou apenas comentários:
  os produtos/arredondamento e os três loops de fetch permanecem iguais.
  Portanto não foi encontrada uma regressão recente **dentro desses loops**.
- `25be5e1` (21/09): caminho GS de saída 512 dots. Também passou a desenhar
  linhas comuns de 256 pixels em um destino de 512 pixels PSMCT16. O desenho
  final direto dobrou a largura; desenhos finais de color math/brightness
  também ampliaram a área, embora intermediários continuem com 256 pixels.
  Isso é uma mudança estrutural confirmada de trabalho GS, não uma prova
  de que causou 60→30 FPS. O commit contém suporte necessário a hires e
  pressupostos de apresentação; uma reversão cega pode quebrá-los.
- Mudanças posteriores em Gaussian/PMON, open bus, portas temporizadas e
  DMA adicionaram comportamento correto e também trabalho. Não foi encontrada
  evidência que autorize remover esse comportamento para recuperar FPS.

Não há revisão histórica conhecida, acompanhada de log do mesmo PS2 e mesma
cena, que comprove o ponto de 60 FPS. Sem ela, um bisect de custo de host
seria incapaz de localizar de forma confiável uma regressão EE/GS.

### Causa comum e hipóteses descartadas

O mapa Mode 7 usa frequentemente a variante direta main; árvore/nuvens usam
Mode 1 com janelas/color math. As duas sequências compartilham staging e
saída GS, mas não necessariamente o mesmo custo dominante.

Nos trechos estáveis de Mode 1 observados, o cache de linhas BG já apresenta
quase só hits (um intervalo: 40.768 hits, zero misses). Não há evidência para
reimplementar a decodificação CHR como primeira solução para essa cena.
O probe da seleção de variantes GS registrou até três transições por janela
de 120 frames; não demonstrou reconstrução das listas a cada scanline como
causa dominante. O probe antecipa a seleção do backend, não mede execução GS.

No host, CPU e PPU dominaram; o mixer completo foi uma parcela menor e
mensurável. Dentro da PPU, composição portátil e fetch Mode 7 são candidatos.
Esses perfis usam C em vez do executor EE e composição C em vez do GS:
não se transferem suas porcentagens ao console.

Mode 5 não apareceu nesses intervalos do replay. O cache de linha existente
depende de estado visual/gerações; movimento legítimo pode invalidá-lo. Não
foi alterado nem identificado como causa das duas cenas prioritárias.

## Decisões sobre EE, GS, cache e assembly

As duas reduções implementadas atuam em trabalho provado no EE: feedback
sem consumidor e atualização de bancos independentes. Ambas preservam as
interfaces públicas e o layout compartilhado e se aplicam a classes de
workloads, não aos nomes das ROMs.

Não foi adicionado assembly: a redução de chamadas e stores em C/C++ resolve
o trabalho redundante identificado. Avaliar assembly para SPC700 ou fetch
Mode 7 exigiria primeiro perfil EE, estimativa do ganho e testes equivalentes
do executor/aritmética. Seu código legado especializado não foi substituído.

Não foi transferida nova operação para o GS. O backend já faz composição;
nenhum experimento deste ambiente demonstrou que converter/transferir dados
adicionais custaria menos que o cálculo EE. Também não foi retirado color
math do GS sem medir seu custo completo. Double buffering e batching podem
reduzir esperas, mas exigem espaço, protocolo de DMA e medição do consumidor.
Não são alterações pequenas demonstradas pelos testes atuais.

## Validação executada

| Verificação | Resultado e alcance |
|---|---|
| Main original PS2 | Build completa, 166 fontes, zero erros; warnings existentes. |
| Candidato PS2 | Builds normal (`SNES_DIAGNOSTICS=0`) e diagnóstica nível 1; símbolos CPU ASM, SPC C e mixer PS2 resolvidos. Não executadas no console. |
| Suítes host | 24 executáveis passaram: CPU, ROM, 19 PPU/áudio/diagnóstico, SA-1, netplay e ROM Lab self-test. |
| Loader sintético | Lo/Hi/ExLo/ExHi, header duplicado, metadados SRAM, payload intacto, prioridade do checksum válido, rejeição de reset inválido e proteção contra falso deinterleave. |
| S-DD1 sintético | Escritas iguais/diferentes/pendentes, enable DMA independente, todas as 128 páginas das quatro janelas após cada mudança sob SlowROM/FastROM, janelas não alteradas e reset. |
| Mixer sintético | PCM/ENVX/OUTX/ENDX idênticos à main nos casos descritos acima. Integrado ao CI host. |
| Tales | 3.600 frames iguais ao controle com checksum reparado; original corrigido determinístico em 7.200 frames com áudio completo. |
| Trials | 12.000 frames padrão iguais à main; 10.800 frames com mixer completo e 90 checkpoints PCM iguais à main. |
| Star | Otimização do mixer isolada igual à main em 7.200 frames/60 checkpoints PCM; timing corrigido determinístico em 10.800 frames; remap parcial igual ao completo corrigido em 10.800 frames/90 checkpoints PCM. |
| Super Mario RPG | 2.400 frames de core/vídeo iguais à main, como regressão adicional SA-1. Não foi workload principal. |
| Revisão | `git diff --check`, callers/estado compartilhado e diff revisados. Sem probes temporários, ROMs ou condições novas por título/CRC. |

“Igual” nesta tabela significa igualdade dos checkpoints de core e vídeo,
excluindo custo de execução e identificação do controle com ROM alterada.
Os checkpoints de PCM são hashes cumulativos separados. São detectores de
regressão, não prova de ausência de bugs em qualquer jogo ou de equivalência
da execução no PS2. Capturas visuais foram examinadas nos pontos da intro.

Os probes temporários ficaram em worktrees privados e não estão no código
de produção. O CI recebe somente fixtures sintéticas. Donkey Kong Country e
Top Gear não foram utilizados como workloads desta investigação.

## Próxima validação necessária no PS2

1. Usar baseline e candidato com o mesmo console, refresh, áudio, frameskip,
   configuração e mídia. Executar desde reset a intro de Trials e as cenas
   pesadas de Star; confirmar boot, vídeo e áudio de Tales.
2. Medir o ELF normal para FPS apresentado. Usar nível 1 para distribuir
   custo por janela: `[snes-frame]` min/avg/max e orçamento, `[snes-perf]`,
   `[snes-ppu-stage]`, `[snes-gs]`, `[snes-audio]` e `[snes-sdd1]`.
   Tempos inclusivos se sobrepõem; não somar percentuais como partes exclusivas.
3. Analisar o log com `python3 tools/snesdiag/analyze.py emulog.txt`.
   Separar espera GIF, cópia/kick, fetch Mode 7, composição e áudio.
   FPS apresentado em torno de 30 pode refletir catch-up/vblank, não somente
   um core que consome exatamente o dobro do orçamento.
4. Se a espera/custo GS dominar ambas as cenas, fazer um experimento isolado
   sobre a ampliação 256→512 do commit `25be5e1`, preservando hires, coordenadas,
   formato de destino e color math. Se EE dominar, usar o perfil específico
   para escolher o próximo loop. Não assumir antecipadamente nenhuma opção.
5. Comparar com uma revisão comprovadamente boa no mesmo hardware, se houver,
   antes de atribuir a regressão histórica. Manter correções gráficas e
   temporais mesmo se precisar separar a parte cara de um commit.
6. Conferir sprites/prioridades/layers, janelas/transparência/color math,
   flicker, som/echo/PMON, sincronização, resets e crashes; repetir a build
   e revisar o diff antes de aprovar integração.

Builds reproduzíveis:

```sh
make JOBS=4 LOAD_LIMIT=4 SNES_DIAGNOSTICS=0
make strip SNES_DIAGNOSTICS=0
# Copiar o ELF normal antes de mudar o modo da build.
make JOBS=4 LOAD_LIMIT=4 SNES_DIAGNOSTICS=1
./tools/pputest/build.sh
./tools/pputest/mixer_feedback_test
./tools/romtest/build.sh && ./tools/romtest/rom_test
./tools/romlab/build.sh && ./tools/romlab/romlab self-test
```

O Makefile atual invalida corretamente objetos quando o modo diagnóstico
muda. O nível 2 é mais intrusivo e deve ser reservado a anomalias, não usado
para atribuir FPS normal.

## Referências de comportamento

ares consultado em `4cb8d92b441557cb6bcaf133c4cbc7f6819b1122`:

- [`mia/medium/super-famicom.cpp`](https://github.com/ares-emulator/ares/blob/4cb8d92b441557cb6bcaf133c4cbc7f6819b1122/mia/medium/super-famicom.cpp): pontuação do header/checksum.
- [`ares/sfc/cpu/memory.cpp`](https://github.com/ares-emulator/ares/blob/4cb8d92b441557cb6bcaf133c4cbc7f6819b1122/ares/sfc/cpu/memory.cpp) e [`io.cpp`](https://github.com/ares-emulator/ares/blob/4cb8d92b441557cb6bcaf133c4cbc7f6819b1122/ares/sfc/cpu/io.cpp): espera de ROM e MEMSEL.
- [`ares/sfc/coprocessor/sdd1/sdd1.cpp`](https://github.com/ares-emulator/ares/blob/4cb8d92b441557cb6bcaf133c4cbc7f6819b1122/ares/sfc/coprocessor/sdd1/sdd1.cpp): escrita de MMC.

Não foi importado código nem copiada a arquitetura do ares.
