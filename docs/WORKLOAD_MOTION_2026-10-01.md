# Cenas em movimento: cache BG, Mode 5 e apresentação PS2

Continuação de [WORKLOAD_FOLLOWUP_2026-10-01.md](WORKLOAD_FOLLOWUP_2026-10-01.md).
Base desta rodada: `d889e82`; base da main: `6c6eb4a`.
O pedido mais recente inclui Mode 5 como prioridade. Travamentos posteriores
de Tales/Star continuam fora desta rodada de desempenho.

## Evidência e limites

O log enviado exercita dois custos distintos em Mode 5:

| Janela do log | Cache de pixels finais hit/miss | PPU | CHR | blend |
|---|---:|---:|---:|---:|
| `f=7300`, movimento | 13.664 / 13.216 | 54% | 18% | 13% |
| `f=7420`, estável | 23.296 / 3.584 | 26% | 3% | 6% |
| `f=7660`, movimento | 11.200 / 15.680 | 54% | 16% | 16% |

As janelas vêm de Count do EE virtual no NetherSX2, não de um PS2 físico.
`f` é global da sessão e não identifica precisamente as fotos. O cache de
pixels finais inclui sprites: movimento de OBJ, CGRAM, VRAM ou registradores
pode legitimamente impedir reutilização. Relaxar essa chave produziria pixels
antigos. A solução deve baratear o caminho de miss e preservar caches de
dados que realmente continuem válidos.

Os testes abaixo comparam estados, vídeo e PCM do core portátil. Contagens
de operações ajudam a provar trabalho evitado; tempos/FPS do host não são
benchmarks do EE, do GS ou do PS2. Não está demonstrada estabilização em
60 FPS, nem ganho líquido de frame time no console.

## 1. Invalidação conservadora por regiões da VRAM

`SnesPPUInvalidateChrCache` invalidava todas as linhas BG após qualquer
escrita de VRAM. Upload de sprites em uma região separada destruía o cache
do fundo mesmo quando seu tilemap e caracteres não mudavam. O cache físico
CHR podia continuar quase todo em hit, mas a montagem das linhas era refeita.

O cache agora usa stamps de 32 páginas de 1.024 palavras. Cada BG depende
conservadoramente de todo seu tilemap e de toda a faixa CHR endereçável,
com wrap da VRAM. Isso não tenta acompanhar apenas tiles visíveis.
Mode 1 com tiles 16×16 inclui uma página extra: o fetch legado pode somar
17 ao tile 1023 sem mascarar o número antes do endereço físico. A faixa
adicional conserva esse comportamento. O fetch hires já mascara suas metades.

Uma escrita relevante sempre avança o stamp; wrap do contador descarta as
entradas. Mudanças de bases/modo continuam comparadas na chave completa.
O cache físico CHR e o cache de pixels finais mantêm suas invalidações.
Não se alteram callbacks, filas PPU, DMA, estados serializados ou timing.
O metadata novo ocupa 180 bytes e os stamps de regiões são memoizados por
BG até mudar uma escrita ou uma faixa. Evita ampliar caches grandes no EE.

Contagens acumuladas dos replays, com o mixer completo ativo a 32 kHz:

| Workload determinístico | Frames | Misses BG antes | Após stamps | Reconstruções evitadas |
|---|---:|---:|---:|---:|
| Trials, intro sem entrada | 10.800 | 170.714 | 63.685 | 107.029 (62,69%) |
| Star, abertura sem entrada | 7.200 | 833.408 | 558.208 | 275.200 (33,02%) |
| Trials, Start periódico até menus | 6.000 | 19.446 | 7.404 | 12.042 (61,92%) |

As contagens não são percentuais de redução do tempo total. O benefício
genérico é preservar fundos de Mode 1/5 durante uploads em regiões distintas.

## 2. Reutilização das células sobrepostas na rolagem horizontal

Depois de um miss exato, o renderer verifica se a janela de 33 células
andou precisamente uma célula para a direita ou esquerda, mantendo iguais
todos os outros dados da chave, incluindo stamp, Y, tamanho, bases e modo.
Nesse caso copia as 32 células sobrepostas e decodifica somente a borda.
As máscaras de opacidade/prioridade são copiadas ainda sem deslocamento;
o alinhamento por fine X e a composição existentes permanecem iguais.

Mode 1 com tiles 16×16 inclui a metade horizontal no deslocamento, com
wrap de 128 células. Nos demais caminhos admitidos, o wrap é de 64.
Mode 5 conserva conjuntamente as duas fases físicas de cada célula.
Mosaic, offset-per-tile, saltos maiores, movimento vertical e mudança dos
dados continuam no caminho completo. Mode 6 não passa a usar este cache.

Na intro natural de Trials, 14.336 linhas usaram essa reutilização;
na abertura de Star, 7.604. Cada ocorrência evita buscar/decodificar 32
células, mas continua copiando/compondo dados. A seleção Mode 5 reproduzida
com Start, A e direita teve zero ocorrências de rolagem admitida: movimento
na tela não implica movimento horizontal reutilizável de BG. Não se atribui
a essa otimização um ganho naquela sequência.

O código C/C++ usa cópias de células alinhadas de 64 bits, adequadas ao EE;
não acrescenta assembly nem amplia os buffers de linhas. Os dois caminhos
beneficiam categorias de jogos, sem consultar título, CRC ou dados da ROM.

## 3. Pixels hires diretamente no staging existente

O caminho simples de Mode 5/6 produzia 1 KiB de pixels BGR555 na stack e
depois copiava essa linha para o scratchpad em 8 KiB. Agora o backend GS
aguarda o GIF e expande os mesmos índices/paleta diretamente nesse staging.
Quando a política existente admite a linha no cache final, copia o resultado
do staging para o cache antes de submeter. Hits finais conservam o upload
normal a partir do cache.

`ExecHiresIndexed` tem implementação padrão para outros backends, preservando
compatibilidade de código-fonte, não ABI binária. Só o GS substitui o destino
da expansão. Formato, ordem sub/main, brilho, 512 pixels e comandos de GS
permanecem iguais. A linha simples remove uma escrita/leitura intermediária
de 1 KiB na stack; o frame da stack da função não encolheu, pois outros
caminhos ainda precisam de temporários. Não se reduz o volume EE → GS:
o GIF ainda transfere 1.024 bytes por linha.

A espera mudou para antes da expansão, pois o DMA possui o staging anterior
até terminar. Isso pode reduzir sobreposição EE/DMA em hardware real e exige
comparação A/B no console. As esperas pequenas no log virtual não provam
ganho líquido. Não foram removidos syncs, mudança de ownership ou efeitos.
O caminho complexo de color math/clipping continua igual.

## 4. Diagnóstico de frame incluindo o frontend

O contador antigo do core termina antes de parte da apresentação.
`GPFifoFlush` inclui a ponte gsKit/legacy com `gsKit_queue_exec`, `gsKit_finish`
e espera DMA. A nova linha `[ps2-frame]`, apenas com diagnóstico ativo, mede
o loop completo e as fases frontend `prep/submit/flip`. Registra também
mínimo/média/máximo de `work = loop - flip` em janelas de 120 displays.
`flip` inclui a rotina de apresentação inteira; subtraí-la não separa
perfeitamente trabalho de espera. `submit` pode misturar CPU e espera GS.

Os campos usam Count, não microssegundos nem FPS. Uma janela de displays
não precisa corresponder a 120 frames do core por causa da cadência regional
e recuperação de VBlank já existentes. Menu/troca de sessão descartam a
janela. A versão `SNES_DIAGNOSTICS=0` não contém esses contadores ou leituras.
Essa extensão não altera o agendamento ou a apresentação.

## Verificações

- 25 executáveis de regressão passaram, incluindo dependências de VRAM,
  scroll ±1, wrap, máscaras, cache, CPU, áudio, SA-1 e loader.
- O self-test do ROM Lab compara 17.408 linhas de cenas sintéticas em
  Mode 1/5 com o renderer frio por linha: ambos os tamanhos de tile, mapas
  pequenos/grandes, dois sentidos, fine X, prioridades, color math e escritas
  CHR. Isso verifica o renderer portátil, não a composição do GS.
- Replays contra `d889e82`: Trials 10.800, Star 7.200 e seleção Mode 5 4.200
  frames, estados/vídeo equivalentes. Hashes de 90/60/35 janelas PCM iguais,
  mixer completo a 32 kHz. A etapa isolada de stamps também foi comparada.
- O método hires da versão PS2 normal compilada foi executado em Unicorn
  MIPS64: 48 casos com paletas/índices, cache opcional, ausência de target,
  guardas e escrita só depois da espera. Conclusão/submissão DMA foram stubs:
  isso não emula GS, MMI, latência ou desempenho de um R5900 físico.
- Builds PS2 normal e diagnóstico nível 1 passaram. As advertências legadas
  de compilação persistem. Não se altera o nível de otimização do mixer MMI.

## Comparação no console e pendências

O pacote inclui ELF normal novo e o normal da rodada anterior, com os mesmos
defaults de apresentação. Compare a mesma cena, configuração e percurso,
sem diagnóstico; depois uma execução diagnóstica curta permite distinguir
core, submissão e apresentação. O pacote não inclui ROMs ou save states.

Mode 7, CPU/SPC, apresentação com carrier de 512 pixels, travamentos
posteriores e underruns de áudio ainda precisam de investigação/medida.
A ligação histórica entre o carrier e a queda relatada de 60 para 30 FPS
continua sem demonstração causal; não foi revertida uma correção visual.
Estas mudanças ficam na branch de revisão e não entram na main apenas por
compilar ou passar no host.
