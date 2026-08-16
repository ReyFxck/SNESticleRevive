# SNESticle Revive PS2 v1.0.4

Changelog acumulado da versão 1.0.4, comparado com a tag **v1.0.3**.

Data deste pacote de teste: **13 de agosto de 2026**

Versão exibida pelo programa: **SNESticle Revive PS2 v1.0.4**

> Esta é uma source de teste. A compilação foi validada, mas modos de vídeo,
> CDFS e diferentes dispositivos ainda precisam de confirmação em PS2 real e
> nos emuladores usados pela comunidade antes de uma release ser marcada como
> final.

---

## Destaques

- Expandido o core **SuperFX/GSU**, incluindo conjunto de instruções, pipeline,
  cache de código, acesso à ROM/RAM e caminho gráfico `PLOT`/`RPIX`.
- O backend de vídeo foi reduzido aos modos entrelaçados **480i** e **1080i**;
  os caminhos instáveis de 240p/288p e 480p foram removidos do GS e do menu.
- Corrigida a causa de corrupção e flicker da **Issue #19**: texturas do
  emulador não usam mais endereços fixos que podiam sobrepor um framebuffer
  físico de 640x480.
- 480i e 1080i compartilham framebuffer 640x480, fonte em 2x e o mesmo caminho
  de overscan/widescreen, reduzindo estados especiais no renderer.
- Adicionados perfis de cor SNES **Original** e **Composite**, selecionáveis e
  salvos nas configurações.
- Removido o limite prático de **255/256 itens** do navegador e do CDFS de ISO.
- A listagem de diretórios passou a usar os registros retornados pelos drivers,
  evitando uma consulta `stat` separada para cada ROM.
- Corrigida a regressão em que pastas CDFS apareciam na lista, mas não abriam.
- Pastas agora aparecem como **`> NOME/`**, com marcador e barra sempre visíveis.
- Capas Libretro agora incluem **boxart, título, snap e logo**, com download
  automático opcional por `COVER=y` na criação da ISO.
- SRAM de SNES e NES agora fica separada em `SNESticle/SNES/` e
  `SNESticle/NES/`, mantendo leitura e migração segura dos saves antigos.
- Finalizados a SRAM de bateria e os **save states de cartuchos NES**, incluindo
  CPU, PPU, áudio, CHR RAM e estado privado dos mappers.
- Substituído o sintetizador base do **NES/2A03** por
  **Nes_Snd_Emu + Blip_Buffer**: os cinco canais recebem cada escrita no ciclo
  correto, sem perder ataques/notas curtas entre quadros.
- A saída do NES agora nasce diretamente em 32 kHz, eliminando o antigo bloco
  PCM de 44,1 kHz e a segunda reamostragem que podia segurar ou estourar notas.
- Reset e load state calculam também a scanline inclusiva até o próximo VSync,
  evitando encurtar o primeiro bloco de áudio depois da retomada.
- Corrigido o congelamento introduzido pela r7 ao abrir ROMs NES que consultam
  o status do APU no primeiro ciclo de uma janela de áudio.
- Corrigido o segundo congelamento da r7/r8 no PS2: `Nes_Apu` e `Blip_Buffer`
  agora são construídos explicitamente, sem depender de `.init_array` no
  startup antigo do PS2SDK.
- Restaurado o acesso a pendrives/HDs USB ao iniciar pela ISO: a stack usa
  `usbd_mini`/FreeUsbd compatível com OPL, IRXs BDM fixados e espera limitada
  para mídias lentas terminarem de montar em `massN:`.
- Substituído o `host:` visível ao usuário por um dispositivo `smb:` de rede,
  carregado sob demanda e restrito no navegador à leitura/abertura de ROMs.
- A aba antiga de Host/NetPlay virou um configurador SMB completo e grava o
  `SMB.CNF` automaticamente, sem exigir que o usuário monte o arquivo à mão.
- O `SMB.CNF` agora possui fallback de gravação e leitura em ELF gravável,
  `mass0:`/`mass1:`/`mass:` e slots MMCE detectados, além de `mc0:`/`mc1:`.
- L2+R2 publica o menu imediatamente e agenda a SRAM dois frames depois, sem a
  antiga espera fixa de um segundo após o save.
- A música do menu continua sendo alimentada durante modais, listagem de pastas
  grandes, leitura/decode de capas, gravação de configurações e conexão SMB.
- Abertura de ROM comum, ZIP e GZ passou para leituras grandes diretas por
  `fileXio`, sem abrir o mesmo arquivo duas vezes nem zerar 8 MiB sem uso.
- Corrigida a regressão dessa troca para `fileXio`: o loader agora usa os
  flags IOP corretos e volta a abrir ROM comum, ZIP e GZ em todos os devices.
- Escritas em VRAM invalidam o cache de tilemap/caracteres do renderer, evitando
  conservar gráficos antigos depois de mapa, pausa, diálogo ou novo upload.
- Os modos OBJ retangulares 6/7 agora usam 16×32, 32×64 e 32×32 reais; a OAM
  também passa a respeitar latch, endereço alto, rotação e descarte horizontal.
  A cena reportada de Final Fight 2 segue pendente de reteste, sem alegação de
  correção apenas com a bancada host.
- Corrigidos no SuperFX o giro da janela de cache, execução em RAM `$60-$7F`,
  MMIO byte a byte e o hot loop; os diagnósticos pesados agora são opcionais.
- O player de MOD/XM foi atualizado do antigo libxmp-lite 4.5.0 para o upstream
  oficial 4.7.2; MOD aplica `DEFPAN=50` antes da carga e recebe as correções
  modernas de canal, instrumento, efeitos e fluxo do ProTracker.
- O seletor inicial de save state agora mostra sempre Auto, USB/mass, Memory
  Card, MMCE e HDD; o padrão também pode persistir no ELF gravável, USB,
  MX4SIO ou MMCE quando não há memory card.
- Corrigida a falha intermitente de áudio do boot, mais perceptível em 480i:
  o serviço era parado no fim da inicialização e só voltava quando algum core
  — frequentemente o NES — enviasse o primeiro bloco de áudio.
- O CDFS não usa mais esperas infinitas nem a cascata de 32×32 tentativas; uma
  mídia ausente ou leitura quebrada agora termina com timeout e erro real sem
  prender o menu, a música e o carregamento de ROM.
- A paleta antiga e excessivamente saturada do InfoNES foi substituída pela
  paleta NTSC 2C02 padrão do **Mesen2**, preservada em RGBA8.
- O 65816 e o controlador DMA/HDMA foram auditados contra o **MesenCE/Mesen2** e os
  5,12 milhões de vetores do SingleStepTests; CPU, interrupções, pilha, wrap e
  timing de HDMA receberam correções independentes do renderer de sprites.
- Os latches de scroll BG/Mode 7 agora seguem o S-PPU; a auditoria de `OBSEL`
  preserva explicitamente seu endereço efetivo, e o blender evita cópias
  inteiras da CLUT quando nenhuma — ou somente poucas — cores mudaram.
- DMA modo 4 para `$2116-$2119` agora mantém a ordem imediata entre endereço e
  dados de VRAM, impedindo que uploads intercalados usem um `VMADDR` antigo.

---

## Revisão r29: Top Gear — ganhos confirmados e renderer estável

- O cache OBJ de 8 KiB foi substituído por um cache físico de CHR 4bpp,
  indexado diretamente pela VRAM. Os carros que reutilizam a mesma arte não
  relêem nem decodificam os quatro planos em cada scanline; escritas comuns,
  DMA, reset e load state invalidam os tiles físicos atingidos.
- A produção de áudio agora segue o tempo emulado: em 32 kHz / 60 Hz usa a
  sequência exata **532, 532, 536** amostras. Um frame lento deixa de encontrar
  o ring vazio e misturar aproximadamente o dobro no frame seguinte, removendo
  o ciclo de realimentação que ampliava as quedas.
- O cache BG fica desligado por padrão. No log r28 ele tinha 100% de hits, mas
  ainda custava mais no EE; desligá-lo reduziu o bloco BG CHR em cerca de 8% e
  trouxe aproximadamente 2% de ganho total na corrida cheia.
- O Mode 7 experimental da r28 foi retirado: na abertura giratória ele ficou
  cerca de 5% mais lento que o caminho anterior. Nenhuma alteração especulativa
  em HDMA/PPU Sync entrou nesta revisão.
- Builds normais usam `SNES_DIAGNOSTICS=0`, `SNES_OBJ_CACHE=1` e
  `SNES_BG_CACHE=0`. A bancada cobre OAM/VRAM, OBJ, cache CHR e o agendamento
  de áudio; os quatro testes host-side passam.
- Removidos os antigos `DEBUG_BOOT_SCREEN` e `MAINLOOP_DEBUG_GS_TEST`: a tela
  BIOS já não imprimia conteúdo e o teste vermelho do GS era apenas um gancho
  temporário. As ferramentas de diagnóstico SNES, profiler e DSP-4 permanecem.

---

## Revisão r24: recuperação de estabilidade após a r23

- O reteste da r23 mostrou uma regressão severa no EE: Top Gear caiu para
  aproximadamente 76% da velocidade, com uso do EE próximo de 97,5%, além de
  oscilações percebidas desde a abertura do homebrew.
- Foram retiradas as duas experiências introduzidas somente na r23: o cache
  OBJ de 48 KiB e o caminho Mode 7 totalmente desenrolado. O renderer volta
  exatamente aos caminhos da r22, que haviam sido confirmados como estáveis.
- O cache OBJ seguro de 8 KiB da r22 continua ativo. Diagnósticos, profiler e
  capturas DSP-4 permanecem desligados por padrão nas builds release.
- A otimização de Top Gear voltará em testes A/B separados, uma alteração por
  build, para medir cache OBJ e Mode 7 independentemente antes de qualquer
  nova inclusão na versão principal.

---

## Revisão r23 (retirada): cache OBJ ampliado e Mode 7 em uma passagem

- O reteste da r22 mostrou por que o cache de 8 KiB quase não ajudava o grid
  cheio: eram apenas **21.180 hits para 132.900 misses** por janela, com zero
  refresh de VRAM. A arte estava estável; as 512 entradas diretas é que se
  expulsavam continuamente. O cache agora possui um slot exato para cada uma
  das 4096 combinações tabela/tile/linha de OBJ, sem hash nem colisões.
- A linha fica guardada sem paleta e na orientação normal. H-flip apenas
  inverte os oito bytes já decodificados, portanto carros com outra paleta ou
  espelhados reutilizam o mesmo slot. Os quatro bytes-fonte continuam sendo
  conferidos em todo acesso; os 48 KiB não sacrificam correção em animação,
  DMA, mudança de `OBSEL`, pause ou load state.
- O efeito giratório antes do Start foi isolado separadamente: ele muda para
  Mode 7 e faz HDMA de matriz/paleta em cada scanline, levando o fetch Mode 7
  sozinho a cerca de 39% do tempo medido. O novo caminho conserva em
  registrador o último tile do mapa e produz pixels, prioridade e opacidade em
  uma única passagem de blocos de oito, evitando releitura do mapa e a antiga
  varredura posterior de 256 pixels.
- Uma regressão host compara pixels e máscaras do Mode 7 novo com o algoritmo
  anterior em **4.120** combinações de repetição, tile 0, backdrop, EXTBG,
  matrizes e coordenadas dentro/fora da área. A bancada OBJ também percorre os
  4096 índices para provar que não existe colisão lógica. O ganho final e os
  60 FPS ainda dependem do reteste desta ISO no PS2/emulador.

---

## Revisão r22: cache seguro de sprites para Top Gear

- O log de **Top Gear** isolou a queda da largada cheia no fetch de OBJ: com
  todos os carros, o renderer decodificava cerca de 154 mil linhas de tiles
  por segundo, contra 37 mil quando o grid esvaziava, embora OAM, HDMA e IRQs
  da tela dividida continuassem praticamente iguais.
- O renderer agora mantém um cache direto de 8 KiB para linhas 4bpp de OBJ.
  Ele guarda pixels sem paleta, permitindo que vários carros reutilizem a
  mesma arte com cores diferentes. Cada acesso ainda compara os quatro bytes
  originais da VRAM, impedindo sprites antigos depois de animação, DMA, mapa,
  pause ou load state.
- A ordem de fetch, prioridade, H/V-flip e limites reais de 32 OBJ/34 tiles
  não foram alterados. `SNES_OBJ_CACHE=0` permite comparação A/B, e
  `SNES_DIAGNOSTICS=1` registra hit/miss/refresh para medir o ganho no PS2.
- A bancada host cobre cold miss, hit, H-flip separado, troca de VRAM, paleta
  independente e o maior endereço possível. Top Gear 1/2 ainda dependem do
  reteste na largada cheia antes de afirmar 60 FPS estáveis.

---

## Revisão r21: ordem de endereço/dados na DMA de VRAM

- O log profundo de First Samurai isolou transferências frequentes em DMA modo
  4 com `BBAD=$16`: cada grupo escreve `$2116`, `$2117`, `$2118` e `$2119`, ou
  seja, troca o endereço da VRAM e grava uma palavra nesse novo endereço.
- Antes da DMA, a fila por scanline da CPU era sincronizada corretamente. Já
  dentro da transferência, os bytes de `$2116/$2117` voltavam para essa fila,
  enquanto `$2118/$2119` usavam o caminho rápido imediato. Com isso, os dados
  ultrapassavam o endereço e eram gravados no `VMADDR` anterior, explicando o
  tilemap em mosaico com sprites e HUD ainda reconhecíveis.
- Escritas MDMA destinadas aos registradores PPU `$2100-$213F` agora são
  aplicadas imediatamente e na ordem da transferência. Portas APU/WRAM e o
  caminho HDMA permanecem inalterados.
- A bancada host ganhou uma regressão específica de modo 4 que alterna dois
  endereços e duas palavras: confirma os dados em `$1234` e `$5678`, preserva
  a posição antiga e verifica o `VMADDR` final. Testes de PPU/OBJ passam e os
  ELFs EE de release e diagnóstico profundo foram compilados. A correção ainda
  depende do reteste visual no NetherSX2/PS2 antes de ser marcada confirmada.

---

## Revisão r20: First Samurai/Final Fight 3 e custo global EE/GS

- Corrigida uma divergência estrutural nos ports `$210D-$2114`. Scroll
  horizontal usa bits 3-7 do latch H/V e bits 0-2 de um latch horizontal
  separado; scroll vertical usa o latch H/V completo, e ambos são limitados a
  10 bits. O código anterior montava todo write como um par baixo/alto simples.
  Em jogos que alimentam scroll por HDMA isso pode escolher outro trecho do
  tilemap a cada scanline, produzindo justamente a fragmentação em faixas
  observada em First Samurai e Final Fight 3.
- `$210D/$210E` atualizam também scrolls Mode 7 independentes de 13 bits. O
  latch Mode 7 passa a ser compartilhado corretamente com `$211B-$2120`
  (matriz e centro), em vez de cada registrador manter seu próprio byte
  anterior. Os bits não implementados de `BGNBA` também deixam de alterar a
  base de caracteres.
- O endereço OBJ de `OBSEL` foi tornado explícito: o índice permanece com 8
  bits e seu bit 8 seleciona o deslocamento completo `$1000-$4000`; a base usa
  todos os três bits de `OBSEL.0-2`. Isso mantém o endereço efetivo da revisão
  anterior e evita somar a segunda tabela duas vezes durante a auditoria; não
  é apresentado como uma correção visual. O `OBSEL=62` do log também não é
  tratado como causa comprovada de First Samurai.
- A proteção contra a corrida entre CPU e GIF-DMA foi preservada. A área
  estável do scratchpad continua separando o produtor EE do consumidor GIF,
  porém a CLUT só é enviada quando CGRAM realmente muda e uma vez no começo de
  cada quadro. Na chain completa, a cópia normal cai de 1792 para 768 bytes;
  se HDMA altera uma única cor, são 772 bytes em vez de 1792. Uma carga
  completa ainda usa um `memcpy`, e o upload integral exigido pelo layout CSM1
  do GS é preservado.
- Scanlines sem nenhum alvo em `CGADSUB`, sem clipping da main screen e com
  brilho 15/15 agora usam uma chain GS direta: só main+paleta são enviados e
  um único primitivo grava o resultado final. Nesse caso exato, sub/atributos
  não podem alterar a imagem e deixam de ser compostos ou copiados; o staging
  dinâmico cai de 768 para 256 bytes. Janela de cor, add/sub, half-color, fade
  e force blank continuam obrigatoriamente na chain completa.
- Com brilho SNES em 15/15, o passe final do GS era exatamente uma
  multiplicação por 1. A chain comum preserva o mesmo estado final, mas deixa
  de emitir o primitivo desse passe; ela só é reconstruída quando um fade
  cruza 15/15. Brilhos menores continuam rasterizando o passe original, sem
  aproximação de cor.
- O renderer não busca tiles de BG/OBJ que não estejam habilitados nem na main
  nem na sub screen. Quando a sub screen seleciona fixed color — ou quando
  `CGADSUB` não possui alvo algum — ela é descartada antes do fetch. Isso
  remove trabalho invisível de EE sem mudar prioridade, janela ou pixels dos
  estados que realmente usam esses layers.
- As duas CLUTs de atributos, constantes durante toda a execução e guardadas
  numa faixa exclusiva de VRAM, deixam de ser reenviadas a cada quadro. Elas
  são carregadas uma vez na primeira entrada do renderer SNES.
- `SNES_DIAGNOSTICS=1` agora é o relatório geral de CPU/PPU/GS de menor
  impacto. Hashes de OAM/VRAM/CGRAM, validação de staging, captura detalhada
  de OBJ/DMA e contadores por instrução do GSU ficam em
  `SNES_DIAGNOSTICS=2`, destinado a capturas curtas. O relatório geral também
  separa bytes HDMA de scroll/CGRAM/janela-cor e registra os latches de scroll.
- A bancada host cobre os quatro valores de name-select de `OBSEL`, sequências
  H/V intercaladas, máscara de 10 bits, scroll Mode 7 e seu latch compartilhado;
  testes de OBJ e equivalência OAM/VRAM passam. Builds EE de release,
  diagnóstico geral e diagnóstico profundo também foram validados. First
  Samurai e Final Fight 3 ainda precisam do reteste visual no NetherSX2/PS2
  antes de marcar a cena como confirmada.

---

## Revisão r19: CPU 65816 completa, IRQ/NMI e DMA/HDMA alinhados

- A bancada `tools/cputest` agora importa estado final, RAM e quantidade de
  ciclos dos **512 arquivos** do `SingleStepTests/65816`: 10.000 casos por
  opcode em emulação e 10.000 em modo nativo, totalizando **5.120.000**.
  Todos os **5.080.000 casos que não são block-move** passam sem divergência.
  `MVN/MVP` continuam deliberadamente byte a byte, como no Mesen, para que uma
  interrupção possa ocorrer entre bytes; a bancada própria valida a sequência,
  PC e sete ciclos por byte.
- Corrigidos o registrador Direct Page em emulação, wrap/indexação de DP,
  penalidades de página e ciclo, fetch de PC dentro do Program Bank, branches,
  operandos de 16/24 bits e o wrap físico do barramento de 24 bits.
- `ADC/SBC` decimal de 8 e 16 bits usa o mesmo resultado no core C e no ASM,
  inclusive para dígitos BCD inválidos. `BRK`, `COP`, `WDM`, entrada/saída de
  emulação e truncamento de X/Y também foram alinhados aos vetores.
- A pilha ganhou as sequências específicas de `PLB`, `PHD/PLD`, `PEA/PEI/PER`,
  `JSL/RTL` e `JSR (abs,X)`. Os últimos casos encontrados pela rodada completa
  — `PEI` em `$xxFF`, `(dp,X)` no fim da página e push de `JSR` cruzando
  `$0100` — têm testes oficiais reproduzíveis e agora passam.
- `WAI` deixa o PC no opcode seguinte e acorda mesmo com IRQ mascarada; `STP`
  mantém a CPU parada até reset. Uma NMI já capturada não é cancelada por uma
  leitura de `$4210`, e IRQ que estava mascarada entra imediatamente depois de
  `CLI`, `REP`, `PLP` ou `RTI`, sem esperar o fim da fatia de scanline.
- NMI/IRQ agora ajustam I/D e ciclos diferentes de emulação/nativo. A posição
  do H/V-IRQ inclui o atraso do comparador/pipeline, e NMI capturada durante
  MDMA espera os 24 master clocks posteriores ao DMA documentados pelo Snes9x;
  esse caso cita especificamente **Wild Guns** e Mighty Morphin Power Rangers.
- O HDMA foi refeito na ordem de duas fases do Mesen: primeiro todos os dados,
  depois todos os contadores/tabelas. Canais encerrados por `00` permanecem
  parados no frame, modo 5 transfere quatro bytes, direção B→A funciona,
  endereços ficam no banco correto e leituras/custos obrigatórios de oito
  clocks não são mais omitidos. O padrão de porta B do MDMA reverso também não
  reinicia quando uma fatia termina depois do byte 1, 2 ou 3.
- As mudanças são de CPU/barramento e não reabrem o renderer OBJ já testado.
  **Final Fight 2 e Wild Guns ainda precisam de confirmação visual no
  NetherSX2/PS2**; esta revisão não declara os dois resolvidos sem esse reteste.

---

## Revisão r18: wrap real do 65816 e build normal sem diagnóstico residual

- A comparação com o Mesen e uma bancada diferencial descartou os dois
  suspeitos anteriores: o DMA de 544 bytes chega idêntico da WRAM à OAM e o
  caminho em bloco de `$2118/$2119` produz a mesma VRAM, endereço e latch que
  as escritas alternadas byte a byte em todos os modos de `VMAIN` testados.
- Foi encontrada uma diferença reproduzível no 65816: endereços efetivos que
  carregavam além de `$FF:FFFF` acessavam a página extra vazia do core, embora
  o barramento físico de 24 bits deva voltar para `$00:0000`. A página extra de
  64 KiB agora espelha os oito descritores do banco `$00`, inclusive MMIO e
  writes presos em trap, que recebem o endereço já limitado a 24 bits.
- Os casos de `LDA abs,X` e `LDA abs,Y` que cruzam o limite passaram de falhas
  reproduzíveis para **10.000/10.000** vetores oficiais em cada opcode. Uma
  rodada adicional de 440 mil vetores nativos de load/store deixou apenas os
  casos já conhecidos do executor C quando a própria instrução começa em
  `$FFFD/$FFFE`; o núcleo ASM do PS2 preserva o banco do PC nesse caminho.
- `tools/cputest` ganhou uma bancada host para o espelho de leitura, escrita e
  MMIO de 24 bits, além do adaptador para os vetores oficiais do 65816.
- O `Makefile` agora guarda o modo de compilação. Ao voltar de
  `SNES_DIAGNOSTICS=1` ou `PROFILE=1` para um `make iso` normal, todos os
  objetos afetados são recompilados com os contadores desligados. Builds
  normais consecutivas continuam incrementais.
- Esta é uma correção de CPU comprovada e um candidato novo para os tiles de
  personagens corrompidos. A cena de Final Fight 2 continua **pendente de
  confirmação visual no NetherSX2/PS2**; não é anunciada como resolvida sem o
  reteste da ROM.

---

## Revisão r17: sprites estáveis e DMA mais rápido

- O blender do GS não entrega mais ao GIF-DMA o mesmo `BlendInfo` que a CPU
  reutiliza para montar a scanline seguinte. Cada linha é copiada para uma
  área estável do scratchpad depois do `DmaSyncGIF`; CPU e GS continuam
  sobrepostos, mas o DMA não mistura pedaços de duas linhas. Esse era um
  candidato direto para os personagens fragmentados de Final Fight 2.
- DMA modo 1 para `$2118/$2119`, caminho comum dos uploads de tiles e sprites,
  grava rajadas lineares diretamente na VRAM e invalida o cache uma vez por
  bloco. O fallback mantém remapeamento, incremento e byte final ímpar.
- A atualização completa da OAM também virou uma operação em bloco, preservando
  latch da tabela baixa, espelho da tabela alta e rotação de prioridade sem
  pagar uma chamada e invalidação por byte.
- A fonte de DMA que cruza `$xx:FFFF -> $xx:0000` é reunida no mesmo bloco para
  não reiniciar no meio da transferência a sequência de portas B do modo DMA.
- OBJ com flip horizontal passa a ser buscado da esquerda para a direita, como
  no hardware; o flip seleciona a coluna de tile espelhada. Assim o corte no
  limite de 34 tiles descarta o lado correto.
- `DecodeBGInfo` começa zerado, impedindo campos não usados dos modos de BG de
  habilitarem camadas fantasmas e trabalho aleatório.
- A bancada host cobre OAM em bloco, VRAM linear/com wrap/com byte ímpar,
  fallback de incremento e seleção de coluna OBJ normal/espelhada. A cena e o
  FPS de Final Fight 2 ainda exigem reteste no PS2/NetherSX2.

---

## Revisão r16: somente 480i e 1080i

- Removidos da tela Video Config os modos `240p/288p (CRT)` e `480p`. A opção
  de vídeo agora alterna diretamente entre `480i (default)` e `1080i`.
- Eliminados do backend os dois casos progressivos, incluindo framebuffer
  256x240, `GS_FRAME`, modo DTV 480p, letterbox `16:9 Safe` e parâmetros VCK
  exclusivos. O primeiro boot também solicita interlace desde `GS_InitGraph`.
- O atlas dilatado exclusivo de CRT/240p foi removido; ambos os modos restantes
  usam a fonte original com escala inteira 2x sobre o framebuffer 640x480.
- Os IDs persistidos de 480i (`1`) e 1080i (`3`) foram preservados para não
  quebrar `video.cfg`. Configurações antigas com ID 0 (240p/288p), ID 2 (480p)
  ou um valor inválido caem automaticamente no 480i seguro.
- 480i continua sendo o padrão. 1080i mantém o viewport centralizado 1280x960
  em 4:3 e as opções de offset, overscan e widescreen continuam disponíveis.

---

## Revisão r15: hotfix de ROM, PPU/OAM e SuperFX test 5

### ROM comum, ZIP e GZ voltam a abrir sem perder a carga rápida

- A revisão r14 trocou `fopen`/`fread` por chamadas diretas a `fileXio`, mas
  passou `O_RDONLY` da newlib. Na EE esse valor é `0`; o IOP/iomanX exige
  `FIO_O_RDONLY`, cujo valor é `1`. Drivers como o CDFS rejeitavam a abertura
  antes do primeiro byte e, como os três formatos compartilham esse caminho,
  nenhuma ROM era iniciada.
- As duas entradas diretas — ROM sem compressão e o slurp usado por ZIP/GZ —
  usam agora `FIO_O_RDONLY` e `FIO_SEEK_*` de `io_common.h`.
- A leitura grande foi preservada. O servidor `fileXio` continua dividindo o
  pedido em blocos de 16 KiB no IOP, portanto o conserto não reintroduz os RPCs
  pequenos nem a abertura duplicada que deixavam o lançamento lento.

### Upload novo de VRAM não reutiliza tilemap antigo

- `SnesPPURender::UpdateVRAM()` estava vazio. O renderer podia manter as 33
  entradas de tile já decodificadas quando um jogo sobrescrevia o tilemap no
  mesmo endereço e sem alterar scroll/base.
- Toda escrita pelos ports `$2118/$2119`, inclusive DMA, agora agenda uma única
  invalidação de `BGSCR|BGCHR` para a próxima scanline. Uma rajada inteira só
  refaz o cache uma vez; o caminho OBJ continua lendo a VRAM diretamente.
- Isso ataca a classe observada ao voltar do mapa de Super Metroid e após
  caixas de diálogo/overlays. O caminho OBJ lê VRAM diretamente e recebe uma
  correção independente abaixo; ambos os grupos ainda devem ser retestados no
  PS2.

### OBJ retangular e avaliação de OAM seguem as regras do hardware

- A tabela antiga representava tamanho por um único shift e deixava os modos
  `OBSEL.5-7 = 6/7` como `??`, configurados por engano como 8×8 nos dois bits
  de seleção. Esses modos são retangulares no hardware e aparecem justamente
  em jogos com sprites grandes, incluindo Final Fight 2.
- O renderer agora guarda largura e altura separadas. Os pares corretos são
  16×32/32×64 no modo 6 e 16×32/32×32 no modo 7; seleção de scanline usa a
  altura e a busca horizontal usa a largura.
- O flip vertical também segue a peculiaridade do SNES para `H = 2×W`: cada
  metade quadrada é invertida dentro de si, em vez de espelhar o retângulo
  inteiro.
- O primeiro reteste visual mostrou que essa correção de tamanho, isoladamente,
  não resolve a cena reportada de Final Fight 2. A investigação encontrou mais
  diferenças objetivas no caminho usado para alimentar e selecionar os OBJ.
- `$2102` preserva agora o bit alto escrito por `$2103`. Na tabela OAM baixa, o
  byte par fica no latch e o par só é gravado quando chega o byte ímpar; a
  tabela alta grava imediatamente e espelha seus 32 bytes por `$200-$3FF`.
  Leituras/escritas que avançam o endereço também atualizam o primeiro sprite
  quando a rotação de prioridade está ligada.
- Sprites completamente à esquerda deixam de consumir uma das 32 entradas da
  scanline. A contagem de 34 tiles também exclui o tile que termina em `x=-1`
  e preserva a exceção do hardware em `OBJ X=256`, que conta mesmo invisível.
- A bancada `tools/pputest` valida tamanhos, flip, limites horizontais, latch,
  espelhamento e rotação de prioridade contra bsnes/Snes9x. Os testes host
  passam; Final Fight 2 permanece **pendente de confirmação visual** no
  PS2/NetherSX2 e não é anunciado como corrigido nesta revisão.

### SuperFX: cache correto, RAM executável completa e menos custo no EE

- A janela `$3100-$32FF` agora aplica a rotação documentada somando os nove bits
  baixos de `CBR`. Com `CBR=$C3A0`, o byte lógico zero é acessado pela CPU em
  `$3160`; o sentido anterior colocava o programa enviado ao cache no lugar
  errado.
- PBR/ROMBR tratam toda a faixa `$60-$7F` como Game Pak RAM, com espelhamento
  pelo tamanho físico. A implementação anterior reconhecia somente `$70/$71`,
  deixando o Mario Chip 1 sem executar alguns blocos carregados pelo 65816.
- Escritas nos pares R0-R15 preservam o outro byte do próprio registrador, em
  vez de compartilhar um latch global. O byte alto do SFR também é gravável e
  o cache só é limpo na transição real `GO=1 -> GO=0`.
- `FMULT/LMULT` aceita R4 como destino conforme o hardware. O `Step()` foi
  incorporado ao loop `Run()`, removendo uma chamada C++ por instrução; o
  benchmark host do caminho sintético melhorou cerca de 15% sem aumentar o
  objeto gerado.
- `SNDBG_LOG` deixa de ficar forçado em toda build. O padrão normal é zero;
  `make SNES_DIAGNOSTICS=1` recompila o relatório geral e o nível 2 habilita
  os contadores profundos quando uma captura detalhada for necessária.
- A bancada host passa com os diagnósticos ligados e desligados: 17.960 vetores
  aritméticos, além de pipeline, MMIO, cache rotacionado, PBR em `$60`, RAM,
  branches e `PLOT/RPIX`, todos sem falhas.

---

## Revisão r14: carga de ROM, MOD/libxmp e destinos de save state

### ROMs não fazem mais I/O duplicado

- `_MainLoopExecuteFile` abria toda ROM uma vez apenas para confirmar a
  existência e fechava o arquivo; em seguida o loader abria e lia tudo outra
  vez. A abertura de teste foi removida e o erro real de leitura passou a ser
  a confirmação única.
- ROMs sem compressão usam uma chamada grande de `fileXioRead`. O servidor
  `fileXio` do IOP divide esse pedido internamente e transfere os blocos por
  DMA, evitando a sequência de RPCs pequenos criada pelo buffer de `fread`.
- ZIP e GZ ainda são descompactados inteiramente na EE, mas o arquivo
  comprimido agora é medido e lido com `fileXioOpen/Lseek/Read`, sem a cadeia
  `fopen/fseek/ftell/fseek/fread` do stdio.
- Foi removido o `memset` de 8 MiB feito antes de todo lançamento. O parser já
  recebe o tamanho exato; somente uma guarda de até 1 KiB após o fim real é
  zerada para manter seguros os antigos acessos alinhados de look-ahead.
- A mudança vale para CDFS, USB/MX4SIO, memory card, MMCE, HDD/PFS e SMB porque
  todos esses caminhos passam pelo mesmo `fileXio` registrado no frontend.

### MOD atualizado para o libxmp-lite oficial 4.7.2

- O snapshot de 2021 do fork PS2, baseado no libxmp-lite 4.5.0, foi substituído
  pela source oficial `libxmp-lite-4.7.2.tar.gz`, tag/commit
  `a13276d27feabcf9ee4f982913f718ee05a65cb7`.
- SHA-256 do arquivo importado:
  `bace7f53248a2cd5adcf77f9402a8858fc0fec05f4e6d6436e3d2a28d68f640e`.
- O Makefile passou a compilar também `misc.c`, `flow.c`, `filetype.c` e
  `rng.c`, mantendo `LIBXMP_CORE_PLAYER` para a configuração embarcada.
- Entre as correções acumuladas do upstream está a regressão introduzida no
  4.5.0 em que `xmp_start_player` não desmutava canais normais, além de várias
  correções de instrumento, pan, pattern loop e compatibilidade de tracker.
- Antes de carregar MOD/XM, o frontend define
  `XMP_PLAYER_DEFPAN=50`, exatamente na ordem recomendada pelo upstream. Isso
  reduz o hard-pan dos MODs clássicos e evita que um downmix mono ruim de
  TV/HDMI pareça apagar instrumentos. O XM continua usando seu pan próprio.
- Esta revisão mexe somente na música tracker do menu. O áudio emulado de SNES
  e NES não foi alterado.

### Todos os destinos aparecem e o padrão não depende de memory card

- O seletor da primeira combinação **L2 + Cross** não filtra mais o menu pelo
  hardware detectado naquele instante. Ele sempre mostra **Auto**, **USB / mass**,
  **Memory Card**, **MMCE** e **Internal HDD**.
- Assim é possível escolher como padrão uma mídia removível ausente. Se ela
  continuar ausente ao salvar, o usuário recebe o erro normal do destino; a
  preferência não é trocada silenciosamente.
- `state.cfg` continua lendo primeiro as cópias antigas em
  `mc0:/SNESticle` e `mc1:/SNESticle`. Sem card gravável, usa a pasta do ELF
  quando ela for local e gravável, depois `mass0:`, `mass1:`, o alias `mass:`
  e os slots MMCE habilitados/detectados.
- Quando o ELF veio de uma ISO mas a ROM foi aberta por outro device local, o
  caminho exato dessa ROM também entra no fallback. Isso inclui `mass2+` e a
  partição APA/PFS atual; a preferência é relida assim que essa ROM abre.
- O driver MX4SIO configurado é carregado antes da leitura de `state.cfg`, de
  modo que o fallback em `massN:` exista no momento correto.
- `cdfs:` e `smb:` continuam estritamente fora dessa lista: são origens de ROM
  somente leitura, não destinos de configuração/state.
- **Ask Save Location Again** remove a cópia ativa e os fallbacks locais
  conhecidos, para que a próxima gravação realmente peça a escolha outra vez.

### README: DEV9 do NetherSX2

- Foi adicionada uma seção específica para NetherSX2 2.2n+: ativar
  **Enable DEV9 Ethernet**, escolher **API = Sockets** e **Device = WiFi**
  numa LAN normal (ou VPN/SIM conforme a rota usada pelo Android).
- Para SMB por IP numérico, DNS1/DNS2 permanecem em **Auto / 0.0.0.0**. Os
  presets manuais de DNS são para servidores de jogos PS2 e não substituem o
  endereço do PC/NAS configurado na aba SMB do SNESticle.
- Se **API** ou **Device** aparecer como **Não definido**, o PS2 virtual não
  possui link de rede e o SMB termina em timeout/erro de conexão.

---

## Revisão de responsividade r13: L2+R2, BGM durante I/O e fallback SMB

### L2+R2 não espera mais o memory card nem a playlist

- A pausa grande ao sair de um jogo SNES tinha três custos empilhados antes de
  `_bMenu` ser ligado: a primeira chamada de `BgmNext()` podia varrer todas as
  pastas de música, a SRAM era escrita de forma síncrona e o sucesso ainda
  abria um modal bloqueante de 60 frames.
- `_MenuEnable(TRUE)` agora marca e arma o menu imediatamente, preserva o
  decoder já carregado e apenas agenda o save quando a SRAM está suja.
- O trabalho pendente espera duas telas completas. Assim o menu e a mensagem
  `Saving SRAM...` aparecem antes de qualquer RPC do memory card.
- O sucesso/erro usa status não bloqueante. Foi removida a espera artificial de
  um segundo que não aumentava a segurança do arquivo.
- A verificação forçada de checksum continua no momento do L2+R2, portanto uma
  escrita do jogo ocorrida dentro da janela normal de 30 frames não é perdida.
- A criação de `SNESticle/SNES` ou `SNESticle/NES`, a migração do save antigo e
  a confirmação explícita antes de formatar um card continuam inalteradas.

### Música continua tocando enquanto a UI espera I/O

- O ring do `audsrv` guarda aproximadamente 50 ms; qualquer `fopen`, `dread`,
  `fwrite` ou decode de PNG mais demorado fazia o tracker ficar sem produtor.
  Além disso, `MainLoopRender()` não chamava `BgmUpdate()` enquanto um modal
  estivesse na tela, então até mensagens sem I/O silenciavam a música.
- Foi criado um helper EE exclusivo para escopos de I/O do menu. Ele recebe o
  contexto libxmp por um semáforo, sintetiza somente a faixa que já está na
  memória e alimenta o `audsrv`; nunca abre arquivos, varre dispositivos nem
  tenta carregar a próxima faixa.
- O helper acorda somente na transição para uma operação lenta e dorme de forma
  real no kernel ao terminar. Não existe polling, timer ou custo dessa thread
  durante SNES/NES gameplay.
- A proteção foi aplicada à enumeração e ordenação de diretórios em todos os
  devices, montagem/espera de USB, índice e decode de capas, `video.cfg`,
  `state.cfg`, configuração/conexão SMB e save pendente de SRAM.
- Modais agora desenham o menu ao fundo e chamam o BGM normalmente. O status é
  desenhado depois da tela, corrigindo também a ordem que escondia mensagens.
- Alterar **Menu Music Frequency** reinicia o player libxmp já carregado na nova
  taxa, sem liberar o módulo e relê-lo do CD/USB/memory card.

### `SMB.CNF` usa todos os fallbacks locais compatíveis

- **Save & Connect** primeiro atualiza uma configuração própria já carregada;
  para um arquivo novo tenta `mc0:/SNESticle`, `mc1:/SNESticle`, o diretório do
  ELF quando gravável, `mass0:`, `mass1:`, o alias `mass:` e os slots MMCE
  habilitados que responderam ao PING.
- MX4SIO entra pelos mesmos namespaces `massN:`. Um ELF iniciado numa partição
  APA/PFS do HDD pode gravar ao lado dele após o mapeamento `hdd0:` → `pfs0:`.
- A carga segue a mesma família de caminhos. Arquivos próprios graváveis têm
  prioridade sobre o `SMB.CNF` compartilhado de wLaunchELF e sobre a cópia
  somente leitura da ISO, portanto o fallback recém-salvo é realmente usado na
  reconexão e no próximo boot em que o respectivo device estiver habilitado.
- `mc?:/SYS-CONF/SMB.CNF` continua aceito para compatibilidade, mas nunca é
  sobrescrito pelo configurador.

---

## Revisão de estabilidade r12: boot 480i, CDFS e configuração SMB

### Áudio não depende mais de abrir um jogo NES

- Encontrada a ligação entre os sintomas aparentemente separados. No fim do
  boot, `Aud_Clearbuff()` chamava `audsrv_stop_audio()`, que deixa o player do
  IOP parado. O `Aud_Play()` legado, porém, era uma função vazia.
- O resultado dependia do tempo da inicialização: a música do menu e o SNES
  podiam ficar mudos, enquanto abrir um jogo NES parecia “consertar” tudo
  porque seu primeiro bloco PCM chamava `audsrv_play_audio()` por acaso. O
  480i apenas tornava essa ordem mais fácil de reproduzir; não era defeito da
  resolução em si.
- `Aud_Play()` agora reinicia de verdade o audsrv com um bloco silencioso
  mínimo e o backend acompanha explicitamente os estados iniciado/parado.
- O stop desnecessário do fim do boot foi removido. `Aud_Pause()`,
  `Aud_Clearbuff()` e o primeiro enqueue continuam coerentes caso algum caminho
  futuro precise parar e retomar o serviço.
- O conserto não altera o volume, o mixer, o SPC700, o 2A03 nem a frequência
  dos jogos; corrige somente o ciclo de vida do backend de áudio.

### CDFS deixa de congelar o restante do homebrew

- O fork do driver ainda herdava do PS2SDK chamadas bloqueantes
  `sceCdDiskReady(0)`/`sceCdSync(0)`, 32 tentativas externas e até 32
  tentativas internas. Uma ISO ruim, mídia ausente ou comando CDVD perdido
  podia manter a EE esperando I/O por tempo indefinido; nesse intervalo ela
  também deixava de alimentar o audsrv, dando a impressão de defeito musical.
- As esperas foram substituídas por polling não bloqueante com prazo definido.
  Uma leitura que excede o limite recebe `sceCdBreak()`, tem uma janela curta
  para encerrar e retorna erro; somente duas tentativas limitadas são feitas.
- `read()` não copia mais um setor velho/zerado nem informa o tamanho pedido
  quando a leitura falha. Agora devolve `EIO`, invalida o cache e impede que o
  carregador trate uma ROM corrompida como válida.
- `dread()` distingue fim normal de diretório de erro de mídia. `getstat()`
  agora segue a convenção ioman (`0` no sucesso, `-ENOENT` quando ausente) e
  nunca preenche o resultado com uma estrutura não inicializada.
- A leitura do volume descriptor valida tanto o comando quanto a presença de
  ISO9660/Joliet. O tamanho copiado de nomes também é limitado antes do
  terminador, removendo uma escrita fora do buffer no IOP.
- O `cdfs_stream.irx` recompilado possui **11.969 bytes** e SHA-256
  `f0a14edceb4876130508b0c18ba7c254ccbefa284858d87bc4baebc2ca78cdef`.

### A antiga tela Host agora configura SMB

- A aba de rede do iaddis foi reaproveitada como **SMB Network**. Foram
  removidos da interface os controles antigos de hospedar/conectar NetPlay,
  que não configuravam o filesystem de ROMs.
- A tela permite editar pelo controle o IPv4 do servidor, porta 445/139, nome
  do compartilhamento, usuário e senha. O editor usa Esquerda/Direita para o
  cursor, Cima/Baixo para o caractere, Quadrado para apagar e Triângulo para
  terminar.
- **Save & Connect** valida os campos, cria automaticamente
  `mc0:/SNESticle/SMB.CNF` ou usa `mc1:` como fallback, liga a opção SMB e só
  então inicializa DEV9, DHCP, `smbman`, autenticação e share.
- Sem memory card, o arquivo pode ser gravado ao lado de um ELF iniciado por
  um dispositivo gravável. A tela nunca sobrescreve o `SMB.CNF` global de
  `mc?:/SYS-CONF` usado por outros homebrews.
- A configuração própria em `mc?:/SNESticle` passou a ter prioridade sobre um
  arquivo empacotado ao lado do ELF/na ISO, permitindo corrigir IP ou senha sem
  remontar o disco.
- Entrar na aba é seguro e preguiçoso: ela apenas lê os campos. Rede e DHCP
  continuam fora do boot e só são acionados pelo comando explícito de conexão.

---

## Revisão de rede r11: `host:` substituído por SMB somente leitura

- Identificada a finalidade do `host:` original de iaddis: ele era a ponte de
  desenvolvimento do **ps2link/ps2client HostFS**, usada para enviar ROMs e
  módulos a partir do PC. Não era um compartilhamento de rede autônomo e
  dependia dos metadados fornecidos pelo launcher/emulador.
- Removido `host:` da lista normal de dispositivos. O fallback interno de boot
  direto/ps2link e os caminhos antigos de depuração permanecem disponíveis,
  sem serem apresentados como fonte de ROM para o usuário.
- Adicionado `smb:` como filesystem iomanX real por meio do `smbman.irx` do
  PS2SDK. O driver devolve `FIO_S_IFREG` e `FIO_S_IFDIR` corretamente, evitando
  o problema em que todos os arquivos do HostFS pareciam pastas.
- O `smbman.irx` foi fixado em `irx/`, embutido no ELF e documentado com
  origem, licença e SHA-256. Nenhum IRX solto precisa acompanhar a aplicação.
- Rede, DEV9, DHCP, driver SMB, login e abertura do compartilhamento são
  iniciados somente quando o usuário escolhe `smb:`. O boot normal continua
  sem tocar na rede.
- A espera de DHCP é limitada a 15 segundos. Ausência de cabo/servidor gera
  `DHCP Timeout` em vez de congelar o homebrew indefinidamente.
- Corrigido o nome da interface da stack moderna de `sm1` para `sm0`. O SMAP
  atual do PS2SDK registra `sm0`; antes, a configuração podia ser aplicada a
  uma interface inexistente e nunca obter endereço.
- Corrigida a ordem de inicialização para a usada pelo OPL:
  `ps2dev9 → netman → NetManInit → smap → ps2ip → ps2ipInit`. Assim, os eventos
  de link do SMAP não são perdidos antes de o RPC do EE estar pronto.
- Adicionado `SMB.CNF` de compartilhamento único, aceitando IP numérico, porta,
  share, usuário, senha e tipo de senha. Também são aceitos os nomes familiares
  do wLaunchELF (`smbServer_IP`, `smbUsername`, `smbPasswordType` e outros).
- O arquivo pode ficar ao lado do ELF, no diretório `SNESticle`/`SYS-CONF` do
  memory card ou na raiz CDFS. `SMB_CONFIG=/caminho/SMB.CNF` o inclui na raiz
  de uma ISO sem imprimir credenciais no log do Makefile.
- A opção antiga **Host (PC link)** da segunda página de Video Config virou
  **SMB (Network)**, reaproveitando a mesma posição no `video.cfg` para manter
  compatibilidade. Depois de uma tentativa ela mostra erros específicos de
  configuração, rede, protocolo, autenticação, share ou navegação.
- Ao voltar para o navegador enquanto ele está na lista de dispositivos, a
  lista é atualizada; ligar/desligar SMB passa a surtir efeito sem reiniciar.
  Também foi corrigida a escrita antes do buffer ao pressionar Triângulo nessa
  raiz vazia, usada agora como uma atualização segura.
- Copy, paste e delete ficam bloqueados em todo caminho `smb:`. O frontend usa
  a rede somente para ler ROMs/ZIPs/capas; SRAM e save states continuam nos
  destinos locais já configurados. A documentação também exige um share
  somente leitura no servidor.
- Documentada a limitação do driver a **SMB1/NT1** e o risco correspondente:
  usar apenas em LAN confiável/isolada, nunca expor à internet. Emuladores
  precisam oferecer Ethernet DEV9/SMAP real; HostFS sozinho não substitui a
  interface de rede.

---

## Revisão de armazenamento r10: USB mass/exFAT ao iniciar pela ISO

- Identificada a diferença que explicava o MMCE funcionar enquanto `mass:`
  falhava: MMCE já usava um IRX fixado no projeto, mas a stack USB era retirada
  do PS2SDK instalado na máquina de quem compilava.
- O `usbd.irx` completo foi substituído pelo `usbd_mini.irx` baseado no
  **FreeUsbd** anterior à reescrita. É a variante escolhida pelo OPL para seu
  carregador BDM e restaurada no PS2SDK especificamente por regressões USB em
  hardware real.
- A ordem de carga passou a seguir o OPL: `bdm` → `bdmfs_fatfs` → `usbd_mini`
  → `usbmass_bd`.
- Os quatro IRXs USB/BDM agora ficam juntos em `irx/`, com versão, origem,
  licença e SHA-256 documentados. Assim, compilar no Debian/DroidSpaces não
  troca silenciosamente o comportamento conforme a versão local do PS2SDK.
- O `bdmfs_fatfs` fixado inclui FAT16, FAT32 e **exFAT**, além de MBR/GPT.
- Ao escolher `mass0:` ou `mass1:`, o navegador repete apenas a abertura desse
  dispositivo por até três segundos. A enumeração USB é assíncrona e o primeiro
  `dopen` podia ocorrer antes de uma mídia lenta terminar de montar.
- A espera não roda no boot, nem em CDFS, memory card, MMCE, host ou HDD; um
  `massN:` ausente volta ao navegador após o limite em vez de travar para
  sempre.
- CDFS continua no driver streaming próprio; MMCE continua no caminho SIO2 com
  PING; MX4SIO aproveita o mesmo namespace/montagem BDM. Nenhum core, áudio,
  vídeo, SRAM ou save state foi alterado nesta revisão.
- Compilação e inspeção dos IRXs foram validadas. O reconhecimento do pendrive
  exFAT ainda precisa da confirmação final em um PS2 real iniciado pela ISO.

---

## Revisão NES r9: inicialização correta do APU no PS2

- Corrigida a causa do congelamento que ainda permanecia na r8 ao abrir
  qualquer jogo de NES, antes mesmo de o primeiro quadro ser apresentado.
- O GCC atual colocava os construtores globais de `Nes_Apu` e `Blip_Buffer` em
  `.init_array`, mas o script/startup do PS2SDK usado pelo projeto só percorre
  a lista antiga `.ctors`. Assim, no PS2, os objetos ficavam apenas zerados e
  o primeiro `output()` do APU acessava ponteiros de osciladores nulos.
- Os dois objetos agora usam armazenamento estático alinhado e são construídos
  explicitamente por `InfoNES_pAPUInit()`. Eles continuam vivos entre trocas
  de ROM, sem depender de suporte implícito do runtime C++.
- A inspeção do ELF confirmou que `_GLOBAL__sub_I_pAPUSoundRegs` desapareceu e
  que `.init_array` encolheu exatamente uma entrada de quatro bytes.
- Validada também a sequência real de memória inicialmente zerada, construção
  explícita, leitura de `$4015` no ciclo zero e 600 quadros de geração/drenagem
  de áudio (320.002 amostras).
- Compilação limpa do ELF do PS2 concluída sem avisos nem erros. O conserto é
  restrito à inicialização do áudio NES; SNES, Final Fight, vídeo, navegador,
  mappers, paleta e saves não foram alterados nesta revisão.

---

## Revisão NES r8: hotfix do congelamento ao abrir jogos

- Corrigida a leitura de `$4015` exatamente no ciclo zero. Esse acesso é
  válido no NES, mas a rotina importada tentava avançar o APU até o ciclo `-1`.
- Como os `asserts` de consistência estão ativos na compilação do PS2, o tempo
  negativo encerrava a execução dentro da biblioteca; no console/emulador isso
  aparecia apenas como o homebrew congelado ao iniciar determinados jogos.
- A leitura agora usa diretamente o estado corrente quando não existe um ciclo
  anterior e conserva a ordem original dos eventos em todos os demais ciclos.
- Adicionado ao processo de validação um teste específico para leituras no
  ciclo zero, depois de escritas no mesmo ciclo e logo após `end_frame()`.
- O conserto é restrito ao APU do NES. Vídeo e áudio do SNES, Final Fight,
  mappers, paleta, navegador e saves não foram alterados nesta revisão.

---

## Revisão NES r7: áudio por ciclo e paleta 2C02

Esta revisão substitui o primeiro conserto incremental do pAPU descrito mais
abaixo. Os itens antigos permanecem no changelog como histórico da pré-release,
mas o renderer PCM antigo, suas LUTs e o conversor 44,1 → 32 kHz não fazem mais
parte do caminho executado.

### Cinco canais base sem notas perdidas

- Integrado o `Nes_Apu`/`Blip_Buffer` de Shay Green, usado como referência
  consolidada por emuladores e pelo projeto Game Music Emu.
- Escritas em `$4000-$4013`, `$4015` e `$4017` são aplicadas na posição de ciclo
  acumulada do 6502. Ataques e cortes que ocorram dentro do mesmo quadro não
  são mais reduzidos a uma única fotografia de registradores no VSync.
- Pulsos 1/2, triângulo, ruído e DPCM mantêm seus próprios timers, fases,
  envelopes, length counters, sweep, contador linear e sequenciador de quadro.
- A leitura de `$4015` consulta o APU já avançado até o ciclo da instrução,
  incluindo o término real do DPCM e os IRQs do frame counter.
- O DPCM lê diretamente o espaço do cartucho pelo callback do 6502; assim o
  endereço, loop, tamanho e último bit do sample seguem o estado real do core.
- `Blip_Buffer` gera áudio band-limited diretamente em **32.000 Hz**. A razão
  CPU/áudio mantém a cadência alternada de 533/534 samples por quadro e a saída
  continua chegando ao conversor 32 → 48 kHz do PS2 em lotes de quatro.
- O snapshot do APU foi refeito sem ponteiros: preserva canais, envelopes,
  fases, ruído, DPCM, frame counter e IRQs. States r5/r6 ainda são aceitos;
  nesses arquivos antigos os registradores são reaplicados e o áudio retoma
  com uma nova fase, pois o formato PCM anterior não possui tradução exata.
- Esta alteração é exclusiva do NES. O SPC700, mixer e áudio do SNES não foram
  modificados.

### Cores menos saturadas

- Trocada a antiga tabela FCEUX/InfoNES reduzida a RGB555 pela paleta padrão
  **NTSC 2C02 do Mesen2**, com componentes completas de 8 bits em RGBA8.
- Índices escritos na palette RAM agora são sempre mascarados para os seis bits
  existentes no hardware (`0x00-0x3F`), evitando leitura fora da tabela.
- O verde excessivamente neon observado em jogos como **Side Pocket** passa a
  usar os mesmos valores-base do Mesen2. Um único bit vermelho continua
  reservado internamente pelo InfoNES como marcador de prioridade do fundo;
  isso altera no máximo um nível de 0-255 e não muda visualmente a cor.

### Escopo ainda separado

- O núcleo novo cobre os cinco canais **base** do 2A03. VRC6, VRC7, MMC5, FDS
  e Sunsoft 5B continuam sem ligação ao mixer; jogos que realmente dependem de
  expansão podem continuar sem esses instrumentos.
- O resultado foi compilado e validado estruturalmente, mas tom, carga da EE e
  comportamento do SPU2 ainda precisam de confirmação em PS2 real/NetherSX2.

---

## Core SNES: SuperFX / GSU

- A implementação do GSU deixou de ser apenas infraestrutura mínima e passou a
  cobrir o conjunto funcional de opcodes e seus prefixos `ALT`, `TO`, `FROM` e
  `WITH`.
- Implementados ou corrigidos, entre outros:
  - branches condicionais, `LOOP`, `JMP` e `LJMP`;
  - aritmética, comparação, multiplicação, shifts e rotates;
  - leitura/escrita de ROM e Game Pak RAM;
  - `CACHE`, `GETB`, `GETBH`, `GETBL`, `MERGE`, `LM`, `SM`, `SBK` e famílias
    relacionadas;
  - caminho gráfico `COLOR`, `CMODE`, `PLOT` e `RPIX`.
- Modelado o pipeline real de um byte do GSU, incluindo delay slot quando uma
  instrução altera `R15` e o caso em que opcode e operandos ficam separados
  entre origem e destino de um salto.
- Implementados cache de código de 512 bytes, validade por linha, alinhamento
  por `CBR` e invalidação coerente.
- Implementados prefetch de ROM por `R14`, troca de banco e atualização dos
  registradores associados.
- O cache de pixels agora possui os dois buffers usados pelo chip, permitindo
  alternância entre blocos sem perder pixels pendentes.
- Corrigidos os layouts gráficos de 2/4/8 bpp e o modo objeto de 256 pixels.
- O GSU agora avança em fatias por scanline, em vez de bloquear a EE até um
  trabalho inteiro terminar; o orçamento varia conforme o clock selecionado.
- IRQ de término é propagado ao 65C816 e baixado pelo acesso correspondente ao
  registrador de status.
- Adicionado watchdog defensivo para devolver o controle ao emulador se um
  programa GSU não terminar.

### Mapeamento dos cartuchos SuperFX

- Adicionada classificação de placas **Mario Chip 1**, **GSU1** e **GSU2**.
- `Star Fox`/`Starwing` usam o mapa de RAM do Mario Chip 1; `Star Fox 2` usa o
  mapa GSU2.
- Cartuchos GSU1 recebem seus espelhos adicionais de Game Pak RAM.
- Adicionadas as visões de Program ROM de 32 KiB e 64 KiB por banco usadas pelo
  coprocessador, incluindo os espelhos altos.
- Tipos de cartucho SuperFX `13h`, `14h`, `15h` e `1Ah` passam a ser detectados
  explicitamente.
- O tamanho da Game Pak RAM usa o campo do header estendido quando disponível,
  com fallback seguro para headers antigos.
- O registrador de versão do GSU é configurado conforme a placa e preservado
  durante reset.
- A classificação padrão da ROM foi inicializada de forma determinística,
  evitando flags aleatórias em tipos de cartucho desconhecidos.

### PPU e diagnóstico do core

- Corrigida a rotação de prioridade da OAM ao escrever em `$2102/$2103`;
  desligar a rotação volta corretamente ao primeiro sprite e invalida a lista
  renderizada quando necessário.
- A bancada `superfxtest` ganhou testes para pipeline, branches, cache, VCR,
  operações aritméticas, acesso a ROM, dois buffers de pixel e modos gráficos.
- Instrumentação opcional de CPU, PPU, GSU, DMA, HDMA, APU, mixer e sprites foi
  ampliada sob `SNDBG_LOG`; continua fora do caminho normal quando o log está
  desativado.

---

## Vídeo e renderização do PS2

### Framebuffers por modo

- **240p/288p:** framebuffer físico de `256x240`, mantendo os 256 samples
  horizontais nativos do SNES/NES. Isso elimina o redimensionamento digital
  anterior de 256 para 640 pixels e reduz shimmer em rolagem horizontal.
- **480i:** framebuffer de `640x480`; as 240 linhas lógicas passam a usar escala
  vertical exata de 2x em vez de 240 para 448.
- **480p:** framebuffer completo de `640x480` progressivo.
- **1080i:** fonte de `640x480` apresentada em uma janela 4:3 de `1280x960`,
  centralizada no raster 1080i, em vez de esticar automaticamente para 16:9.
- Em console PAL, o modo CRT continua usando o raster apropriado, apresentado
  como 240p/288p na interface.

### Issue #19: corrupção, bandas e flicker em 480p

- Removido o layout fixo de VRAM que colocava `_OutTex` em `0x2400`.
- Em `640x480`, esse endereço ficava dentro da área ocupada pelo segundo
  framebuffer; a textura sobrescrevia aproximadamente as últimas linhas de
  quadros alternados, causando bandas repetidas, imagem quebrada e flicker.
- Framebuffers são reservados primeiro pelo gsKit; depois `_OutTex`, fonte,
  textura de capa e área temporária do blender são alocadas dinamicamente e
  alinhadas.
- A inicialização falha de forma controlada se as quatro regiões não couberem
  nos 4 MiB de VRAM, em vez de continuar com endereços sobrepostos.
- A textura de capa e a fonte também deixaram de depender de posições fixas.
- O framebuffer físico inteiro é limpo a cada quadro, evitando pixels antigos
  em barras, bordas e regiões fora do canvas lógico.

### Issue #26: fonte em 240p

- A fonte passa a ser desenhada em 1x no framebuffer nativo de 240p e em 2x nos
  modos superiores.
- Posição e avanço usam a mesma transformação lógica/física, mantendo textos
  centralizados e colunas alinhadas.
- O desenho dos glifos evita amostragem fracionária com `NEAREST`, reduzindo
  letras cortadas ou com largura variável.
- Após o teste em CRT mostrar que os traços horizontais de uma única scanline
  ainda pareciam cortados, `FontInit()` passou a gerar em RAM um atlas
  específico para 240p: cada pixel de tinta é repetido uma linha abaixo.
- A dilatação usa a linha vazia já existente entre os glifos e é feita uma vez
  no upload da fonte, sem duplicar centenas de primitivas por quadro.
- O atlas embarcado continua intacto; 480i, 480p e 1080i usam a versão normal,
  portanto a correção não engrossa a fonte nas resoluções que já estavam boas.
- Esta revisão visual não altera áudio, ritmo de emulação, SRAM, PPU nem lógica
  específica de jogos.

### Widescreen, overscan e offsets

- Adicionada transformação com escala e offset no nível de primitivas.
- Corrigida a aplicação de widescreen/overscan/offset durante uma
  reinicialização de vídeo no boot; antes a rotina podia retornar cedo.
- **480p widescreen** usa canvas `640x360` centralizado no framebuffer
  `640x480`, com barras pretas limpas. Isso evita o `StartX` negativo e o wrap
  para 4095 que faziam NetherSX2 identificar/renderizar a imagem incorretamente.
- Os demais modos mantêm o caminho PCRTC já usado para apresentação 16:9.
- A interface identifica o caso especial como **`16:9 Safe`**.

---

## Cores do SNES

- Corrigido um erro antigo na calibração: os valores RGB eram passados por
  valor e, portanto, o cálculo YIQ nunca alterava a paleta final.
- Adicionados dois perfis:
  - **Original:** comportamento visual das versões anteriores e padrão inicial;
  - **Composite:** aplica a calibração YIQ/brightness existente no core.
- A troca é feita ao vivo em **Video Config > SNES Colors**.
- O perfil selecionado é salvo no cartão de memória.
- Configurações v16 são migradas para v17 sem perder modo de vídeo, offsets,
  widescreen, volumes ou dispositivos; na migração, o perfil fica em Original.

---

## Áudio e desempenho do NES (InfoNES) — primeira revisão, substituída pela r7

> Histórico da primeira tentativa desta pré-release. O caminho ativo atual é
> o `Nes_Snd_Emu + Blip_Buffer` documentado na seção r7 acima.

### Canais e temporização do pAPU

- O divisor 16.16 usado pelo DPCM em 44,1 kHz foi corrigido de `265664` para
  `2659741` (`1789773 / 44100 * 65536`). O valor anterior não era uma simples
  aproximação: faltava um dígito e samples/efeitos DPCM tocavam cerca de dez
  vezes mais devagar, alterando tom e duração.
- O DAC direto de `$4011` agora mantém sua saída mesmo com o DMA de `$4015`
  desligado, como no 2A03 real.
- O último bit de cada byte DPCM deixou de ser descartado; os deltas usam os
  passos corretos de dois níveis dentro da faixa de sete bits.
- A leitura de status em `$4015` foi separada do último valor escrito. O bit 4
  agora indica o tamanho DPCM realmente restante e volta a zero no fim do
  sample, permitindo que jogos que fazem polling iniciem o próximo efeito.
- Corrigido o caso conhecido de nota dos pulsos 1/2 permanecer tocando depois
  do contador de duração acabar quando a flag `halt/loop` estava ativa.
- Escritas no período baixo dos pulsos e do ruído não recarregam mais, por
  engano, seus contadores de duração; somente o registrador alto dispara a
  nova nota.
- Os divisores dos pulsos e do triângulo passaram a usar `timer + 1`, removendo
  o pequeno desvio sistemático de afinação do caminho antigo.
- Envelopes dos pulsos e do ruído agora distinguem corretamente volume
  constante de envelope, começam em 15 ao disparar uma nota e decaem a 240 Hz.
  Antes a lógica estava invertida e os acumuladores sem sinal impediam o
  decaimento correto.
- Sweep dos dois pulsos foi movido para 120 Hz e corrigido para a diferença de
  negação entre pulse 1 e pulse 2.
- O contador linear do triângulo deixou de diminuir uma vez por sample PCM.
  Agora é recarregado/contado pelo sequenciador de quadro; isso recupera linhas
  de baixo e outros instrumentos que desapareciam em poucos milissegundos.
- O ruído usa novamente um LFSR de 15 bits com seed não nulo, taps longo/curto
  corretos e volume/envelope correto.
- Quando um canal é silenciado ou recebe período inválido, todas as amostras
  restantes do quadro são escritas com zero. O código anterior saía do laço e
  deixava dados do quadro anterior, uma causa direta de nota presa e estalo.
- Escritas nos registradores do pAPU recebem timestamp do relógio acumulado do
  6502. O valor de overshoot de `K6502_Step()` não é mais confundido com tempo
  de quadro, evitando deslocar ataques e cortes de nota para o começo do bloco.
- A fila de eventos ganhou limite defensivo para impedir sobrescrita de memória
  em ROMs que escrevam nos registradores de áudio em excesso.

### Mixer e saída do PS2

- Os cinco canais base deixaram de ser somados com o mesmo peso e um centro DC
  fixo. O frontend usa duas tabelas pré-calculadas com as curvas não lineares
  de **pulse** e **triangle/noise/DPCM** do 2A03.
- Um bloqueador DC simples remove o offset do DAC sem o salto artificial que
  causava estouros em entradas/saídas de som.
- O conversor 44,1 kHz → taxa do mixer mantém posição 32.32 e a última amostra
  entre quadros. Ele não repete mais a borda nem reinicia a interpolação a cada
  VSync.
- A razão fracionária 44.100 → 32.000 produz a cadência correta de 533/534
  samples por quadro (média exata de 32 kHz), em vez de truncar sempre para
  533 e tocar lentamente com pequenas descontinuidades.
- A entrega ao `AudMixBuffer` usa lotes múltiplos de quatro, compatíveis com o
  conversor 32 → 48 kHz e sem perder a amostra ímpar em `Flush()`.
- Histórico do filtro/resampler é zerado ao resetar ROM ou carregar state. A
  imagem do pAPU passou à versão 2, mas states NES v1 desta pré-release ainda
  são aceitos e têm o DAC antigo convertido.
- Todas essas mudanças ficam no core/caminho do **NES**; mixer SPC700 e áudio
  do SNES não foram alterados.

### Custo e limites desta rodada

- Envelope, sweep e contadores deixaram os laços de 735 amostras e passam a
  rodar apenas nas quatro/duas batidas necessárias por quadro. A mistura usa
  LUTs e não faz divisões no caminho normal por sample.
- Cenas pesadas ainda podem ultrapassar 16,6 ms por causa de CPU/PPU/mappers e
  precisam de perfil/teste em PS2 real; esta rodada remove desperdício do
  áudio, mas não promete 60 fps em toda ROM.
- Foram corrigidos os cinco canais **base** do 2A03. Áudio de expansão VRC6,
  VRC7, MMC5, FDS e Sunsoft 5B continua sendo uma etapa separada; portanto uma
  ROM japonesa que dependa desses chips ainda pode ter instrumentos ausentes.

---

## SRAM e save states de SNES/NES

### Organização e compatibilidade da SRAM

- A SRAM dentro do save do cartão de memória passou a ser separada por sistema:
  - `mc0:/SNESticle/SNES/<rom>.srm` para SNES;
  - `mc0:/SNESticle/NES/<rom>.srm` para NES.
- A pasta principal `mc0:/SNESticle/` continua contendo ícone, configurações e
  bancos de save state; as subpastas `SNES` e `NES` são exclusivas para SRAM.
- Saves antigos de SNES em `mc0:/SNESticle/<rom>.srm` continuam compatíveis. Se
  não existir o arquivo novo, o emulador lê o antigo, marca uma migração e grava
  uma cópia em `SNES/` na próxima abertura do menu. O original não é apagado.
- A criação das subpastas é feita sob demanda e volta a funcionar após troca de
  cartão, sem depender de um cache global que confundia SNES e NES.
- O checksum da SRAM agora é inicializado também quando ainda não existe arquivo,
  evitando herdar o estado sujo do jogo carregado anteriormente.

### SRAM de bateria do NES

- `NesSystem::GetSRAMBytes()` e `GetSRAMData()` foram implementados.
- ROMs iNES com a flag de bateria expõem os 8 KiB completos da SRAM do InfoNES;
  ROMs sem bateria não criam um `.srm` desnecessário.
- A SRAM é zerada ao inserir outro cartucho, antes da leitura do save, impedindo
  que bytes do jogo anterior vazem para a nova ROM.
- Trainers iNES de 512 bytes são copiados para `$7000-$71ff`, como exige o
  formato, sem interferir na restauração posterior da SRAM.

### Save state do NES

- O gerenciador existente passou a aceitar cartuchos `.nes` nos mesmos cinco
  slots e nos destinos Auto, USB, memory card, MMCE e HDD interno.
- Estados NES usam bancos `.n1a/.n1b` até `.n5a/.n5b`; estados SNES preservam
  os nomes `.s1a/.s1b` existentes. No cartão, ambos permanecem diretamente em
  `mcN:/SNESticle/`, conforme a opção de destino já existente.
- O navegador de manutenção reconhece os bancos `.nNa/.nNb`, remove o par de
  segurança ao apagar um slot e oculta as novas pastas de SRAM.
- `NesStateT` deixou de ser o placeholder de 64 KiB e agora serializa:
  - registradores, interrupções e clocks do 6502;
  - RAM, SRAM, PPU RAM, OAM, registradores, scroll, scanline e paleta;
  - CHR RAM e cache decodificado de padrões;
  - estado do pAPU, incluindo envelopes, fases, contadores e DPCM;
  - RAM, registradores, latches e contadores de IRQ privados de todos os mappers
    compilados no InfoNES;
  - bancos PRG/CHR/SRAM ativos e bases do renderer.
- Nenhum endereço cru é salvo: cada ponteiro de banco vira uma referência de
  região + offset e é validado/reconstruído ao carregar. Assim, o estado não
  depende do endereço em que ROM e buffers foram alocados após reiniciar.
- O container externo mantém versão, identidade/CRC da ROM, CRC do payload,
  compressão deflate e os dois bancos resistentes a queda de energia. Um ID de
  sistema adicionado ao campo reservado mantém compatibilidade com estados SNES
  v1 já existentes.
- Estados FDS continuam fora deste suporte; o menu informa que o recurso atual
  é destinado a cartuchos iNES.

---

## Capas de SNES e NES

### Formatos, tipos e navegação

- Mantido o suporte à PNG simples com o mesmo nome base da ROM e aos extras
  manuais `Game-1.png` até `Game-9.png`.
- O layout Libretro passa a reconhecer os quatro diretórios disponíveis para
  SNES e NES: `Named_Boxarts`, `Named_Titles`, `Named_Snaps` e `Named_Logos`.
- O botão □ alterna apenas as imagens existentes, nesta ordem: imagem simples,
  boxart, título, snap, logo e extras numerados.
- A lista de nomes de PNG agora cresce sob demanda, começando em 256 entradas e
  podendo chegar a 16.384. Isso evita que coleções com vários tipos por ROM
  sejam cortadas pelo antigo limite fixo de 2.048 nomes.
- O cache de imagens e o índice continuam sendo liberados antes de iniciar uma
  ROM, devolvendo a memória ao core.
- O índice em RAM agora é ordenado uma vez e consultado por busca binária, em
  vez de comparar cada nome pedido com todas as PNGs várias vezes por frame.
- O prefetch de capas vizinhas ganhou atraso inicial e intervalo entre
  decodificações, evitando várias leituras/decodificações pesadas em frames
  consecutivos.
- As pastas `Named_Boxarts`, `Named_Titles`, `Named_Snaps`, `Named_Logos` e o
  arquivo `COVERS.IDX` agora ficam escondidos da lista de jogos.

### Download automático pelo Makefile

- Adicionado `COVER=y` e seu equivalente minúsculo `cover=y` ao alvo `make
  iso`. `COVER=n` permanece como padrão e não acessa a internet.
- Quando ativado, o build consulta as coleções Libretro oficiais de SNES e NES
  e adiciona os quatro tipos de PNG apenas à árvore temporária `cdfs:/ROMS/`.
  A ROM e sua pasta original não são modificadas.
- O sistema é detectado pelas extensões SNES/NES. Arquivos ZIP são inspecionados
  quando possível; `COVER_SYSTEM=snes` ou `COVER_SYSTEM=nes` permite resolver
  arquivos compactados ambíguos.
- A correspondência tenta o nome exato, a substituição Libretro de caracteres
  inválidos por `_`, remoção de tags GoodTools, abreviações `(U)/(E)/(J)/(W)` e
  o nome interno de uma ROM única em ZIP.
- Capas válidas que já existem são preservadas. Ausências, nomes não
  reconhecidos e falhas de rede são resumidos sem impedir a criação da ISO.
- O downloader gera um `COVERS.IDX` compacto em cada pasta de ROMs. Em CDFS e
  outros dispositivos lentos, o emulador lê esse arquivo sequencial uma vez e
  evita enumerar separadamente os quatro diretórios `Named_*`; layouts manuais
  sem índice continuam usando o fallback compatível.
- Adicionado `make covers ROMS=/pasta`, que cria o mesmo layout `Named_*`
  diretamente em uma pasta destinada a USB, MX4SIO, MMCE, HDD, memory card ou
  ao diretório exportado por SMB, sem precisar gerar ISO.
- `COVER_JOBS` controla o paralelismo e `COVER_BASE_URL` permite espelho ou
  teste local do downloader.

### Documentação

- A seção de capas do README foi reescrita em inglês com exemplos completos de
  nomes para `.sfc`, `.nes` e `.zip`.
- Adicionados os links separados das coleções Libretro de SNES e NES, uma
  tabela explicando cada `Named_*`, exemplos de várias imagens para a mesma ROM
  e instruções para CDFS e todos os demais dispositivos.
- Documentado explicitamente que a automação adiciona PNGs, mas nunca injeta
  arquivos dentro da ROM, renomeia o jogo ou altera seus bytes.

---

## Navegador, dispositivos e CDFS

### Velocidade e compatibilidade entre dispositivos

- CDFS, USB/mass, cartões de memória, `smb:`, MMCE e PFS/HDD usam
  `fileXioDopen/fileXioDread` como caminho comum de enumeração.
- O registro de diretório já contém nome, modo e tamanho; ROMs reconhecidas não
  geram um `stat` ou seek óptico adicional por item.
- Tipos de arquivo retornados por drivers ioman antigos são consumidos no
  formato `FIO_S_*` já normalizado pelo iomanX.
- Corrigida a regressão da primeira otimização CDFS: o navegador aplicava
  novamente a conversão antiga `FIO_SO_*`, classificando `ROMS` e `BGM` como
  arquivos. Agora essas entradas voltam a abrir como diretórios.
- Drivers de terceiros que não informam tipo recebem fallback por caminho
  completo (`getstat` e, se necessário, uma tentativa segura de `dopen`).
- O fallback só ocorre para tipo desconhecido; não reduz a velocidade da lista
  normal de ROMs.

### Mais de 256 ROMs em ISO

- O array do navegador no EE agora cresce geometricamente: 256 é apenas a
  reserva inicial, não um limite.
- O limite real passa a ser a memória disponível, com tratamento de falha de
  alocação e sem escrever além do buffer.
- O driver CDFS embutido é um fork do CDFS do PS2SDK que transmite registros
  ISO9660/Joliet por uma janela pequena de setores.
- Foi removida do caminho `dopen/dread` a tabela fixa `TocEntry[256]` do driver
  padrão, permitindo listar pastas grandes sem truncar a partir do item 256.
- O novo `cdfs_stream.irx` é embutido no ELF, carregado no boot e reinicializado
  corretamente após reset do IOP.

### Interface do navegador

- Pastas são renderizadas como **`> NOME/`**.
- O marcador `>` e a barra final `/` ficam fixos; apenas o nome intermediário
  recebe reticências ou marquee quando ultrapassa a largura disponível.
- A alteração é apenas visual e não modifica o nome enviado a `Chdir()` ou ao
  carregador de ROM.
- A quantidade de linhas visíveis é calculada a partir da altura real da fonte
  e termina antes do rodapé.
- O texto da lista não ultrapassa mais a barra inferior em diretórios grandes.
- Arquivos PNG usados como capas continuam escondidos da lista de ROMs.

---

## Interface e rodapé

- Removido o endereço IP do rodapé; essa informação já existe na tela de
  configuração de rede.
- Restaurada a faixa inferior em verde-azulado escuro inspirada no visual
  original do iaddis.
- O rodapé é desenhado depois da tela ativa, impedindo que itens do navegador o
  cubram.
- O rodapé mantém a versão do GCC à esquerda e a versão do programa alinhada à
  direita.
- Os nomes dos modos de vídeo foram atualizados para descrever o sinal usado:
  `240p/288p (CRT)`, `480i (default)`, `480p` e `1080i`.

---

## Build e código-fonte

- `cdfs_stream.irx`, seu código-fonte e licença estão incluídos na árvore.
- O Makefile verifica e embute o novo IRX automaticamente.
- `tools/fetch_libretro_covers.py` implementa a preparação opcional de capas
  sem dependências Python externas.
- As regiões de vídeo deixaram de depender de constantes de endereço entre
  modos.
- O pacote ZIP preserva a pasta raiz `SNESticleRevive/` e exclui `.git`,
  resultados de build e ELFs gerados.
- A partir deste pacote de teste, a entrega solicitada é **source + changelog**;
  nenhum ELF pré-compilado acompanha os downloads enviados nesta conversa.

### Validação realizada neste ambiente

- Compilação limpa da source extraída do ZIP com o toolchain PS2DEV: **161
  arquivos compilados**.
- Resultado desta source: **161/161 arquivos**, **0 erros** e **0 avisos**.
- Confirmado no ELF final que `smbman.irx` foi realmente embutido, incluindo
  o identificador de protocolo `NT LM 0.12`; o binário fixado também foi
  conferido contra o SHA-256 documentado.
- O relógio DPCM foi conferido contra `1789773 / 44100`: o passo corrigido
  resulta em `40,5844269` ciclos/sample, contra `40,5844218` exatos; o antigo
  resultava em apenas `4,0537109`.
- Uma simulação de 600 quadros do resampler contínuo gerou **320.000 samples**
  em 10 segundos (32.000 Hz exatos), sem lote fora de múltiplo de quatro e sem
  acumular amostras pendentes.
- O downloader foi validado com uma coleção local: uma ROM SNES no formato
  GoodTools `(U) [!]` e uma ROM NES dentro de ZIP encontraram os nomes Libretro
  correspondentes e produziram **8/8 imagens** nas quatro categorias.
- Uma segunda execução de `make covers` reconheceu as oito PNGs existentes e
  não sobrescreveu nenhuma delas.
- O novo índice foi validado com quatro tipos para uma ROM GoodTools: gerou um
  `COVERS.IDX` de quatro entradas, e uma segunda execução preservou todas as
  PNGs existentes enquanto atualizava o índice.
- A preparação de `cdfs:/ROMS/` foi verificada com `COVER=n` sobre uma pasta já
  preparada: ROM, quatro diretórios `Named_*` e `COVERS.IDX` foram copiados
  integralmente para a árvore da ISO.
- `COVER=y` e `cover=y` foram testados na preparação de `cdfs:/ROMS/`: as PNGs
  foram colocadas somente na árvore temporária da ISO e a pasta original
  permaneceu com zero PNGs.
- A imagem ISO final não foi produzida neste ambiente porque não há
  `mkisofs`/`genisoimage`/`xorriso`; a etapa completa anterior ao gerador de
  ISO, incluindo `SYSTEM.CNF`, ROMs e capas CDFS, foi validada.
- O ZIP da source é testado com `unzip -t`.
- O pacote é extraído em uma pasta temporária e recompilado para confirmar que
  não depende do diretório de trabalho original.

---

## Pontos que ainda exigem teste comunitário

- Confirmar 480i em NTSC/PAL, adaptadores PS2-to-HDMI e NetherSX2.
- Confirmar a proporção 4:3 e a opção widescreen em 1080i em diferentes TVs.
- Abrir `cdfs:/ROMS/`, subpastas e uma ISO com mais de 256 ROMs.
- Repetir a navegação em `mass0:`, `mass1:`, `mc0:`, `mc1:`, `smb:`, MMCE e
  partições HDD/PFS; no SMB, testar guest, usuário/senha e pastas grandes em
  um PS2 real conectado por Ethernet.
- Comparar os perfis Original e Composite em jogos com gradientes, transparência
  e tons de pele; a preferência visual continua sendo subjetiva.
- Comparar em PS2 real músicas e efeitos dos cinco canais base, incluindo
  **Double Dragon** (nota presa), **Battletoads & Double Dragon** (DPCM) e
  **Castlevania III US** (polling de `$4015`). A edição japonesa de Castlevania
  III usa VRC6 e continua fora do áudio base desta rodada.
- Deixar um jogo NES tocando por vários minutos e testar pause/reset/load state
  para confirmar ausência de estalo, deriva de tom e amostra ímpar perdida.
- Validar jogos SuperFX de placas diferentes, incluindo Star Fox/Starwing,
  títulos GSU1 e títulos GSU2. O core recebeu testes unitários, mas compatibilidade
  jogo a jogo ainda depende de testes reais.

---

## Referências técnicas e créditos desta rodada

- Projeto original e layout de interface: iaddis/SNESticle PS2.
- PS2SDK: iomanX/fileXio e base do driver CDFS.
- PS2SDK `smbman`, OPL e wLaunchELF_ISR: referência do login, abertura de share
  e ordem de inicialização da rede/SMB.
- PicoDrive PS2 de irixxxx: referência para resolução defensiva de entradas de
  diretório sem tipo conhecido.
- `fhoedemakers/pico-infonesPlus`: referência moderna do InfoNES para as
  correções de nota presa nos pulsos, DAC/status DPCM e efeitos ausentes
  (incluindo a Issue #111 daquele projeto).
- `jay-kumogata/InfoNES`: origem do pAPU integrado e base usada para comparar
  as mudanças locais do frontend PS2.
- `libgme/game-music-emu`: origem de `Nes_Snd_Emu` e `Blip_Buffer` (Shay
  Green, LGPL-2.1+), usados pela revisão r7 para os cinco canais base do 2A03.
- `SourMesen/Mesen2`: referência da paleta padrão NTSC 2C02 usada pela revisão
  r7 para remover a saturação excessiva do InfoNES.
- InfinityStation: referência anterior para limpeza de bandas e comportamento
  visual do navegador.
- Relatos das Issues #19 e #26 e testes enviados pela comunidade.
- Observações de jsr sobre escala 240p/480i/480p e amostragem horizontal.
