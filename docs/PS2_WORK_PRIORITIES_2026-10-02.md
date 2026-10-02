# Prioridades do usuário e estado verificado em 2026-10-02

Fonte: lista mais recente do usuário. Ela restaura Star Ocean/Tales e áudio
como urgentes; a orientação anterior de adiar travamentos não vale para as
próximas etapas. Top Gear continua como workload autorizado. Nenhuma entrada
abaixo significa que o problema já foi resolvido no aparelho.

SNESticle: `main` em `6c6eb4a`, candidato de desempenho em `70347f8`,
[PR #97](https://github.com/ReyFxck/SNESticleRevive/pull/97).
O normal/profile v2 entregue continua idêntico. Esta rodada não muda código
de emulação, transporte ou apresentação do SNESticle.

## Caches da build normal v2

Tamanhos extraídos dos símbolos do ELF PS2 em `70347f8`, com caches padrão
habilitados. KiB = 1024 bytes; MiB = 1048576 bytes. Incluem chaves, máscaras
e padding dos arrays, além de contadores/metadados listados.

| Estrutura | Bytes | KiB |
|---|---:|---:|
| CHR físico 2bpp/4bpp, compartilhado BG/OBJ | 448512 | 438 |
| Primeira via de linhas BG, com par main/sub hires | 786432 | 768 |
| Segunda via compacta de linhas BG normais | 425984 | 416 |
| Saída de linhas hires | 294912 | 288 |
| Seleção de vítima BG | 1024 | 1 |
| Invalidação por região VRAM | 180 | 0,176 |
| Paleta hires, estado e geração de saída | 521 | 0,509 |
| **Total dessas estruturas** | **1957565** | **1911,685** |

Total: **1,8669 MiB**, aproximadamente 1,96 MB decimais, dentro dos 32 MiB
da RAM do EE. Isso não inclui a ROM carregada, memória emulada, buffers de
frame/áudio, demais tabelas, VRAM do GS ou RAM do IOP. Não mede o consumo
total do emulador nem do heap. Padding entre símbolos não entra na soma.

Esses são caches de software, diferentes do cache físico do R5900
(16 KiB de instruções, 8 KiB de dados) e do scratchpad de 16 KiB.
O cache CHR conserva índices decodificados; o cache BG conserva fetches
com chaves completas; o hires conserva saídas quando o estado se repete.
Escritas VRAM/CGRAM e alterações relevantes invalidam o resultado.
Eles não guardam save states nem substituem armazenamento persistente.

Mais RAM reservada não prova mais desempenho. É preciso medir hits/misses,
reutilização após expulsão, bytes copiados e invalidações. Rotação/escala
Mode 7 muda os endereços afins; o fetch desse modo não se torna gratuito
por ampliar o cache de linhas BG de outros modos. Preservar efeitos por
scanline e a coerência das escritas faz parte da equivalência.

## Urgente

| Trabalho | Estado/evidência | Próxima verificação necessária |
|---|---|---|
| Mode 7 em movimento | Continua aberto após o retorno do usuário. Há otimizações gerais na PR, sem 60 FPS comprovados no aparelho. | Comparar normal v2 e profile v2 na mesma cena/configuração; separar FETCH, BG e OUT. Investigar histórico e dependências do caminho dominante antes de nova edição. |
| Crash de Star Ocean | Correções gerais de MMC/FastROM e remapeamento já preparadas; não demonstram que o travamento posterior relatado foi corrigido. | Últimas linhas do crash, revisão exata do ELF, entrada e ponto da sequência; comparar execução EE com checkpoints portáteis. |
| Tela preta/travamento posterior de Tales | Recuperação geral do header da tradução resolve o boot no core portátil; o relato posterior permanece aberto. | Reproduzir após o diálogo no caminho PS2 e localizar CPU/APU/PPU/DMA/mapper envolvido. |
| Spawn (USA), áudio na intro sem entrada | Ainda não reproduzido: essa ROM não foi fornecida. A correção geral de ganho de 2x foi feita antes, mas não comprova a causa em Spawn. | Capturar PCM antes/depois do mixer MMI, conversão e backend; distinguir clipping, PCM incorreto e falta de amostras no transporte. |
| Trocar audsrv pelo RFAuds2 | Fonte localizado em [ReyFxck/RFAuds2](https://github.com/ReyFxck/RFAuds2), main `6787ef5`. O SNESticle ainda usa audsrv. | Integrar candidato de transporte por adapter `Aud_*`, preservando conversão/DSP/PCM para um A/B de backend. |
| RFAuds2 assíncrono/completo | O main original usa `sceSifCallRpc` bloqueante. Candidato isolado `fix/async-pcm-transport` acrescenta envio NOWAIT, conclusão consultável e admissão parcial IOP sem espera por espaço. Compilou EE/IOP; fixtures de transporte passaram. | Adapter que retém blocos/caudas, service/poll durante trabalho útil, transições e medições no aparelho. Não chamar o módulo inteiro de concluído. |
| Super FX, issue #31 | Core funcional existe; o scheduler ainda usa 384/960 **instruções** por linha como aproximação dos clocks. | Auditar CLSR, custos de instrução/memória/cache/multiplicação e sincronização/IRQ; depois medir custo EE de dispatch, PLOT/RPIX e acessos. |
| Completar chips existentes | A presença de classes/flags não certifica implementação completa. Lacunas confirmadas abaixo. | Inventário por comandos, registradores, memória, timing, IRQ e serialização; fixtures de comportamento antes de otimização. |

Do `emulog.txt` antigo foi preservada a análise de 73 janelas/2458 registros
e os relatórios [AUDIT](WORKLOAD_AUDIT_2026-10-01.md) e
[FOLLOWUP](WORKLOAD_FOLLOWUP_2026-10-01.md). O arquivo original completo não
está mais neste executor após a recuperação do ambiente. A análise agregada
de custos não identifica sozinha a exceção nem as últimas instruções de um
crash. Para essa investigação é necessário recuperar o arquivo integral,
preferencialmente também um log da build que reproduz o travamento.

### Super FX: opinião da issue e comportamento observável

[Issue #31](https://github.com/ReyFxck/SNESticleRevive/issues/31),
[comentário de emukistreez-opensoure](https://github.com/ReyFxck/SNESticleRevive/issues/31#issuecomment-5787286369):
o comentário sugere manter 21 MHz constantes. Isso não foi aplicado.
O programa pode escrever CLSR para selecionar velocidade; fixar o clock
seria uma mudança de comportamento e não uma otimização do EE.

`SNGSU::GetLineInstructionBudget()` e seu caller em `ExecuteLine` declaram
explicitamente o modelo aproximado atual. Contar instruções não demonstra
MHz corretos; faltam os custos individuais e esperas de memória. Essa é
uma pendência geral do core, independente dos títulos citados na issue.
MIPS/assembly deve ser decidido depois de perfilar o caminho real e comparar
com o código gerado; não substitui correção de timing e não garante 60 FPS.

### Inventário inicial dos chips

| Componente | Evidência do código atual |
|---|---|
| DSP-1/DSP-2, CX4, OBC1, S-DD1, S-RTC, SA-1 | Classes e caminhos de barramento implementados; completude/compatibilidade precisam de auditoria por subsistema. |
| Super FX | Opções, pipeline/cache e PLOT/RPIX implementados, com timing aproximado e watchdog de desenvolvimento; não está completo. |
| DSP-3 | Flag reconhecida e janela mapeada, mas `m_pDsp` fica NULL; não há executor DSP-3. |
| DSP-4 | `sndsp4.cpp` é um placeholder explícito, sem comandos DSP-4. O comentário HLE em `snmemmap.cpp` está desatualizado. |
| Super Game Boy | Flag do cartucho não demonstra emulação do sistema Game Boy; precisa de inventário dedicado. |
| SPC7110/ST010/ST011/ST018 | Nenhum core encontrado nesta árvore. Entram no trabalho de chips ausentes. |

Não foram adicionadas condições por título/CRC. Detecção e exceções
preexistentes não foram alteradas nesta rodada. Não usar código de ares
como arquitetura de substituição; consultar comportamento só diante de
uma diferença específica e demonstrada.

## Importante

| Trabalho | Estado inicial e escopo |
|---|---|
| Chips ausentes | Usar o inventário acima e criar etapas independentes; a detecção de um chip não pode simular suporte funcional. |
| SMB | Integração de rede/smbman já existe, com estados separados para DHCP, driver, protocolo, autenticação, share e browse. Protocolo atual é SMB1/NT1. Reproduzir o erro e a configuração do cliente/servidor antes de trocar componentes. |
| Destinos de save state | Código enumera mass0/mass1/mass, MC e MMCE e considera a origem da ROM. PFS/HDD depende da origem/mapeamento da ROM; isso pode limitar o que aparece. Auditar enumeração e mount sem remover a exclusão de destinos somente leitura. Coprocessadores sem estado serializado são outra pendência distinta. |

## Prioridade baixa

| Trabalho | Dependência |
|---|---|
| CRT-easymode opcional on/off | Definir resultado visual e custo compatíveis com o GS; validar antes de ativar por padrão. |
| Substituir infoNES pelo port MesenCE | Fonte/branch do port PS2 ainda não localizado. Foi solicitado o link ao usuário. Comparar orçamento EE/RAM, áudio, interface e saves antes da integração. |

FPS on/off e diagnóstico no menu, pedido anterior, continuam registrados
para a etapa de configuração. O profile v2 atual é uma build separada.

## Critério de entrega

Cada correção/otimização precisa de evidência do defeito/custo, callers e
estados compartilhados examinados, fixtures relevantes, cross-build e
retorno no aparelho. Nada desta lista autoriza reduzir precisão, clocks,
efeitos ou áudio para alcançar um contador de FPS. Ganhos de host não
demonstram FPS no PS2. Integração na main depende de estabilidade e das
melhorias aprovadas; a build de desempenho e o candidato RFAuds2 ficam
separados nesta etapa.
