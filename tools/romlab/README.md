# SNESticle ROM Lab

O ROM Lab executa no PC o mesmo core portátil de SNES usado pelo
SNESticle Revive. A ideia é reproduzir bugs de lógica sem precisar gerar e
copiar um ELF para o PS2 a cada tentativa.

Ele aceita ROM, save state do próprio Revive, SRAM e um replay de controles.
Em cada quadro, cria checkpoints de CPU, PPU, DMA, WRAM, SRAM, VRAM, CGRAM,
OAM, SPC700, DSP, portas APU e SA-1. O primeiro quadro diferente aponta quais
subsistemas mudaram e pode gerar o estado, a imagem e os últimos opcodes.

Também denuncia:

- opcode da CPU 65C816, SA-1 ou SPC700 que ainda caiu no caminho
  `unimplemented`;
- acessos a memória não mapeada ou I/O ainda não tratado, agrupados pelo
  endereço mais acessado;
- possível deadlock quando PC, vídeo/PPU e portas APU ficam estáveis;
- execução não determinística, repetindo o caso duas vezes e comparando todos
  os checkpoints.

> `host-core fps` mede somente o core no computador. Não representa FPS do
> EE/GS no PS2. O ROM Lab elimina a demora de testar lógica no console; perfil
> de desempenho específico de MIPS, DMA e GS ainda precisa da build de PS2.

O framebuffer em PPM também separa as duas classes de falha: se a imagem do
ROM Lab estiver correta e a captura do PS2 estiver quebrada, o estado de
CPU/PPU chegou certo e a investigação pode ir direto para staging, GIF/DMA ou
GS. Se ambas estiverem erradas no mesmo quadro, a causa está antes disso, no
core, registradores, DMA/HDMA ou renderização portátil.

## Compilar e testar

Requer um compilador C/C++17 e zlib de desenvolvimento.

```sh
./tools/romlab/build.sh
./tools/romlab/romlab self-test
```

Uma build de desenvolvimento com AddressSanitizer e UndefinedBehaviorSanitizer
pode ser gerada com:

```sh
ROMLAB_SANITIZE=1 ./tools/romlab/build.sh
./tools/romlab/romlab-sanitize self-test
```

O script também permite builds A/B sem mudar o fonte:

```sh
ROMLAB_DIAGNOSTICS=1 ROMLAB_BUILD_TAG=diag ./tools/romlab/build.sh
ROMLAB_BG_CACHE=0 ROMLAB_BUILD_TAG=bg-off ./tools/romlab/build.sh
ROMLAB_BG_CHR_CACHE=0 ROMLAB_BUILD_TAG=chr-off ./tools/romlab/build.sh
ROMLAB_OBJ_CACHE=0 ROMLAB_BUILD_TAG=obj-off ./tools/romlab/build.sh
```

`ROMLAB_DIAGNOSTICS` aceita `0`, `1` ou `2`; o nível 2 ativa a captura
profunda. Os três caches aceitam `0` ou `1`. `ROMLAB_BUILD_TAG` conserva cada
executável e diretório de objetos sob um nome separado, evitando misturar os
resultados de uma comparação.

O autoteste não usa ROM comercial. Ele cria uma ROM mínima temporária e
valida core, framebuffer, replay, save/restore, trace e repetição
determinística.

## Rodar um caso

```sh
./tools/romlab/romlab run \
  --rom /caminho/jogo.sfc \
  --state /caminho/slot0-a.state \
  --input /caminho/cena.input \
  --frames 600 \
  --trace /tmp/cena.jsonl \
  --verify-determinism \
  --dump-dir /tmp/cena-dump
```

Sem save state, a execução começa no reset. SRAM bruta pode ser carregada com
`--sram`; se `--state` também for informado, o estado completo tem prioridade.

Formatos de estado aceitos:

- `SnesStateT` bruto criado pelo ROM Lab/Revive;
- banco `SNRSTATE` v1 do Revive, com payload bruto ou deflate;
- SRAM bruta separada.

Savestates de MesenCE, bsnes e Snes9x possuem estruturas internas diferentes e
não podem ser restaurados diretamente neste core. Para comparar emuladores, o
ponto comum é reset + o mesmo replay, ou saves correspondentes criados em cada
emulador na mesma cena.

## Replay de controles

O arquivo é esparso: o estado informado continua valendo até o próximo evento.
Quadros começam em zero. Exemplo em [`example.input`](example.input):

```text
0 NONE
30 START
31 NONE
120 RIGHT+B
180 NONE
```

Também é possível informar uma máscara hexadecimal (`0x8000`) e até cinco
controles por linha, separados por espaço ou vírgula. Valores reconhecidos:
`A`, `B`, `X`, `Y`, `L`, `R`, `UP`, `DOWN`, `LEFT`, `RIGHT`, `START`, `SELECT`,
`NONE` e `DISCONNECTED`.

## Encontrar a primeira regressão

Grave uma referência antes da alteração:

```sh
./tools/romlab/romlab run --rom jogo.sfc --state cena.state \
  --input cena.input --frames 900 --trace antes.jsonl
```

Depois da alteração, compare durante a própria execução:

```sh
./tools/romlab/romlab run --rom jogo.sfc --state cena.state \
  --input cena.input --frames 900 --reference antes.jsonl \
  --dump-dir divergencia
```

Ou compare dois traces já prontos:

```sh
./tools/romlab/romlab diff antes.jsonl depois.jsonl
```

O dump da primeira divergência contém:

- `divergence-NNNNNN.state`: checkpoint bruto para reiniciar perto do erro;
- `divergence-NNNNNN.ppm`: framebuffer exato daquele quadro;
- `divergence-NNNNNN-opcodes.txt`: últimas instruções de CPU, SA-1 e SPC,
  junto dos acessos não tratados mais recentes.

Quando `--dump-dir` é usado sem ocorrer divergência, o mesmo conjunto é salvo
como `final-NNNNNN.*`. Isso permite mandar somente ROM + estado e já receber a
tela, o checkpoint e o caminho de execução no fim da captura.

`--checkpoint arquivo.state` grava o contêiner `SNRSTATE` comprimido e
compatível com o slot 1 do Revive no PS2. Os estados dos dumps são brutos para
serem pequenos e rápidos de gerar, mas o ROM Lab consegue recarregar ambos.

A lista de endereços não tratados é uma lista de candidatos, não uma sentença:
software de SNES usa open bus de propósito em alguns casos. Frequência alta,
loop no mesmo PC ou diferença contra uma referência tornam o candidato muito
mais forte.

## Códigos de saída

| Código | Significado |
|---:|---|
| 0 | execução terminou sem divergência |
| 1 | ROM/estado/arquivo inválido ou falha de I/O |
| 2 | divergência contra o trace de referência |
| 3 | a repetição determinística divergiu |
| 4 | o jogo executou opcode de CPU, SA-1 ou SPC700 ainda não implementado |

ROMs, SRAMs e saves de usuário devem permanecer fora do Git. O ROM Lab não
envia esses arquivos para nenhum serviço.
