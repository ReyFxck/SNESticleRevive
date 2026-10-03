# Medidor v2 e remoção de stores redundantes no compositor

Base: `c897f4d`, branch de revisão da PR #97. Main permanece em `6c6eb4a`.
Esta rodada conclui as edições interrompidas pela perda de conexão do
executor. FPS/diagnóstico no menu ficam para uma etapa posterior.

## O que as fotos mostram

Foi possível ler os campos principais nas fotos 2592–2595; parte do rodapé
estava fora da abertura de vídeo. O medidor anterior confundia o clock dos
timers de barramento com o CP0 Count do R5900. Isso dividia FPS por dois e
multiplicava ms por dois. A correção abaixo apenas corrige unidades; não
acelera a emulação.

Leitura aproximada com a escala corrigida, no teste NetherSX2 do usuário:

| Foto/cena | FPS fonte | WORK ms | CORE ms | PPU ms | OBJ ms | MIX ms | IOP ms |
|---|---:|---:|---:|---:|---:|---:|---:|
| 2592, título | 29,8 | 17,9 | 17,55 | 9,5 | 1,75 | 1,9 | 0,25 |
| 2593, largada com carros | 29,8 | 20,05 | 19,7 | 10,1 | 3,05 | 1,75 | 0,25 |
| 2594, em movimento | 41,8 | 17,05 | 16,65 | 6,8 | 1,55 | 2,15 | 0,25 |
| 2595, carro inferior parado | 39,8 | 17,2 | 16,85 | 7,0 | 1,1 | 2,05 | 0,25 |

São médias de janelas diferentes da leitura instantânea do overlay do
NetherSX2, não medições em um PS2 físico. As configurações do emulador PS2
também influenciam o custo executado; manter clock/cycle skip padrão é
necessário para comparar com o hardware alvo. Há arredondamento a 0,1 ms
no painel antigo antes da correção, logo a tabela não tem precisão de
centésimos de ms apesar de algumas divisões terminarem em 0,05.

OBJ responde por aproximadamente 16–30% da PPU nessas quatro janelas.
PPU responde por aproximadamente 40–54% do core. GIF wait mostrado fica
próximo de zero; IOP é pequeno. O restante do core não pode ser chamado
automaticamente de CPU pura: também contém DMA/HDMA, sincronização e
outros trabalhos. É preciso separar os estágios restantes da PPU, em vez
de atribuir a cena inteira a sprites ou áudio. WORK entre 17 e 20 ms fica
perto/acima do orçamento de 16,7 ms; perder uma janela de apresentação
pode explicar parte dos saltos entre taxas, mas não autoriza remover waits
que protegem buffers ou a ordem dos comandos.

## Corrigir as unidades do contador

`profclock.h` fornece uma constante compartilhada de **294.912.000 Hz**,
usada pelo HUD, orçamento/capacidade dos diagnósticos e profiler legado.
Não muda clocks, CPU SNES, IRQ, NMI, HDMA, áudio, frameskip ou apresentação.
Os logs antigos conservam contagens e proporções úteis; suas capacidades
absolutas e percentuais normalizados pelo orçamento estavam incorretos.

Referência do PS2: [PS2tek, EE COP0 Timer](https://psi-rockin.github.io/ps2tek/#eecop0timer)
documenta que Count incrementa a cada ciclo do EE. Foi conferido também
`pcsx2/COP0.cpp`, onde Count recebe o delta de `cpuRegs.cycle`. O PS2SDK
`timer.h` associa os 147.456.000 Hz aos timers T0–T3 de barramento.

Os testes novos usam números brutos independentes da constante:
294.912.000 ticks/s e 4.915.200 ticks por período de 60 Hz. Forçar o clock
antigo faz os quatro testes novos falharem. Usar somente a constante do
próprio header para gerar o tempo esperado teria escondido o erro novamente.

## Painel completo e estágios da PPU

O painel passa de cinco para seis linhas e termina na coordenada lógica
216, dentro da abertura de 224 linhas. Sua posição anterior podia cortar
o rodapé. A geometria foi conferida no código; a imagem no aparelho ainda
precisa ser confirmada.

Campos novos, todos em ms por iteração PS2 e dentro de PPU:

- `FETCH`: decode de informações BG, tilemap/cache/CHR e busca Mode 7.
- `BG`: composição dos backgrounds, prioridades e máscaras main/sub,
  excluindo a chamada de desenho dos sprites.
- `OUT`: chamadas de saída/composição por linha, incluindo clear e hits
  do cache hires. Não equivale ao uso total do GS nem inclui todo Begin/End.

Campos antigos e três taxas fonte/render/apresentação continuam presentes.
As medidas são inclusivas: não somar PPU com seus filhos ou CORE com PPU.
Atualização aproximadamente uma vez por segundo. Sondas e HUD têm custo;
o normal compila ambos fora. O painel v2 é uma ferramenta de medição,
não uma confirmação de que os caminhos restantes já foram otimizados.

## Escrita redundante no compositor PS2

Em `_RenderBGData_O`, o primeiro BG da tela zerava 16 bytes antes de
escrever novamente o mesmo bloco. Os caminhos opaco e parcialmente opaco
já armazenam os **16 pixels completos**. No parcial, PCEQB/POR/PXOR já
transforma os pixels transparentes em zero antes de SQ.

O SQ de zero foi movido para o bloco vazio. Nenhum pixel, máscara de
prioridade, janela ou efeito é omitido. Não há interface/buffer novo,
transferência GS diferente, identificação de ROM ou reescrita do renderer.
É uma economia pequena no EE, compartilhada pelos modos que usam o
compositor indexado, incluindo Mode 7 e os buffers main/sub de hires.

Fixtures compiladas pelo mesmo GCC R5900 usam os corpos exatos antes e
depois, comparados com um oráculo escalar independente. Foram verificados
67.672 casos: todas as 65.536 máscaras de 16 pixels, fine X 0..7, linhas
com máscaras variadas e guardas de destino. A modelagem interpreta as
instruções inteiras, slots e MMI necessários; não executa cache/GS físico.

| Uma linha, 256 pixels | SQ antes/depois | Instruções modeladas antes/depois |
|---|---:|---:|
| Vazia | 16 / 16 | 124 / 124 |
| Totalmente opaca | 32 / 16 | 300 / 300 |
| Todos os blocos parcialmente opacos | 32 / 16 | 396 / 380 |

A linha opaca elimina stores, mas não reduz a contagem total de instruções
nessa fixture devido ao controle gerado pelo compilador. Não apresentar
esses números como porcentagem de FPS ou garantia de 60 FPS.

## Validação e entrega

- 22 executáveis PPU/áudio/SPC: PASS; self-test portátil: PASS.
- Fixtures brutas rejeitam o clock antigo; modelo compilado do compositor:
  PASS nos limites acima. Corpo otimizado do source coincide com a fixture.
- Com sondas do core habilitadas: Top Gear 7.200, Trials intro 10.800,
  Star abertura 7.200 e seleção Mode 5 4.200 frames preservam estado/vídeo
  portátil e 245 checkpoints cumulativos PCM de 32 kHz contra a rodada
  anterior. O compositor MMI PS2 é coberto pelo modelo, não pelo blender C.
- PS2 normal e perfil v2: compilados. Revisão do diff e dos símbolos:
  normal sem medidor ou contadores SNES profundos. Não há sondas privadas
  no código de produção nem mudança de áudio ou timing.
- Commits separados para escala, store redundante, painel e documentação.
  PR continua draft; main não recebeu mudanças não validadas no aparelho.

Entrega: `SNESticle-target-v2-normal.elf` para comparar desempenho e
`SNESticle-target-v2-profile.elf` para ler os estágios restantes. O pacote
contém o normal anterior para comparação, fontes/fixtures da verificação,
patch, logs e checksums. Não contém ROMs. FPS físico e imagem/áudio no
aparelho ainda precisam de confirmação; esta rodada não resolve nem
certifica todos os problemas ou travamentos anteriores.
