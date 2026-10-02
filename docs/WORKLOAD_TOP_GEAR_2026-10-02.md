# Top Gear: intro e corrida

Base desta rodada: `48be878`, na branch de revisão da PR #97. A main
permanece em `6c6eb4a`. Top Gear passou a ser workload por solicitação do
usuário; nenhuma identificação de ROM participa das alterações do core.

## O que foi reproduzido

O replay sem controle, com DSP/mixer completos a 32.000 Hz, percorre a intro
e entra na corrida automática com pista em movimento. Foram comparados
7.200 frames. A intro exercita Mode 7 aproximadamente entre frames 800 e
2.360; a corrida em tela dividida passa a usar Mode 1 aproximadamente em
2.570. Portanto, otimizar somente a busca afim não resolve a corrida.

Na corrida, BG1/BG2 usam tiles grandes e BG3 também participa da tela
principal. HDMA muda o scroll por linha e as primeiras entradas da paleta.
Essas alterações de cor interrompem muitos lotes diretos do GS: não é
correto reunir linhas com paletas diferentes na mesma expansão de índices.
Não foram congelados scroll, paleta, HDMA, sprites ou pixels entre frames.

A corrida validada é a demonstração automática. A bancada portátil não
executa o frontend, o áudio IOP, a apresentação nem o GS físico. Seu tempo
de execução não foi usado para estimar FPS no PS2. Não há uma medição nova
no console nem identificação do ELF histórico que atingia 45 FPS; a causa
completa da queda relatada para 30 FPS continua sem confirmação.

## Corrigir as máscaras antes de reutilizar mais tiles

`FetchBG` já sinalizava mudanças apenas de scroll fino X com a flag legada
`FETCHPAL`. `RenderLine8` consumia apenas `FETCHCHR`. Se HDMA mantivesse a
mesma linha do mapa e mudasse o scroll fino, os índices se deslocavam,
mas as máscaras de transparência e prioridade permaneciam na posição
anterior. O bug apareceu ao comparar a reutilização com a decodificação
completamente nova, sem cache.

O renderer agora consome ambas as flags e realinha as máscaras. Não foi
modificada a interface de `FetchBG`, a temporização ou a representação dos
tiles. A fixture nova mantém o mesmo Y efetivo, varia X fino a cada linha e
compara o resultado com um decode forçado: quatro cenas Mode 1/5, com tiles
pequenos/grandes, prioridade, transparência e main/sub. A versão anterior
falha em Mode 1, linha 2; a corrigida passa.

Por essa correção, vídeo do build antigo não é usado como oráculo absoluto.
Para isolar a otimização, a referência recebeu somente a correção das
máscaras. Cache de um passo, cache ampliado e decode sem cache produzem os
mesmos 7.200 frames de Top Gear e os mesmos 60 hashes acumulados PCM.

## Reutilizar janelas de tiles sobrepostas

A reutilização horizontal passa de um para até oito grupos de oito pixels,
nos dois sentidos, inclusive nas bordas do mapa. Pelo menos 25 das 33
células permanecem. Geração de VRAM, modo, mapa, CHR, palette, prioridade,
Y e configuração continuam fazendo parte das verificações; mosaic e
offset-per-tile conservam seus caminhos existentes. Native Mode 5 mantém
as duas fases físicas. Não há crescimento dos buffers do cache.

Hits com apenas X fino realinham as máscaras sem copiar novamente os
264 bytes de CHR que já estão em `BGPlanes`. Misses e mudanças de CHR
continuam materializando/restaurando a linha necessária.

Em 38 janelas completas de 120 frames da corrida, terminando entre f=2.760
e f=7.200, a contagem do decoder foi:

| Trabalho, 4.560 frames | Referência corrigida | Cache ampliado |
|---|---:|---:|
| Linhas de células CHR buscadas | 23.295.315 | 21.877.539 |
| Hits de linha | 1.788.926 | 1.788.926 |
| Misses de linha | 972.083 | 972.083 |
| Reutilizações com scroll | 274.482 | 328.670 |

São **6,09% menos células buscadas**, com saída igual. Não são 6,09% de
redução no tempo do frame: CPU, HDMA, composição, caches e transferências
continuam tendo seus custos. A fixture de movimento cobre passos 1/8/9,
ambos os sentidos, mapas e tiles de tamanhos diferentes, wraps, X fino,
prioridades, color math e escrita de VRAM; passo 9 usa o decode comum.

## Usar o caminho direto quando color math é identidade

Parte da intro habilita color math, mas soma/subtrai fixed color preto,
sem halving, ou seleciona uma subtela inteiramente transparente que cai
nesse fixed color. O caminho completo do GS fazia trabalho cujo resultado
era a própria cor principal.

Depois de resolver as máscaras, linhas normais sem clipping principal e
com brightness 15 podem usar o caminho direto existente quando essa
identidade é demonstrada. Hires, clipping, brightness, halving, operandos
não pretos e subtelas opacas conservam a composição completa. As buscas e
o estado SNES continuam sendo processados; esta rodada não omite a busca
da subtela para deduzir a identidade.

O preto é testado após a conversão do perfil de cores, e a fallback não é
CGRAM[0]. O teste usa CGRAM[0] propositalmente não preto e confere todas as
32.768 cores SNES com o blender C independente, adição/subtração e máscaras
parciais. Há também casos negativos para os 256 bits das máscaras.

Para confirmar o comportamento SNES, foi consultado somente o DAC do ares:
[`dac.cpp`](https://github.com/ares-emulator/ares/blob/master/ares/sfc/ppu/dac.cpp),
`above`, `blend` e `fixedColor`. Subtela transparente usa fixed color e
desabilita halving nessa situação. Não foi copiada arquitetura ou código.
A condição aplicada é conservadora: exige que a máscara de halving já
resolvida esteja vazia.

## Reter os comandos de atualização parcial da paleta

O commit `d49ddca`, de 28/09, introduziu uploads parciais úteis para HDMA,
mas reconstruía seu wrapper GIF/DMA em cada envio. Na pista mudam as cores
dos mesmos grupos de CLUT, enquanto endereços e geometria permanecem.
Esse trabalho repetido está demonstrado; isso não identifica o commit como
causa da queda inteira de 45 para 30 FPS.

`BuildSparsePaletteList` retém a chain quando grupo, endereço da staging e
endereço da render chain coincidem. Dados da paleta e parâmetros continuam
sendo atualizados após o wait GIF existente. Mudar qualquer dependência
reconstrói a chain. Não há remoção de waits, reutilização de staging ainda
em DMA, buffer grande adicional, ou alteração de formato/primitivas GS.

Uma sequência de 180 linhas executada a partir dos ELFs PS2 compilados,
com grupos iguais/diferentes, paleta cheia, direct/math, fades e fronteiras
de lotes, gerou **todos os comandos e payloads idênticos**. Chamadas a
GSListBegin caíram de 184 para 24; as instruções contadas no modelo do
wrapper/frontend caíram de 207.663 para 167.725. Essas contagens não medem
cache, ciclos ou GPU no hardware físico.

Também foram capturadas linhas e paletas de cinco frames reais do replay e
repassadas aos backends compilados. A captura ficou fora do código final:

| Frame | Linhas | Diretas antes/depois | Envios antes/depois | GSListBegin antes/depois |
|---|---:|---:|---:|---:|
| Intro, 600 | 224 | 0 / 224 | 224 / 28 | 2 / 2 |
| Intro, 1.000 | 224 | 0 / 0 | 224 / 224 | 214 / 3 |
| Intro, 1.680 | 224 | 0 / 224 | 224 / 213 | 214 / 3 |
| Corrida, 3.000 | 219 | 219 / 219 | 110 / 110 | 105 / 3 |
| Corrida, 3.600 | 219 | 219 / 219 | 103 / 103 | 98 / 5 |

Quando a política de composição não mudou, comandos e payloads foram
comparados byte a byte. Quando math passou a direto, a equivalência de RGB
é sustentada pelo oráculo independente descrito acima. O GS não é executado
por essa bancada. A tabela mostra também por que mais batching sozinho
não elimina o custo da pista com alterações frequentes de paleta.

## Regressão e entrega

- 21 executáveis da suite PPU/áudio/SPC: PASS; ROM Lab self-test: PASS.
- Top Gear: 7.200 frames; Trials intro: 10.800; Star abertura: 7.200;
  seleção/rolagem Mode 5: 4.200. Estado e vídeo iguais à referência com
  máscaras corrigidas; 245 hashes acumulados do mixer completo iguais.
  Essas aberturas não validam travamentos posteriores ou jogos inteiros.
- Top Gear sem cache de linha: outros 7.200 frames iguais à referência.
- Protocolo compilado: 136 linhas normais e 48 casos hires, dimensões dos
  lotes 1..8, payloads e wait antes de sobrescrita: PASS. Wrapper de paleta
  e cinco frames de dados de raster reais: PASS nos limites acima.
- Unicorn/pyelftools modelam GSList, DMA/cache syscalls e instruções de
  controle/slots e EE MULT/MOVZ sem suporte completo no Unicorn. Isso
  verifica protocolo e propriedade de buffers, não FPS, DMA real ou GS.
- PS2 normal (`SNES_DIAGNOSTICS=0`) e diagnóstico 1: compilados. Revisão
  final sem sondas privadas, regras por ROM ou mudanças de áudio/timing.

Histórico adicional consultado: `5e0a42b` e `d53176e`, ambos de agosto,
registram experimentos/regressões de Top Gear. O cache atual é diferente
daquele renderer; não foram revertidas suas correções nem desativados
caches indiscriminadamente.

As mudanças ficam em quatro commits de código separados e um relatório,
na PR de revisão. A versão anterior acompanha a entrega para comparação
com as mesmas configurações e diagnóstico desligado. FPS, frame time,
imagem e áudio no PS2 ainda precisam ser verificados antes da integração
na main. Não há promessa de 60 FPS baseada no ROM Lab ou neste modelo.
