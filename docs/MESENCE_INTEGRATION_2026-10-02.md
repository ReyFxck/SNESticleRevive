# Integração do port MesenCE fornecido

O ZIP `MesenCE-NES-PS2-Source-MenuSafeInput-1.zip` é a fonte desta integração.
Seu `SOURCE_INFO.txt` registra base `20ba206cef5ba207c21203176d02cb9f43dda9fb`,
build funcional `7e9bbb541c4af444201a0c131ca2565135cbb2b4` e export de 2026-10-02.
O subset está em `src/third_party/mesence`: 59 arquivos C++ de NES/utilitários
e os headers transitivos necessários. A licença GPL-3.0 e os avisos próprios
das dependências acompanham os fontes. Não há cores de outras plataformas
compilados, ROMs, executáveis do ZIP ou drivers PS2 standalone importados.

## Ligação com o frontend existente

`NES_BACKEND=mesence` é o padrão; `NES_BACKEND=infones` mantém o core e os
states antigos. O bridge C mantém C++20, RTTI e exceptions nas unidades
Mesen; o SNES e o frontend legado conservam seus flags de compilação.
Os overrides históricos de `operator new` retornavam NULL: no build Mesen
usa-se a implementação padrão para que os containers possam lançar e o
bridge possa capturar falha de alocação. Não existe um segundo dono de
pad, GS, IOP ou SPU2.

- Entrada: os dois pads do SNESticle alimentam `IInputProvider`; os botões
  turbo conservam a alternância anterior. Menu/arquivo continuam no frontend.
- Vídeo: uma tabela de 512 cores por modelo de PPU expande a saída indexada
  para o surface RGBA32 existente. A paleta é preparada ao carregar, fora do
  loop por pixel. Emphasis NTSC e paletas RGB/VS usam os dados do port.
- Áudio: o mixer Mesen emite estéreo de 48 kHz para o `CMixBuffer` existente,
  sem inicializar audsrv, RFAuds2 ou módulos por conta própria. Volumes dos
  canais foram inicializados em 100; o port começava com todos em zero.
  Providers de expansão registrados entram no mixer, mas isso não cria
  suporte aos providers/firmware desabilitados pelo port.
- Frames: PAL/Dendy são fontes de 50 Hz e NTSC de 60 Hz. A política comum
  de cadence/catch-up pode omitir a conversão/upload do frame intermediário;
  o core continua executando CPU, PPU e áudio desse frame. InfoNES conserva
  seu caminho anterior, pois não executa um frame com target NULL.
- Battery: PRG RAM normalmente é acessada sem cópia por frame. CHR RAM
  persistente usa um bundle PRG+CHR, sincronizado com o core, state e reset.
  NES 2.0 com apenas CHR não volátil recebe a alocação indicada no header;
  o port ignorava esse tamanho quando a RAM volátil era zero.
- States: snapshots usam Serializer versão 4, um payload variável `MCE1`
  com tamanho máximo de 10 MiB e a compressão/CRC do container SNESticle.
  O state NROM da fixture tem 24.004 bytes; memória é alocada ao salvar/carregar,
  não reservada como cache gráfico. States InfoNES são reconhecidos como
  incompatíveis e precisam do build legado. Um load incompleto faz rollback
  da máquina atual, em vez de misturar campos antigos e novos.
- Ciclo de vida: unload libera console, ROM, patch, snapshots e bundle de
  battery; não mantém uma ROM NES no heap após passar para SNES. Falhas do
  core não atravessam a fronteira C sem tratamento.

## Correções no subset fornecido

`DebugUtilities::GetMemoryTypeCount()` retornava 1 apesar de o port registrar
índices até 45. ASan reproduziu a escrita fora do array de memória; o tamanho
agora deriva do fim do enum. Serializer/BatteryManager também evitam memcpy
de comprimento zero com ponteiros NULL. O serializer do bridge exige campos
completos, rejeita chaves duplicadas e confere comprimentos sem overflow.
O header miniz encaminha para a implementação já usada pelo SNESticle;
não existe uma segunda implementação miniz ligada.

O bridge impede escrita automática de battery pelo Mesen: o frontend é o
dono dos arquivos `.srm`. As funções desktop Serialize/Deserialize do stub
EmulatorPs2 não são usadas como implementação de states; o bridge serializa
o console efetivo.

## Validação e limites

[mesencetest](../tools/mesencetest/README.md) compila o core real e cartridges
originais: NTSC/PAL, entrada/vídeo, PCM audível, replay exato de 12 frames,
state incompleto sem destruir a partida, PRG/CHR battery, reset e bancos
MMC1/MMC3 passaram. ASan/UBSan passam no mesmo código. Builds R5900 Mesen
com ambos os backends de áudio e a alternativa InfoNES compilam.

O footprint estático text/data/bss observado no ELF Mesen/RFA é 18.122.175
bytes; InfoNES/RFA, 16.537.897. Isso não mede o heap nem garante que qualquer
cartucho NES caiba: o core também possui cópias/dados dinâmicos da ROM.
Nenhum cache foi aumentado para 5 MB.

Não se declara todos os mappers/periféricos do Mesen completos. EEPROMs e
batteries particulares de alguns mappers, combinações de CHR volátil/não
volátil, FDS/discos, VS Dual System e acessórios ainda precisam de validação
e ligação ao frontend. O teste MMC3 verifica bancos/state, não uma suíte
completa de IRQs. A integração não certifica desempenho, vídeo, som ou
transições de menu no PS2 físico e não altera a arquitetura do SNES.
