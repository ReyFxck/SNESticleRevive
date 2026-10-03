# Prioridades do usuário e estado verificado em 2026-10-02

Fonte: lista mais recente do usuário. Ela restaura Star Ocean/Tales e áudio
como urgentes; a orientação anterior de adiar travamentos não vale para as
próximas etapas. Top Gear continua como workload autorizado. Nenhuma entrada
abaixo significa que o problema já foi resolvido no aparelho.

SNESticle: `main` em `6c6eb4a`. A
[PR #97](https://github.com/ReyFxck/SNESticleRevive/pull/97) reúne o candidato
de desempenho/perfil, o adapter RFAuds2 que estava na #98 e o destino HDD
de states que estava na #99. Os commits de cada implementação continuam
separados para revisão. O pacote v2/v3 anterior não foi sobrescrito.
A main não recebeu esses candidatos. O v3 amplia atribuição de custos,
não demonstra 60 FPS nem correção dos travamentos posteriores. A comparação
audsrv/RFAuds2 continua disponível na mesma base por `AUDIO_BACKEND`.

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

A investigação posterior de capacidade está em
[CACHE_CAPACITY](CACHE_CAPACITY_2026-10-02.md). Simular 4/8 vias não recuperou
hits extras na intro de Trials ou no replay Mode 5; na intro/demo de Top Gear,
oito vias recuperariam 58829 hits, 5,93% dos misses atuais. Isso não mede FPS
nem o custo de busca no EE. Nenhum cache foi ampliado nesta etapa.

Mais RAM reservada não prova mais desempenho. É preciso medir hits/misses,
reutilização após expulsão, bytes copiados e invalidações. Rotação/escala
Mode 7 muda os endereços afins; o fetch desse modo não se torna gratuito
por ampliar o cache de linhas BG de outros modos. Preservar efeitos por
scanline e a coerência das escritas faz parte da equivalência.

## Estado atualizado das pendências

A tabela completa e a evidência mais recente estão em
[PENDING_FIXES](PENDING_FIXES_2026-10-02.md). A rodada acrescenta correções
CPU R5900/PCM/MMI/concorrência, controles persistidos de FPS/diagnóstico,
enumeração completa das unidades de armazenamento suportadas, correções
do fluxo SMB, melhorias pequenas do SuperFX e a integração do port MesenCE.
Nenhuma dessas entradas certifica 60 FPS ou os crashes posteriores no alvo.

### Chips: inventário e dependências que continuam abertos

DSP1/2, CX4, OBC1, S-DD1, S-RTC e SA-1 têm classes e caminhos de barramento,
mas precisam de auditoria de completude. SuperFX mantém timing aproximado,
apesar das correções de STOP/IRQ artificial e de bitplanes. DSP3 não tem
executor, DSP4 é placeholder, e SPC7110/ST010/11/18 e sistema Super Game Boy
não receberam implementação nesta rodada. Seus states também exigem uma
extensão versionada; não se muda `sizeof(SnesStateT)` sem preservar bancos
legados e os consumidores de ROM Lab.

[Issue #31](https://github.com/ReyFxck/SNESticleRevive/issues/31) permanece
aberta. O comentário que recomenda 21 MHz constantes não substitui o
comportamento de CLSR nem os clocks/esperas individuais do chip. Ares é
referência desses comportamentos, não arquitetura para substituir o core.

O `emulog.txt` antigo deixou uma análise de 73 janelas/2458 registros, mas
não contém nesta cópia recuperada uma exceção final capaz de explicar os
crashes posteriores. O usuário também perdeu o original. A investigação
não fica condicionada a recuperá-lo; ainda é necessário reproduzir o ponto
no caminho PS2. As fotos disponíveis são do NetherSX2.

O CRT-easymode real continua pendente. Não foi usado o filtro Scanlines
existente como se fosse uma implementação desse shader.

## Critério de entrega

Cada correção/otimização precisa de evidência do defeito/custo, callers e
estados compartilhados examinados, fixtures relevantes, cross-build e
retorno no aparelho. Nada desta lista autoriza reduzir precisão, clocks,
efeitos ou áudio para alcançar um contador de FPS. Ganhos de host não
demonstram FPS no PS2. Integração na main depende de estabilidade e das
melhorias aprovadas; os backends audsrv/RFAuds2 são selecionáveis para
comparação na branch consolidada da PR #97.

## Evidência desta etapa

- [Relatório do adapter RFAuds2](RFAUDS2_BACKEND_2026-10-02.md).
- [Correção do destino HDD](SAVE_STATE_MOUNTED_HDD_2026-10-02.md).
- [Transportes/telemetria do módulo](https://github.com/ReyFxck/RFAuds2/pull/1).

As fotos são do NetherSX2 e não medem um PS2 físico. O relatório
[PENDING_FIXES](PENDING_FIXES_2026-10-02.md) registra as builds, fixtures,
limites e pendências atuais. Os relatórios anteriores conservam a evidência
de cada rodada; suas listas de trabalho são históricas, não o status atual.
A main não recebeu essas mudanças; a revisão permanece na PR #97.
