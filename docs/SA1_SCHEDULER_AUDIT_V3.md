# SNESticle Revive — auditoria SA-1 e regressões V3

Base: ZIP enviado pelo usuário, commit indicado pelo arquivo ZIP `b4965763f5d2db60457cc527d3f0dbf46c5df2d3`. Os arquivos desta distribuição são substituições completas, não patches. Não há ROM comercial, binário ELF ou dependência de rede no ZIP de correção. Preservam-se a estrutura do projeto e as licenças existentes. A correção substitui os dois patches experimentais anteriores.

## Evidência inicial

`emulog.txt` (Super Mario RPG USA, CRC 1B8A0625): 187 janelas de 30 frames, 5.610 frames, 13.082.804 ciclos EE/frame, capacidade equivalente de 22,54 FPS (não é o FPS de apresentação), 5.606 frames acima de 105% do orçamento. Média ponderada inclusiva: S-CPU 46,42%, SA-1 40,54%, PPU 10,82%. A sobreposição impede somar esses percentuais como tempos exclusivos. O diagnóstico anterior não mostrava SA-1 no ranking por erro no analisador Python. As capturas nível 2 são intrusivas.

## Alterações

- Timer SA-1: fast path para sincronizações de até uma scanline, incluindo comparação H, V e H+V; mantém aritmética original para avanços grandes ou contadores fora da faixa. Mantém resto ímpar, latch de IRQ e contadores. Equivalência contra o modelo anterior não implica precisão de hardware completa.
- Barramento: limpeza parcial das linhas de penalidade, atualização redundante de penalidade BW-RAM removida, exclusão da WRAM privada $7E–$7F da fila no C e R5900; contador de eventos descartados no ASM. O modelo de arbitragem não foi desativado.
- Configuração de fast-memory: atualização de sidecar só quando registros relevantes mudam. CCNT continua atualizando a configuração.
- Idle: invalida latch de polling no remapeamento MMC; aposenta operações aritméticas já concluídas antes de testar elegibilidade de idle. Preserva o caminho original de execução quando há eventos.
- Registradores SA-1: porta $2300 dirige o barramento S-CPU, demais portas inacessíveis retornam o open bus existente; no SA-1, $2301–$230D permanecem acessíveis. A leitura direta de depuração ReadRegister não é uma leitura de barramento.
- Save state SA-1: reutiliza byte anteriormente reservado para o open bus, sem mudar o tamanho do layout. Estados antigos produzidos com esse byte zerado restauram zero. Não estende a serialização geral de outros chips.
- STP: continua avançando o timebase SA-1 sem executar instruções após parada, sem creditar duas vezes os ticks já consumidos por DMA.
- S-CPU: evita apagar o orçamento de um abort já solicitado quando o mesmo trap solicita IRQ novamente; evita atualização redundante da IRQ desativada sem limpar atrasos necessários. Não muda IRQ assertiva.
- Diagnóstico: `[snes-sa1-hot]` conta sincronizações com/sem avanço e amostra 1/64 das que avançam; tempos são somas de amostras de finalização do barramento, StepMasterCycles e atualização de IRQ. Instrumentação só em SNDBG_LOG. Analisador inclui SA-1 e alerta sobre drops (máximo acumulado observado, não soma). Corrige descrição do watchdog GSU, que é diagnóstico, não STOP.
- NES: teste de entrada apontava para um caminho removido; atualizado para o frontend real. Não muda o núcleo MesenCE.
- Documentação: janela real de 30 frames, cooldown de 15. Acrescentados comparador de logs e benchmark sintético sem ROM de terceiros.

## Testes executados neste ambiente (host x86-64)

- SA-1: suíte completa, incluindo compilação do serializador; diferencial real do temporizador 80.000 casos; regressões de open bus (256 valores), save/restore, STP, remapeamento, aritmética, barramento, timer. PASS.
- S-CPU: testes internos de open bus, wrap de 24 bits, IRQ/abort repetido, MVN interruptível. PASS. Nenhum vetor de opcode externo foi fornecido; saída `CPU tests: 0` refere-se a esses vetores externos.
- ROM Lab integrado: self-test em diagnóstico 0, 1 e 2. PASS (estado de 529304 bytes). Compila o core C, não o EE/GS frontend.
- SuperFX: 17.960 casos fuzz, suíte PASS; DSP1 PASS; ROM mapping PASS.
- PPU: 3 configurações de hotpath, CHR 16.384, tilemap 24.576, máscaras 65.536+4.096, mosaic 16.384+64 por configuração; OAM 4.096, VRAM 528. PASS.
- Hardware: SPC timer 312.521 asserções, memória APU 1.181.738, multiplicação 65.536, A-bus DMA 16.777.216, OBC1 131.072, I/O e cadence. PASS.
- Áudio: 6 hashes de mixer PCM, SPC 18.432 casos/3.002 eventos, BRR 262.144 blocos. PASS.
- HostFS mock: 13 asserções PASS. NES input: todas as 256 combinações, turbo e disconnect PASS após corrigir o caminho. Suíte completa MesenCE não executada.
- Analisador: 3 testes Python PASS.

### Benchmark sintético do scheduler

`bash tools/sa1test/bench.sh 200`, no código original e no modificado, usando a mesma fixture: 1.310.000 slices, 1.309.999 idle, 71.473.600 master cycles, 35.736.800 sched/exec, timer 0/0, PC 008000, zero drops nas duas versões. Tempos (ms) original 65,920 / 75,920 / 75,433; candidato 51,470 / 58,501 / 51,682. Medianas 75,433 e 51,682 ms, redução de 31,5% nesse microbenchmark (host, sem frontend, ROM comercial, ASM R5900 ou sincronização gráfica). Não projetar o percentual para FPS de Super Mario RPG. Repetir no mesmo hardware/compilador.

## Aplicação no Debian / Termux

Faça backup de alterações locais antes de sobrescrever. O ZIP tem os caminhos `src/`, `tools/` e `docs/` diretamente na raiz. No checkout limpo da base, use:

```sh
cd ~/SNESticleRevive
git status --short
git switch -c release/sa1-audit-v3
unzip -o /sdcard/Download/SNESticleRevive-audit-v3.zip -d .
git diff --check
bash tools/sa1test/build.sh && tools/sa1test/sa1_test
bash tools/cputest/build.sh && tools/cputest/cpu_test </dev/null
bash tools/romlab/build.sh && tools/romlab/romlab self-test
(cd tools/snesdiag && python3 -m unittest -v)
python3 tools/mesencetest/check_input.py
git add src/snes tools docs
git commit -m "Optimize SA-1 scheduler and fix core bus regressions"
git push -u origin release/sa1-audit-v3
```

O workflow existente dispara em main, release/** e pull requests para main. A branch release/sa1-audit-v3 aciona as builds release/diag1/diag2. Não foi alterado o workflow.

## Validação no PS2 / NetherSX2

Compare primeiro baseline e candidato no diag1, mesma ROM/CRC, mesma configuração, mesma sequência de inputs. Use diag2 somente para detalhes. Não compare diag1 com diag2. Use `python3 tools/snesdiag/compare_sa1.py antes.txt depois.txt` e `python3 tools/snesdiag/analyze.py depois.txt`. Novos contadores permitem localizar custo de finalização, step e IRQ. O teste de estado integrado host não executa o R5900; validação de build PS2 e hardware ainda necessária.

A ordem recomendada é SMRPG + Kirby Super Star/Dream Land 3 (SA-1), SMW (regressão), Star Fox/Yoshi's Island (GSU), SFA2/Star Ocean (S-DD1), F-Zero/SMK (Mode 7), Mega Man X2/X3 (CX4), Trials of Mana (ExHiROM). Não use frameskip obrigatório para disfarçar gargalo.

## Pendências e limites

Não há PS2SDK/R5900 instalado neste ambiente. O ELF PS2, o novo macro ASM, os tempos EE e as ROMs reais não foram compilados/executados aqui. Ainda pendentes: arbitragem SA-1 ciclo-exata, equivalência opcode C/ASM, IRQ/DMA/HDMA ciclo-exatos, SuperFX clock/cache, DSP4/DSP3 e demais chips, PPU interlace/hires, SPC TEST/echo temporal e todos os demais itens listados em docs/CORE_HARDWARE_AUDIT_1.1.1.md e docs/PERF_ACCURACY_ROUND6_2026-11-25.md. Não fiz mudanças especulativas em PPU/APU/GSU que já passam nas suítes, nem importei implementação de outros emuladores. Esta revisão não certifica todo o catálogo nem garante 60 FPS.

## Referências consultadas

- https://sneslab.net/wiki/SA-1 — registradores, MMC e open bus.
- https://sneslab.net/wiki/SA-1_Hardware_Behavior — testes de hardware para portas, aritmética, conflitos.
- https://snes.nesdev.org/wiki/Open_bus — barramento retido.
- https://snes.nesdev.org/wiki/Timing — relógios e timing.
- https://snes.nesdev.org/wiki/DMA_registers — A/B-bus e DMA.
- https://sneslab.net/wiki/Super_FX — clock e cache.
- https://github.com/ReyFxck/SNESticleRevive/issues/70 — histórico de otimização e motivação do benchmark SA-1.
