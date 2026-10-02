# Top Gear: medir o trabalho dentro do ELF

Base desta rodada: `404217d`, branch de revisão da PR #97. A main continua
em `6c6eb4a`. A foto enviada mostra NetherSX2/Vulkan, G aproximadamente
42,48, V 59,85 e velocidade 100%. O carro inferior está parado, mas a tela
superior continua em movimento. CPU, SPC/DSP, HDMA, avaliação de OAM e
composição continuam ativos. A taxa G do NetherSX2 não identifica a taxa
de frames SNES executados, nem o subsistema responsável pelo custo.

## Dependências conferidas

O normal usa CPU ASM Plain para ROMs comuns, SPC C, mixer DSP/MMI e renderer
indexado legado no EE, com composição e transferências GIF/GS. As alterações
anteriores reduziram decodes e preparação de comandos; elas não mediram o
custo no console. Hits do cache de CHR de OBJ também não demonstram que toda
a avaliação, busca e composição dos sprites sejam baratas.

Foram conferidos `MainLoopProcess`, `_ExecuteSnes`, `MainLoopRender`,
`GPFifoFlush`, o drain do gsKit, `GSK_SyncFlip`, `DmaSyncGIF`, a fila da PPU,
`Write2000`, HDMA e as chamadas de áudio audsrv. Os waits protegem buffers e
a ordem dos comandos; não foram removidos. Não foi identificado nesta
rodada um bug de pacing que explique toda a perda relatada. A bancada
portátil não executa apresentação, transporte de áudio IOP ou GS físico.

## Despacho das escritas de PPU

`Write2000` consultava flags/janelas SA-1, S-RTC e SuperFX e verificava a
janela APU antes de reconhecer `$2100-$213F`. CPU, DMA e HDMA compartilham
esse handler. Essas janelas são disjuntas; o bloco original da PPU agora é
despachado primeiro. Fila, flushes, latches, `WriteTimed`, linha e relógio
do acesso permanecem iguais. Os handlers dos cartuchos, APU e WRAM
conservam seu comportamento.

O teste do código MIPS compilado compara seis portas de memória, espelhos
de bancos `$00/$80` e oito combinações das flags de cartucho: 96 prelúdios
chegam à sincronização original da PPU com os mesmos argumentos. Sem chips,
a contagem modelada passa de 42 para 27 instruções até esse ponto. O modelo
interpreta controle/slots e não mede cache ou ciclos físicos; essa economia
pequena não estabelece um ganho de FPS para a corrida.

## Medidor separado do normal

`SNES_TARGET_PROFILE=1`, combinado com `SNES_DIAGNOSTICS=0`, habilita um HUD
com amostras de EE Count a 147.456.000 Hz. Não há contadores por instrução
ou pixel, nem novos logs contínuos SIO. As sondas e o HUD têm custo; o ELF
normal continua compilando tudo isso fora. A mudança de modo participa do
marcador de recompilação para impedir mistura de objetos.

O painel atualiza aproximadamente a cada segundo. FPS usa o tempo
decorrido completo, inclusive esperas. Os tempos são médias em **ms por
iteração de apresentação PS2**, incluindo frames ocultos quando o
frameskip estiver habilitado.

| Campo | O que mede |
|---|---|
| SNES | Frames de emulação executados por segundo |
| DRAW | Frames SNES executados com superfície de vídeo por segundo |
| PS2 | Iterações concluídas de apresentação por segundo |
| WORK | Tempo da iteração, descontando apenas a fase FLIP |
| CORE | `ExecuteFrame` completo; inclui PPU, SPC e mixer |
| PPU | Chamadas ao renderer de linhas; inclui OBJ/composição e waits internos |
| OBJ | Atualização/visibilidade, busca e desenho de sprites no renderer indexado |
| GIF | Tempo dentro de `DmaSyncGIF`, inclusive o custo de entrar na função |
| SPC | Execução do SPC em `SyncSPC` |
| MIX | Mixers normal e silencioso; inclui enqueue quando chamado pelo mixer |
| IOP | Consultas audsrv, interleaving, back-pressure e envio de áudio |
| PREP | Preparação do frontend e HUD antes de `GPFifoFlush` |
| SUBMIT | `GPFifoFlush`, incluindo drain/sincronizações do backend |
| FLIP | `GSK_SyncFlip`, incluindo espera por vsync |

As medidas são inclusivas: **não somar CORE + PPU + OBJ**. IOP/GIF também
podem estar dentro de outros campos. CORE não significa apenas CPU 65C816.
GIF não mede utilização total do GS. Nenhum efeito, operação SNES, áudio,
frameskip ou ordem de sincronização foi alterado para medir.

## Teste no aparelho

1. Abrir `SNESticle-target-profile.elf` e a mesma ROM, mantendo frameskip e
   configurações iguais às do teste anterior. Para medir sem frames
   ocultos, desligar frameskip.
2. Esperar pelo menos dois segundos na corrida. Fotografar o painel com o
   carro parado e depois em movimento. Emulador PS2 deve usar clock EE
   padrão: alterar o clock virtual invalida a conversão Count → tempo.
3. Comparar o ELF normal novo com `SNESticle-topgear-normal.elf` da rodada
   anterior, com os mesmos ajustes. O normal não desenha o painel.

WORK próximo/acima de 16,7 ms pode perder a janela de apresentação de
60 Hz. FLIP alto sozinho pode ser espera normal por vsync, não trabalho
dispensável. OBJ alto justifica investigar sprites; PPU alto com OBJ baixo
aponta para outros estágios do renderer. IOP alto exige investigar o
transporte/espera do áudio, preservando o DSP. Esses são critérios para
interpretar a próxima captura, não resultados já medidos.

## Validação desta rodada

- 22 executáveis PPU/áudio/SPC passam, incluindo unidades de Count,
  cadência fonte/render, wrap e acumuladores de 64 bits do novo medidor.
- Contra `404217d`: Top Gear 7.200 frames, Trials intro 10.800, Star abertura
  7.200 e seleção Mode 5 4.200 preservam estado/vídeo portátil e todos os
  245 checkpoints acumulados PCM do mixer completo a 32 kHz.
- Com sondas do core habilitadas: Top Gear 7.200 frames preserva novamente
  estado/vídeo e seus 60 checkpoints PCM. Self-test portátil passa. Isso
  não executa o HUD, frontend PS2 ou os timers de audsrv/GIF no hardware.
- Builds PS2 normal e target-profile passam. ELF normal não contém os
  símbolos do medidor ou das estatísticas SNES profundas. Código de áudio
  normal permanece igual; as sondas são exclusivas do perfil opcional.
- Diff revisto sem sondas privadas, identificação de ROM, efeitos omitidos
  ou mudança de timing. O medidor opcional é uma ferramenta explícita de
  investigação, não instrumentação temporária no build normal.

O pacote inclui os dois ELFs novos, o normal anterior, checksums, logs,
relatório e patch. Não inclui ROMs. Sem uma captura do painel ou medição em
PS2 físico, o gargalo dominante da corrida e um ganho de FPS permanecem
sem confirmação. A PR continua draft; esta rodada não integra mudanças
na main nem afirma que a corrida atingiu 60 FPS.
