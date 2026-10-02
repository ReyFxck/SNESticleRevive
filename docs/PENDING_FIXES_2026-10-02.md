# Correções desta rodada e pendências verificadas

Branch da PR #97; `main` permanece em `6c6eb4a`. Cada alteração de produção
fica em commit próprio. Não foram acrescentados hacks por título/CRC,
clocks menores, descarte de efeitos, simplificação do áudio ou novos cores
SNES. O port NES solicitado é uma integração separada.

## CPU R5900 e áudio

A execução rápida contava um ciclo de máquina ao testar o fim de cada lote,
mesmo sem executar opcode. Isso avançava indevidamente o relógio do ALU
5A22 introduzido recentemente. Agora só o opcode executado conta. Também
foram corrigidos o byte de assinatura BRK/COP, o wrap de PC dentro de PB,
o open bus do byte alto em escrita de 16 bits e a contabilização de entrada
WAI/STP conforme o modelo existente. A equivalência entre C e assembly não
afirma que o modelo coarse de sono já seja timing completo do silício.

`check_ps2_cpu.py` executa o CPU assembly realmente ligado ao ELF, comparando
4.216 casos com o core C: opcodes, modos, limites, budgets e callbacks que
mudam ciclos/open bus. Todos passam. Isso amplia a cobertura de dependências
de traps, mas não reproduz o crash posterior de Star Ocean/Tales.

O buffer de saída descartava frames estéreo ímpares. A conversão cúbica
32→48 kHz também usava uma interpolação diferente nas bordas de cada bloco
e perdia uma origem ímpar. O estado pequeno de lookahead mantém a mesma
fórmula entre blocos, sem descartar amostras. O mixer echo MMI tinha operands
mutados sem early-clobber e dependia de HI/LO sem informar o compilador.
As constraints foram corrigidas, mantendo sua fórmula e configuração
conservadora O1/noinline.

Fixtures de PCM/gain/chunks passam com sanitizers. O echo do ELF passa em
2.000 casos de instruções modeladas, incluindo extremos e guardas. Spawn
continua exigindo reprodução no alvo: seu PCM portátil anterior não mostra
clipping que explique o relato e os modelos não certificam playback SPU2.

## RFAuds2, controles e armazenamento

O produtor principal e o helper BGM compartilhavam FIFO/slot RPC sem lock.
A fixture concorrente falha no adapter anterior e passa com o semáforo EE
ao redor das APIs. O lock conserva a posse do request/reply; não desliga IRQ
para esperar RPC. Streaming e telemetria permanecem NOWAIT; boot, controles
ocasionais e back-pressure com fila cheia ainda podem aguardar. A
[PR RFAuds2 #1](https://github.com/ReyFxck/RFAuds2/pull/1) conserva a revisão
separada dos fontes EE/IOP do transporte.

FPS e diagnóstico são opções persistidas no menu, desligadas por padrão.
A configuração v22 migra para v23 sem habilitá-las. O diagnóstico coarse
compila normalmente, mas probes desligados não leem Count; a fixture testa
10.000 chamadas desligadas. `SNES_TARGET_PROFILE=0` pode excluir as sondas
na compilação; `SNES_DIAGNOSTICS=2` continua sendo uma build de debug.

Destinos suportados agora enumeram mass0..9, alias mass, mc0/mc1, MMCE e HDD
montado. IDs de preferências antigas são preservados. USB e MX4SIO podem
compartilhar a enumeração mass. Auto também considera um HDD gravável já
montado, mesmo com ROM de outro dispositivo. O teste confere 16 raízes e
cada preferência. Os leitores/escritores de bancos reais passam com miniz
e sanitizers para payload SNES legado e Mesen variável, raw/deflate, CRC,
limites, ROM/core/slot, falta de memória e bancos incompletos.
SMB não é apresentado como destino gravável de states;
a interface de formatação de cartão continua limitada às portas nativas. Os IDs antigos mc2..7 ficam preservados para migração de
preferências, mas não aparecem mais na lista de destinos.

O SMB teve três defeitos de frontend corrigidos: ausência de mass2..9/mc2..7
na descoberta de configuração, SD-only sem carregar MX4SIO e overflow no
inteiro da porta. Um quarto, reproduzido pelo ASan, era `strncpy` do caminho
da configuração sobre ele mesmo ao salvar. O teste exercita arquivos reais
temporários, requests/ABI e recuperação de erros do fluxo. O servidor e o
IOP não estão nessa fixture; o módulo continua SMB1/NT1, sem SMB2/3.

## SuperFX e MesenCE

O SuperFX forçava STOP+IRQ ao ultrapassar dois milhões de instruções em um
job, um watchdog de desenvolvimento sem base no hardware. Removida essa
mudança arquitetural, cada chamada continua limitada à sua fatia, e um loop
válido de 2.101.000 instruções mantém GO sem IRQ artificial. A conversão de
oito cores para bitplanes faz uma transposição de 64 bits uma vez, em vez
de repetir oito testes por plano. Os mesmos bytes/endereço/coverage passam
em 98.304 casos host e 3.072 casos R5900. Veja
[superfxtest](../tools/superfxtest/README.md) para contagens e limites.

O port MesenCE fornecido está ligado ao frontend de vídeo, entrada, áudio,
SRAM, states e cadence; InfoNES continua disponível para states antigos.
ASan encontrou e confirmou as correções de alocação da tabela de memória
e memcpy de zero bytes. Todos os canais começavam mudos; agora emitem PCM.
CHR não volátil, release da ROM e rollback de state incompleto têm fixtures.
Veja [integração MesenCE](MESENCE_INTEGRATION_2026-10-02.md).

## O que ainda não está concluído

| Pedido | Limite atual |
|---|---|
| Mode 7/movimento/Top Gear a 60 FPS | Há otimizações gerais e atribuição mais completa; faltam frame times comparáveis no PS2 para confirmar o gargalo residual e o ganho. Nenhum teste host/modelo equivale a FPS físico. |
| Star Ocean/Tales, travadas posteriores | Em Tales tr2 foi reproduzido um bloqueio no core portátil a partir do frame 4510: CPU aguarda a APU, SPC executa em 0000/0002 e as portas param de responder. Isso também existe na referência 27007e5; falta localizar a primeira transição incorreta e confirmar equivalência com o relato no PS2. Star ainda não teve o crash posterior reproduzido. |
| Spawn e outros jogos com som estourado | Corrigidos defeitos de saída, resampler, MMI e concorrência; falta playback prolongado no alvo e atribuição do defeito específico. |
| RFAuds2 totalmente concluído | Streaming assíncrono implementado; controles ainda podem ser síncronos. FAT/Slim, DMA/IRQ e transições precisam de teste. |
| SuperFX completo/issue #31 | O budget permanece 384/960 instruções por linha. Faltam clocks individuais, custos de memória/cache, multiplicação e sincronização CPU/GSU. Fixar 21 MHz não seria uma otimização equivalente. |
| Chips existentes completos | DSP1/2, CX4, OBC1, S-DD1, S-RTC e SA-1 ainda requerem auditoria de comandos, barramento, IRQ, timing e states. |
| Chips ausentes | DSP3 não tem executor, DSP4 é placeholder. SPC7110, ST010/11/18 e sistema Super Game Boy não foram implementados nesta rodada. Detectar uma flag não constitui suporte. |
| SMB funcional no servidor do usuário | Fluxo de frontend testado; falta validar protocolo/autenticação/listagem/leitura no PS2 com um peer. O driver atual não suporta servidores exclusivamente SMB2/3. |
| States completos de todos os chips | Destinos ampliados. O payload SNES legado ainda omite estado de alguns chips e campos de CPU/DSP; requer extensão versionada com migração dos bancos existentes. |
| CRT-easymode | Não implementado. O shader usa cálculo por texel que o GS fixo não executa diretamente; faltam implementação equivalente e medição dos custos de EE/VU/readback. O filtro Scanlines existente não foi renomeado. |
| MesenCE completo no aparelho | Bridge e fixtures passam; faltam testes físicos, mappers/batteries especiais, discos e acessórios. |

A nova configuração de linhas BG tem 512 linhas de mundo, mantendo duas vias
e a mesma coerência. Os caches gráficos somados passam a 3171005 bytes,
aproximadamente 3,0241 MiB. `SNES_BG_CACHE_LINES=256` permite comparar com
a capacidade anterior; isso não aumenta os caches físicos de CPU nem adiciona
armazenamento útil ao CHR que já cobre toda a VRAM. Ver
[rodada de desempenho](PERFORMANCE_FOLLOWUP_2026-10-02.md).

O diff final não contém sondas privadas de ROM/PCM. O conjunto compila em
R5900 com Mesen/RFA, Mesen/audsrv e InfoNES/RFA, e os testes host relevantes
passam. A PR continua draft: compilação, instruções modeladas e replays
portáteis não fecham as pendências da tabela nem autorizam merge na main.
