# Mode 7: mapa e nuvens girando

Base da rodada: `08478bd`, na mesma branch de revisão da PR #97.
A main continua baseada em `6c6eb4a`; este trabalho depende das melhorias
anteriores já testadas pelo usuário na branch.

## Identificação dos dois trechos

A reprodução da intro, com mixer completo a 32 kHz, identificou a foto das
nuvens girando como **Mode 7**, assim como o mapa. A cena da árvore/fadas
anterior é Mode 1. No replay sem entrada, o mapa aparece aproximadamente
entre frames 4.320–5.200 e as nuvens entre 7.200–10.046. Esses números são
locais do replay e não o contador global do log no console.

A foto usa NetherSX2. Seus percentuais EE/GS e a bancada portátil não medem
um PS2 físico. Esta rodada mede operações, verifica equivalência e entrega
ELFs para comparação; não comprova 60 FPS no console.

## Eliminar composição sem contribuição visível

Nas nuvens, CGADSUB seleciona OBJ para color math, mas muitas linhas não
contêm nenhum pixel visível elegível após prioridades, janelas e paletas de
sprites. A composição completa do GS era escolhida pelo registrador, mesmo
quando a máscara efetiva de math já estava vazia.

`RenderLine8` agora omite apenas a composição da subtela quando a máscara
resolvida de math da tela principal é vazia. OBJ continua sendo avaliado e
buscado antes disso; flags de limite, filas PPU, estado CGRAM, CPU, DMA/HDMA
e timing não são omitidos. Native/pseudo hires mantêm ambas as telas.

Depois de aplicar a janela de math, o backend PS2 escolhe a expansão direta
quando a máscara efetiva é vazia, o clipping principal está desativado, a
brightness é 15 e a resolução é normal. Pixels que usam math continuam no
caminho completo, inclusive adição/subtração e halving.

Em quatro janelas de 120 frames das nuvens, 22.140 de 26.880 linhas (82,37%)
eram elegíveis. Isso é uma contagem de linhas, não redução de frame time.
O mapa já usava o caminho direto; só essa mudança não resolveria seu custo.

## Agrupar envios ao GS

O caminho direto reúne até oito linhas consecutivas com a mesma paleta em
uma transferência PSMT8 e um sprite 512 × N. A expansão horizontal e o
carrier PSMCT16 de 512 pixels permanecem os mesmos. TEX1 é nearest no
blender; a relação vertical de UV/destino permanece 1:1, sem misturar linhas.

Cada linha mantém sua própria busca afim e sua composição de índices. Não
há reutilização de pixels entre frames, de matrizes ou de resultados de ROM.
As linhas são copiadas imediatamente, então mudanças posteriores de VRAM,
OAM ou dos registradores não alteram linhas já enfileiradas.

Mudança de paleta, source, descontinuidade de Y, saída para math/brightness,
hires e End/Begin encerram o lote antes de reutilizar seu armazenamento.
O snapshot da paleta é feito na primeira linha, antes de qualquer alteração
posterior de CGRAM. Upload completo/parcial e o CLUT de início de frame
continuam funcionando. O último lote é enviado e aguardado antes do mixer.

A staging de hires em 8 KiB do scratchpad é emprestada para até 2 KiB de
índices. Uma checagem de layout impede alcançar o lookup em 11 KiB. A saída
para hires envia o lote e espera GIF antes de sobrescrever essa região.
Nenhum wait necessário para propriedade de buffer foi removido: linhas ainda
não enviadas pertencem ao EE; uma região em uso por GIF só é reutilizada
após DmaSyncGIF. End mantém seu wait e a invalidação de textura da apresentação.

As chains diretas e de math possuem caches separados, cada uma com variantes
com/sem paleta. Alternar entre elas deixa de reconstruir a chain e chamar
FlushCache(0) em toda fronteira de sprites. O custo adicional do objeto GS é
aproximadamente 4,3 KiB; não há outro buffer grande por frame ou por scanline.
Parâmetros de math, XY e paleta continuam atualizados, não congelados no cache.

Uma sonda privada simulou a política de lotes sobre linhas efetivamente
renderizadas, comparando CGRAM e encerrando no fim de cada frame. A sonda
não está no código de produção:

| Trecho / janela do replay | Linhas | Envios antigos | Envios após política de lotes |
|---|---:|---:|---:|
| Mapa, f=4.440 | 26.880 | 26.880 | 3.360 |
| Mapa, f=4.800 | 26.880 | 26.880 | 3.360 |
| Nuvens, f=7.440 / 8.400 / 8.640 / 9.600 (cada janela) | 26.880 | 26.880 | 7.560 |

Isso projeta 224 → 28 envios/frame no mapa e, em média, 224 → 63 nas nuvens.
O protocolo compilado é conferido separadamente abaixo; não se trata de
medição de kicks ou ciclos no hardware físico. Nas nuvens, além de agrupar
linhas, as elegíveis deixam de executar sete primitivas de math por linha.

## Verificação

- 25 executáveis host existentes: PASS; ROM Lab self-test: PASS.
- Fixture nova: 96 cenas com fontes CGADSUB ausentes, clipping, brightness,
  TS e native/pseudo hires. Referência com math incapaz de contribuir: PASS.
- Trials: 10.800 frames; Star: 7.200; seleção/rolagem Mode 5: 4.200.
  Estados e vídeo iguais aos traces anteriores; 185 hashes acumulados PCM
  do mixer completo iguais, a 32.000 Hz. Esses são testes do core portátil.
- Execução do backend compilado dos ELFs PS2 anterior/novo: PASS para 136
  linhas normais e 48 casos hires. Confere as quatro alterações de altura
  (TRXREG, IMAGE/REF QWC, UV e XYZ), payload de cada linha, lotes 1..8,
  paleta cheia/parcial, source/Y, math, fades, cache e transições com hires.
  Chains e payloads de math/hires permanecem iguais. Nas chains diretas,
  só endereço de staging e dimensões do lote diferem da referência escalar.
- Essa bancada usa Unicorn/pyelftools. Os escritores GSList inalterados,
  syscalls DMA/cache, instruções de controle/slots e EE MULT/MOVZ são
  modelados. Não executa o GS, não mede cache físico, sincronização real,
  apresentação, consumo de áudio do frontend ou FPS. É uma checagem do
  protocolo emitido e da propriedade de buffers, não um PS2 completo.
- Builds PS2 normal (SNES_DIAGNOSTICS=0) e diagnóstico 1: PASS. Revisão do
  diff: sem sondas privadas, flags por ROM ou mudanças no áudio/timing.

Experimentos privados de fetch com grupos de oito, pares e atualização
sequencial não ficaram no código: nas 224 linhas amostradas dos workloads,
o código MIPS gerado ficou mais caro que o fetch de quatro já existente.
Não foi introduzido assembly adicional nem uma implementação de outro core.

A validação de gráficos, áudio, frame time e FPS no PS2 físico continua sendo
a próxima verificação. Por isso as mudanças ficam na branch/PR de revisão,
e não são integradas à main só porque compilam e passam na bancada.
