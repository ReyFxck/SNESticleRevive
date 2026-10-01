# Retorno dos testes: Mode 7 e saída de áudio

Continuação de [WORKLOAD_AUDIT_2026-10-01.md](WORKLOAD_AUDIT_2026-10-01.md).
Base original: `main` em `6c6eb4a`; primeira entrega: `5e7c31b`.
O retorno relata pouca diferença nas cenas pesadas, travamentos posteriores
em Tales/Star e áudio granulado/estourado. A prioridade desta etapa é custo
de execução e qualidade do áudio; os travamentos posteriores ficam em aberto.

Foram feitas duas mudanças de produção, sem mudar interfaces, estados
serializados, layout do scratchpad ou a divisão EE/GS:

| Commit | Mudança |
|---|---|
| `66bc07e` | Volume 100% preserva o PCM, em vez de aplicar ganho de 2× com saturação. |
| `09bdb27` | Fetch de Mode 7 repeat compartilha a seleção do tile entre quatro pixels quando os endereços provam que é possível. |

Não há novo código específico de ROM nem assembly novo. Não foi constatado
o ganho de FPS no PS2 real e não se afirma que estas mudanças resolvem todas
as cenas relatadas.

## O que o log permite concluir

O `emulog.txt` enviado contém 73 janelas de diagnóstico e 2.458 registros.
Foi gerado no NetherSX2/Android executando o ELF anterior com diagnóstico
nível 1. As fotos mostram execuções sem diagnóstico. Percentuais EE/GS das
fotos representam threads/custos do emulador de PS2 no telefone; não são
medidas de ocupação do hardware de um PS2 físico.

Os percentuais abaixo vêm dos contadores internos de Count do EE virtual.
Servem para localizar trabalho nesta execução, não para transformar os FPS
de host nem o campo `capacity` em FPS previsto no console. `f` é o contador
global da sessão, incluindo trocas de ROM; não identifica o frame desde o
reset de cada jogo. Sem entradas registradas, não é possível associar cada
janela exatamente a uma das fotos.

| Janela/caminho | Evidência no log | Consequência |
|---|---|---|
| Trials, `f=6580`, Mode 1 | PPU 52%, CPU 33%, mix 6%. Fetch/composição BG 35%; estágio CHR 18%; blend 9%. | O custo pesado também existe fora do Mode 7. |
| Trials, `f=8380`, Mode 1 + 7 | CPU 38%, PPU 39%, mix 9%. Mode 7 21%; BG/composição 30%; blend 7%. | O fetch afim é um caminho quente concreto, adequado a uma otimização pequena no EE. |
| Trials, janelas posteriores com Mode 7 | Fetch Mode 7 aproximadamente 19–23% do tempo contado. | Evitar seleção/branches redundantes por pixel tem alcance mensurável dentro da função. |
| Star, 14 janelas | CPU 40–79%, PPU 9–40%, mix 5–17%. | A hipótese inicial de áudio como causa única não é sustentada. As cenas não têm um único gargalo constante. |
| Trials, Mode 5 em movimento | PPU chega a 54%, blend 13–16%. | Registrado para investigação posterior, conforme a prioridade solicitada. |

Timers de BG/Mode 7 e de sincronização da PPU são inclusivos: não se somam
como fases independentes. `ppu-stage sync` inclui renderização executada
por `Sync`; 53% ali não significa 53% esperando uma fila.

Na janela Mode 1, três BGs são buscados por linha, com color math em todas
as linhas. O cache físico CHR registra 548.849 hits, 24 misses e nenhuma
invalidação. O cache de linhas BG tem 60.270 hits/20.370 misses, cerca de
25% misses. Mesmo com CHR já decodificado, consulta, cópia e montagem das
linhas custam tempo. Isso não prova conflito entre as duas vias nem autoriza
aumentar o cache: falta medir reutilização de chaves após expulsão. Esse
caminho continua sendo um alvo relevante para a cena de árvore/fadas/nuvens.

As esperas GIF por scanline foram pequenas no log virtual. Contudo,
`DmaSyncGIF` mede consumo pelo DMA e não cobre todas as esperas do GS:
`GSK_FlushFrame` chama `gsKit_finish`, e a apresentação chama
`gsKit_sync_flip`. Portanto, o log não exclui custo no GS/apresentação.
Não foi removida nenhuma espera que protege staging compartilhado.

## Saída de áudio: saturação demonstrável e trabalho evitado

`AudMixBuffer::Flush` aplicava ganho de 200% quando Game Volume estava em
100. Uma amostra válida de 17.000 virava 32.767; valores acima de 16.383
perdiam sua forma de onda por saturação. A origem desse ganho na main é
antiga; o commit `5d12677` contém uma correção semelhante em outra linha de
histórico, mas não é ancestral da main usada aqui. Não foi encontrada uma
introdução recente desse ganho na main que explique sozinha a ampliação
recente do problema relatado.

Agora 100% é unidade, e 0–99% atenuam. O caminho padrão pula todo o loop de
ganho, reduzindo acessos, multiplicações, divisões e clamps por amostra nos
dois canais. A cadência, resampling, DSP, echo, fila audsrv e SPU2 permanecem
iguais. O controle também beneficia NES porque o frontend é compartilhado.
O timer de mixagem inclui `Flush`; este custo não é externo àquele timer.

O teste novo captura a fronteira de enqueue do frontend real. Verifica todos
os 65.536 valores int16 nos dois canais, filas síncrona/assíncrona e volumes
100, 50, 0, 1, 99 e valores fora da faixa. Em 100%, a saída 48 kHz é idêntica
à entrada, sem clipping introduzido pelo frontend. O resampler não foi
modificado. O teste não demonstra ausência de underrun no IOP nem valida o
mixer MMI em hardware. O log não possui uma medição de underruns: amostras
entregues em cadência regular não demonstram que a reprodução seja regular.

Esta correção resolve uma causa comprovada de áudio estourado, mas não
permite prometer que todo áudio granulado esteja corrigido. Volume 100%
ficará menos alto que na entrega anterior. Para comparar com a mesma
amplitude, use volume 50% na anterior e 100% na nova; não restaure ganho
digital de 2× no core para compensar o volume de escuta.

## Mode 7: reutilização exata em grupos de quatro

O fetch repeat anterior selecionava/comparava o endereço do tile a cada
pixel, embora já reutilizasse o byte do tilemap. A nova versão calcula os
campos do endereço diretamente das coordenadas 24.8 e testa os extremos de
grupos de quatro pixels. Cada pixel continua lendo seu próprio byte CHR.

Os incrementos vêm de coeficientes signed 16-bit; o flip horizontal também
permite +32768. Três passos têm deslocamento absoluto máximo 98.304, menor
que um período de 262.144 menos a largura fixa de um tile (2.048). Como
cada eixo é afim/monótono, extremos no mesmo tile do mapa repetido não
podem esconder uma volta completa nem uma saída e retorno. Isso prova que
os quatro pixels usam o mesmo tile. Endereços diferentes seguem quatro
buscas individuais; incrementos fora dessa faixa e a cauda de 0–3 pixels
seguem o caminho escalar. Conversão para unsigned preserva os campos de
wrapping; os campos OR de tile e pixel não se sobrepõem.

A VRAM permanece estável durante `RenderLine`; HDMA/escritas PPU são
sincronizadas pelo mecanismo existente. Não há cache atravessando scanlines.
Produto/arredondamento da transformação, EXTBG/prioridade, color math,
transparência, leitura CHR e ordem das linhas não foram modificados.
As variantes clamp e black mantêm o código anterior.

Uma probe privada de 10.800 frames da intro encontrou 830.000 chamadas de
fetch: 63,68% dos grupos de quatro tinham os extremos no mesmo tile. Oito
pixels reduziam essa proporção para aproximadamente 31%, portanto foi
escolhido o grupo menor. Um caminho horizontal especial não foi adicionado:
`dy=0` ocorreu em apenas 224 dessas chamadas. Nenhuma probe entrou no código
entregue. Também não foi adicionado assembly para flips CHR: a probe
registrou aproximadamente 1,7% de linhas com flip, sem evidência de custo
suficiente para justificar a especialização nesta etapa.

### Código compilado, não estimativa de FPS

Wrappers antes/depois foram compilados com GCC EE 15.2 e as flags de
otimização/aritmética de produção, incluindo todas as `CONSERVATIVE_FLAGS`.
O corpo de 1.100 bytes do fetch novo é **idêntico byte a byte** ao corpo no
ELF normal entregue.

| Vetores de 256 pixels | Instruções antes | Depois | Resultado |
|---|---:|---:|---|
| 83 linhas amostradas do workload | 537.428 | 384.566 | 28,44% menos no fetch repeat |
| 65 vetores aleatórios/limites | 448.362 | 375.389 | 16,28% menos no fetch repeat |
| Mesmos vetores, clamp | 367.958 | 367.958 | Sem mudança |
| Mesmos vetores, black | 227.351 | 227.351 | Sem mudança |

A execução usa Unicorn MIPS64, com reprodução explícita da variante R5900
de `MULT/MULTU` com destino GPR. Pixels e sentinelas coincidiram com uma
referência independente para comprimentos 0, 1, 3, 4, 7, 255 e 256. Isso
verifica código inteiro e conta instruções, mas **não modela cache, pipeline,
latências, DMA ou GS do PS2**. Não significa 28,44% de ganho de FPS. O texto
dos três wrappers cresceu 968 bytes; impacto no I-cache exige medição no EE.
Uma tentativa de otimizar black aumentou seu número de instruções e foi
descartada antes do commit.

## Validação e próxima comparação

- 25 executáveis passaram: 24 de componentes e o self-test do ROM Lab.
  O teste Mode 7 conserva 10.000 vetores aleatórios e um milhão de vetores
  da transformação; adiciona limites de tile/mapa, passos fora da faixa,
  tails, buffers sem alinhamento e sentinelas.
- Trials: 12.000 frames de core/vídeo coincidem com a main original e a
  repetição é determinística. Replay de 10.800 frames com mixer completo
  compara também checkpoints PCM anteriores à saída do frontend.
- Star: 10.800 frames são comparados à referência com FastROM já corrigido;
  não se exige equivalência ao timing incorreto da main. Os checkpoints PCM
  antes do frontend também são preservados.
- Normal (`SNES_DIAGNOSTICS=0`) e diagnóstico nível 1 foram compilados para
  PS2; o pacote identifica cada ELF e inclui hashes SHA-256.
- Diff revisado: somente as duas mudanças descritas, seus testes/CI e esta
  documentação; sem probes privadas, arquivos de jogos ou hacks temporários.

Teste de desempenho: use o ELF normal, a mesma configuração, a mesma cena
e avanço de frames; faça reset da ROM a cada execução, especialmente S-DD1.
O ELF diag1 serve para separar fases e não deve ser a única referência de
FPS. Compare Mode 7 separadamente de Mode 1/Mode 5. No NetherSX2, conserve
também taxa de ciclos, cycle skip e backend ao comparar versões; seu resultado
não substitui o benchmark no console.

A main permanece intacta. A PR continua em draft até validação do renderer
GS, áudio MMI e tempos de frame em PS2 real. A regressão histórica 60→30,
o custo de apresentação GS e os travamentos posteriores ainda não têm
causa comprovada; as correções gráficas/timing anteriores foram preservadas.
