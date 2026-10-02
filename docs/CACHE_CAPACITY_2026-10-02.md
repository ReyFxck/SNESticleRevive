# Capacidade dos caches: investigação e configuração atual

Atualização: por solicitação do usuário, o padrão agora soma **3171005 bytes,
3,0241 MiB**, duplicando os conjuntos de linhas de mundo BG de 256 para 512.
Mantém duas vias; não aumenta o número de comparações por consulta. As linhas
separadas por 256 no mapa vertical de 512 pixels deixam de disputar o mesmo
conjunto. A coerência e as chaves permanecem completas. Pode-se retornar à
capacidade anterior com `SNES_BG_CACHE_LINES=256`. O alcance de CHR já cobre
a VRAM inteira e o fetch Mode 7 continua sendo independente deste cache.
Essa capacidade foi pedida explicitamente; os probes históricos de mais vias
abaixo **não medem** o ganho da nova configuração. Regressões e limitações
estão na [rodada atual](PERFORMANCE_FOLLOWUP_2026-10-02.md).

O restante deste relatório conserva a investigação histórica na base
`287161e`, derivada da main `6c6eb4a`.
Esta etapa mede oportunidades de reutilização; não muda o tamanho dos
caches, os pixels, o áudio ou a temporização do emulador.

## Memória existente e alcance

As estruturas gráficas listadas em
[prioridades](PS2_WORK_PRIORITIES_2026-10-02.md#caches-da-build-normal-v2)
somam **1957565 bytes, 1,8669 MiB**. Não é o consumo total do emulador.
O EE tem 32 MiB de RAM; ROM, código, memória emulada e buffers também
ocupam esse espaço. Reservar RAM não aumenta o cache físico do R5900:
16 KiB de instruções, 8 KiB de dados e scratchpad separado de 16 KiB.

O CHR físico de 438 KiB já comporta todas as posições de tiles da VRAM
nos formatos 2bpp e 4bpp. Não expulsa tiles por falta de capacidade.
Escritas nos bytes correspondentes exigem nova decodificação, qualquer
que seja o tamanho reservado.

O cache de linhas BG aceita atualmente uma ou duas vias, não um tamanho
arbitrário em megabytes. A primeira entrada de cada conjunto guarda
main/sub hires; a segunda guarda apenas resolução normal. A chave inclui
geração das dependências VRAM, mapa, CHR, modo, geometria, paleta,
prioridade e posição relevante. Scroll fino X permite reutilização com
realinhamento das máscaras. Um deslocamento curto também reaproveita
células sobrepostas mesmo quando não há hit exato.

Esse cache atende os caminhos elegíveis de Mode 1/5, sem mosaic ou
offset-per-tile. O fetch afim de Mode 7 é outro caminho. Mais vias neste
cache não aceleram diretamente rotação ou escala de Mode 7.

## Método e controles

Uma cópia privada do candidato recebeu uma sonda de chaves. A cada
consulta elegível, ela simula LRU de 1, 2, 4 e 8 vias mantendo exatamente
as verificações de coerência existentes. Não fornece dados ao renderer:
somente conta hits que um cache maior poderia recuperar.

A versão simulada de duas vias reproduziu **exatamente os hits reais**
em todos os workloads abaixo. As vias adicionais compactas continuam
restritas à resolução normal. Um experimento separado também permite
2/4/8 vias completas para o par hires de Mode 5.

Workloads, mixer completo a 32 kHz:

- Trials: 10800 frames desde reset, sem entrada, incluindo a intro Mode 7.
- Top Gear: 7200 frames desde reset, sem entrada; intro e corrida automática
  em tela dividida. A imagem final foi conferida na corrida.
- Trials Mode 5: 4200 frames com o filme de entrada `choice-A.input`, que
  inclui seleção e movimento.

Os **22200 registros por frame** mantiveram os mesmos estados e hashes de
vídeo dos traces anteriores, excluindo apenas a duração medida no host.
Os **185 checkpoints de hash acumulado PCM** também permaneceram iguais.
A repetição de Mode 5 com vias completas manteve os mesmos 4200 registros
e 35 checkpoints PCM. Nenhuma sonda privada foi colocada no código de
produção ou nos ELFs entregues.

## Resultados

Contagens de consultas elegíveis e hits exatos, não de todos os pixels ou
de todas as chamadas do renderer:

| Workload / modo | Consultas | 1 via | 2 vias reais/simuladas | 4 vias | 8 vias |
|---|---:|---:|---:|---:|---:|
| Trials intro / 1 | 3579072 | 3510014 | 3515387 | 3515387 | 3515387 |
| Trials movimento / 1 | 71232 | 64276 | 64276 | 64276 | 64276 |
| Trials movimento / 5 | 1556352 | 1554784 | 1554784 | 1554784 | 1554784 |
| Top Gear intro/demo / 1 | 2824357 | 1135372 | 1831803 | 1872335 | 1890632 |

Em Mode 5, permitir também vias completas para hires produziu os mesmos
1554784 hits com 2, 4 ou 8 vias. Apenas 1568 consultas já não tinham hit
no cache atual desse replay; o aumento não recuperou nenhuma delas.
Isso não certifica outras cenas Mode 5 ainda não capturadas.

Top Gear apresenta margem: quatro vias recuperariam **40532** hits e
oito recuperariam **58829**, frente a **992554** misses atuais.
Equivale a evitar **4,08%** ou **5,93%** desses misses. Com oito vias,
os hits passam de 64,86% para 66,94% das consultas elegíveis.

São oportunidades de evitar trabalho de fetch, **não percentuais de
ganho de FPS**. Parte desses misses já usa o reaproveitamento de células
sobrepostas. Uma implementação maior também paga mais comparações de
chaves, metadados, acessos ao cache físico e, nos hits, as cópias necessárias.
A sonda não mede essas despesas nem o GS/IOP físico.

## Decisão

Não reservar 5 MB como nova configuração padrão com esta evidência.
Trials não mostrou ganho de hits, e Mode 7 não usa esse cache de linhas.
Top Gear justifica avaliar um candidato pequeno e separado, com medição
no alvo antes de manter a mudança.

Estimativa dos buffers, preservando uma via completa e aumentando as
vias compactas de resolução normal:

| Configuração hipotética | Bytes das estruturas somadas | MiB |
|---|---:|---:|
| 2 vias atuais | 1957565 | 1,8669 |
| 4 vias | 2809533 | 2,6794 |
| 8 vias | 4513469 | 4,3044 |

Oito vias usariam cerca de **4,51 MB decimais** nessas estruturas; não
inclui metadados extras de substituição que uma implementação exigiria.
Quatro vias custariam mais 832 KiB e recuperariam boa parte dos hits extras
simulados. Ainda falta provar que esse benefício supera a busca adicional
no R5900. Não há novo ELF de cache ampliado nesta etapa.

## Fontes recebidos do MesenCE

O novo ZIP `MesenCE-NES-PS2-Source-MenuSafeInput-1.zip` contém fontes,
desbloqueando a análise do port NES. O `Makefile` NES-only fornecido
compilou com o toolchain PS2 disponível, sem alterações nos fontes.

- Referência informada no ZIP: MesenCE `20ba206c`, build funcional
  `7e9bbb54`, export `backup/mesen-ps2-source-export-20261002`.
- ELF standalone compilado: 2165964 bytes; SHA-256
  `caa4cb6cba147be1cae843f105ffff6f3d2ef7be79d4c92997e18adbe2a69e40`.
- Seções reportadas: text 2084474, data 13176, BSS 454220 bytes.
  Não são uma medição do heap durante uma partida.

A compilação não demonstra integração ou velocidade. O frontend do port
inicializa seu próprio GS/IOP/áudio; `Ps2Audio` ainda espera espaço via
`audsrv_wait_audio`. A substituição do infoNES precisa adaptar vídeo,
entrada, áudio, saves e ciclo de vida aos componentes existentes do
SNESticle. O port NES não substitui o core SNES. Sua integração posterior está em
[MesenCE](MESENCE_INTEGRATION_2026-10-02.md).
