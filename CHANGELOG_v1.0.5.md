# SNESticle Revive PS2 v1.0.5

Pequena atualização de compatibilidade sobre a versão **1.0.4**, concentrada
em duas correções do núcleo SNES.

## Correções

- **Aero the Acro-Bat 2:** corrigido o estado de VBlank na linha 0 e limpo o
  estado interno de HDMA no começo do quadro. A mudança trata a causa provável
  da tela preta relatada na
  [Issue #28](https://github.com/ReyFxck/SNESticleRevive/issues/28).
- **Super Mario All-Stars — Super Mario Bros. 2 (USA):** corrigido o
  `offset-per-tile` dos modos 2 e 4 quando o fundo ou o mapa de offsets usa
  tiles 16x16. O renderizador agora seleciona o quadrante 8x8 correto, respeita
  flip, fine scroll e os diferentes tamanhos de tilemap.

## Versão e compatibilidade

- Atualizada para **1.0.5** a versão padrão da compilação, do nome dos ELFs, da
  interface e do identificador usado pelo baixador de capas.
- Mantidas as melhorias de inicialização pelo OPL e de armazenamento da versão
  1.0.4, inclusive o tratamento relacionado à
  [Issue #48](https://github.com/ReyFxck/SNESticleRevive/issues/48).
- Saves, configurações e save states compatíveis com a versão 1.0.4 continuam
  válidos; esta atualização não altera seus formatos em disco.

## Validação

- Testes host-side específicos de linha 0/V-IRQ e `offset-per-tile` 16x16
  passaram.
- O build completo para PS2 compila e linka sem erros.
- A confirmação final de Aero the Acro-Bat 2 e Super Mario Bros. 2 ainda deve
  ser feita em um PS2 real. Ao relatar o resultado, informe o modelo SCPH, a
  versão do OPL e de onde o ELF foi iniciado.

## Créditos

Agradecimentos a **Icer Addis (iaddis)**, **Sardu**, **ps2dev**, equipes do
**OPL**, **Aurora**, autores das implementações de referência e à comunidade
que enviou testes e relatos.
