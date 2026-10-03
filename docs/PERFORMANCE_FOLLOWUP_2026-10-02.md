# Mode 7, MesenCE, cartões e cache gráfico

Esta rodada parte de `27007e5` da PR #97 e atende ao relato de NES em 0,8 FPS,
Mode 7 pesado, cartões inexistentes no menu e ao pedido de pelo menos 3 MiB
nos caches gráficos. A main `6c6eb4a` não foi alterada. As mudanças são gerais,
sem seleção por ROM/CRC, clocks reduzidos, efeitos omitidos ou código ares
importado. A PPU e a divisão EE/GS existentes foram mantidas.

## MesenCE: custos que foram encontrados

O subset já troca os locks desktop por operações leves no PS2. Não foi
atribuído o relato de 0,8 FPS a mutexes sem medi-los. O perfil de 3.000 frames
de um NROM original com background, controle e tom audível mostrou:

| Operação | Chamadas antes |
|---|---:|
| IsRenderingEnabled | 613.736.661 |
| ProcessScanlineImpl | 245.818.161 |
| LoadTileInfo | 196.655.728 |
| GetPixelColor | 184.320.000 |
| ProcessSpriteEvaluation | 184.320.000 |

Run era instanciado no header, em uma unidade sem os corpos dos helpers,
criando chamadas por dot. Agora é instanciado no `.cpp` que contém Exec.
Inline explícito é limitado aos quatro helpers medidos e aos pequenos
wrappers DefaultNesPpu; os getters usam o ponteiro EmuSettings já existente.
Todos os dots, fetches, avaliação de sprites, IRQs e regras de timing continuam
executando. Não foram removidas emulação nem callbacks de mapper. Run ligado
no ELF R5900 ocupa 4.304 bytes; não se forçou todo o core para dentro dele.
IsRenderingEnabled, ProcessScanlineImpl, LoadTileInfo e GetPixelColor deixam
de aparecer como chamadas separadas no perfil final dessa fixture.

A compilação NES passa de O1 para O2 e deixa de forçar BaseControlDevice para
O0. Os flags conservadores de aliasing, overflow e memória do projeto são
mantidos. Não há fast-math, LTO geral ou alteração dos flags do mixer SNES MMI.
O stamp do build invalida objetos da configuração anterior.

O mixer aplicava aritmética double e divisões de software a cada mudança de
canal. Com volume 100 e panning central, os valores de entrada têm domínio
pequeno e os ganhos de expansão são inteiros. Duas tabelas readonly de
**65.598 bytes** reproduzem os casts das fórmulas originais; entradas fora do
domínio e outros ajustes mantêm as expressões anteriores. Cada célula de delta
é limpa ao consumir seu timestamp, em vez de zerar o array inteiro no fim de
cada frame. Todo delta inserido registra seu timestamp; Reset ainda zera tudo.

No caminho normal 96→48 kHz, Hermite só visita fases inteiras 0/1. Selecionar
os endpoints produz exatamente o mesmo PCM que o polinômio, na mesma ordem
em relação ao histórico. O loop usa fase inteira e histórico int16, conservando
startup, pending, volume e blocos pequenos. Outras razões/fases continuam no
polinômio original, com diferenças divididas por 2.0, sem truncamento inteiro.

Validação:

- 1.015.808 combinações de canais base e 100.000 somas de expansão iguais às
  fórmulas double, incluindo os ganhos exatos de cada canal.
- 40.000 chamadas comparadas ao resampler anterior: chunks, taxas, volume,
  reset, pending, fill e add, com o mesmo PCM e guardas. ASan/UBSan passam.
- Core/bridge com sanitizers: NTSC/PAL, vídeo, PCM audível, replay de states,
  rollback, SRAM/CHR não volátil, reset e bancos MMC1/MMC3 passam.
- Perfil original de 3.000 frames: vídeo acumulado `6043602144408`,
  2.395.987 frames PCM e hash PCM `3677299883937154727`, iguais antes/depois.
  Tempo de CPU do processo x86 com `-pg`: 22,143268 → 14,603000 s.
  Essa diferença inclui o custo dos probes por chamada e O1→O2; **não é uma
  medição de ganho no PS2**, nem comprova a correção do caso relatado de 0,8 FPS.

A ROM NES e o ambiente daquele relato ainda não foram identificados. O perfil
original revela caminhos gerais caros, mas não substitui esse workload.

## Controles NES e cartões

Os dois ports NES passaram por todas as 256 combinações de leitura serial.
A função MapPad real passou pelas combinações, duas fases turbo e desconexão.
Na configuração padrão PS2: Cross=A, Square=B, Circle=turbo A,
Triangle=turbo B; D-pad, Start e Select ficam nos dois controles. Esses testes
não certificam acessórios, multitaps ou os drivers físicos de pad.

O menu de states e a enumeração automática agora oferecem apenas mc0/mc1.
Os IDs persistidos de mc2..7 foram conservados para não deslocar as preferências
USB/MMCE/HDD. Preferências antigas desses slots retornam aos dois ports nativos.
A fixture compila as funções reais: 16 raízes, preferências, MX4SIO-only e
consulta sem carregar módulos passam. A descoberta histórica de arquivos SMB
em aliases do SDK não cria entradas de Memory Card no menu.

## Cache gráfico com pelo menos 3 MiB

O padrão passa de 256 para 512 conjuntos de linhas de mundo BG, com as mesmas
duas vias, chaves e invalidação. As metades de um mapa vertical de 512 pixels
não colidem no mesmo conjunto. Não há busca por mais vias a cada hit.

| Estrutura | Antes | Depois (bytes) |
|---|---:|---:|
| Linhas BG completas | 786.432 | 1.572.864 |
| Linhas BG normais, segunda via | 425.984 | 851.968 |
| Seleção de vítima | 1.024 | 2.048 |
| Soma dos caches gráficos medidos | 1.957.565 | **3.171.005** |

São **3,0241 MiB**, com acréscimo de 1.213.440 bytes. Não é RAM total nem cache
físico da CPU. `SNES_BG_CACHE_LINES=256` permite A/B com a capacidade anterior.
O cache CHR já cobre a VRAM inteira; não foi preenchido com RAM sem utilidade.
Sprites grandes também são compostos de tiles, cuja decodificação já pode ser
reutilizada. Cache maior não evita atualização quando o jogo modifica esses bytes.
Mode 7 usa o fetch afim descrito a seguir, independente desse cache de linhas.
Não se atribui ganho de FPS à capacidade sem medir misses e frame time no alvo.

O ELF Mesen/RFA normal reporta text/data/bss de 4.297.995 / 639.736 /
14.421.392 bytes (19.359.123 somados). Heap de ROMs, containers, pilhas e runtime
não está incluído nesse número. Não foram reservados 3 MiB para cada cache.

## Mode 7 no EE e envios GS

A busca wrap usa um acumulador de 64 bits para x/y. O bias de x é múltiplo do
período de wrap e compensa o carry quando dx é negativo. Guards delimitam o
intervalo sem overflow entre as palavras; callers incomuns usam o caminho
escalar. Os quatro dots, cache do tilemap, precisão fracionária e arredondamento
da matriz são conservados. Uma barreira de compilador sem instruções evita a
substituição por `__muldi3`: o R5900 possui add de 64 bits, mas não DMULT.
A extração dos oito bits de máscara usa duas operações de 32 bits em vez de
uma sequência longa de shifts de 64 bits. Transparência e prioridade permanecem.

| Amostras do replay | Instruções fetch antes | Depois | Redução |
|---|---:|---:|---:|
| Mapa, 56 scanlines | 270.385 | 256.942 | 4,97% |
| Nuvens, 168 scanlines | 770.432 | 732.282 | 4,95% |

Nenhuma das 224 amostras ficou mais cara nessa contagem. A variante de unrolling
sem cache do tilemap ficou 9,7–15,5% pior e foi descartada. Essas são instruções
executadas por wrappers do fetch compilados com os flags R5900, não tempo de
frame ou comportamento de cache físico.

O caminho direto GS usa os 3 KiB disponíveis entre 8 e 11 KiB do scratchpad,
passando de oito para doze linhas por lote. TEX0 passa a descrever 16 linhas;
uma textura PSMT8 de 256×16 ocupa duas páginas de 8 KiB, dentro da reserva de
32 KiB anterior ao buffer temporário. Palette, math, brightness, source, Y,
hires e fim de frame continuam encerrando lotes. Waits de propriedade DMA são
mantidos. O máximo contínuo de 224 linhas elegíveis exige 19 envios em vez de
28; não é garantia de 32% menos tempo de frame.

- Fixtures host de fetch, rounding e 65.536 combinações de bits: PASS.
- `check_ps2_mode7.py`: 1.380 casos repeat/clamp/black do código compilado,
  limites e guardas; 256 padrões da extração de máscara: PASS. Integra-se ao CI.
- Modelo de packets do blender ligado no ELF final: 182 linhas normais e
  48 casos hires; lotes 1..12, bytes, alturas, paletas e transições: PASS.
  A normalização da referência escalar compara TRXREG, IMAGE, REF, UV/XYZ e
  a altura TEX0; math/hires continuam byte a byte iguais. DMA espera antes
  de reutilizar staging. O modelo não executa o GS ou SIF real.

## Regressão e pendências

Referência `27007e5` e candidato foram executados com o mixer completo 32 kHz:

| Workload desde reset | Frames iguais | Checkpoints acumulados PCM iguais |
|---|---:|---:|
| Trials intro | 10.800 | 90 |
| Top Gear intro/demo | 7.200 | 60 |
| Trials seleção/movimento Mode 5 | 4.200 | 35 |
| Star Ocean | 18.000 | 150 |
| Tales tr2 | 18.000 | 150 |
| Spawn intro sem entrada | 7.200 | 60 |
| Total | **65.400** | **545** |

Cada trace confere CPU, DMA, PPU, memória, SPC, DSP e vídeo por frame. PCM é
conferido a cada 120 frames. Sondas e ROMs desses replays ficaram em cópias
privadas, fora do fonte de produção e dos ELFs. Os testes PPU/SPC/áudio do CI,
4.216 casos do CPU assembly ligado e 2.000 casos de echo MMI passam. Ambos
os builds finais Mesen/RFA e Mesen/audsrv passam.

Tales já bloqueia na referência: na biblioteca, aproximadamente frame 4510,
CPU C00593..C0059D aguarda portas APU; SPC acaba em 0000/0002 e as portas ficam
27/DD/CD/02 → 09/0B/07/00. Isso ocorre também sem mixer de saída. Uma sonda
privada rastreou JMP indireto em 1662, pela tabela 1959+X, indo para E8FC e
executando dados antes do loop. A origem do índice/dados ainda precisa ser
confirmada; não se trocou valor de porta ou clock para esconder o bloqueio.
Não é possível afirmar que corresponde a todas as travadas do relato PS2.
Star chegou a 18.000 frames neste replay sem reproduzir seu crash posterior.
Igualdade de regressão preserva bugs existentes e não prova compatibilidade.

A lista de trabalho não encerrado permanece em
[PENDING_FIXES](PENDING_FIXES_2026-10-02.md): FPS/frame time no PS2, falhas de
APU/Spawn, Star/Tales, clocks completos SuperFX, chips, SMB em servidor real,
state de coprocessadores, CRT-easymode e periféricos/mappers especiais NES.
Compilar ou passar em modelos não completa esses itens. A PR permanece draft;
a main não recebeu as mudanças antes da validação necessária no alvo.
