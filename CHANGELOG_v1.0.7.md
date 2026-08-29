# SNESticle Revive PS2 v1.0.7

Atualização focada em controle de frameskip, compatibilidade de ROMs e
desempenho da PPU no PlayStation 2.

## Novidades

- Adicionada a opção **Frameskip: On/Off** nas configurações de vídeo.
  - **On:** permite recuperar VBlanks perdidos sem colocar toda a emulação em
    câmera lenta quando uma cena ultrapassa o orçamento do quadro.
  - **Off:** renderiza todos os quadros; é indicado para jogos leves ou para
    comparar precisão visual, mas cenas pesadas podem ficar lentas.

## Correções

- **Pinocchio (USA):** corrigida a seleção do header HiROM. O carregador agora
  pontua os dois headers físicos antes de decidir por uma conversão Type-1,
  evitando que dados coincidentes sejam confundidos com uma ROM intercalada.
  O jogo passa da tela preta e inicia normalmente.
- Reduzido o trabalho do renderizador de OBJ em cenas com muitos sprites,
  preservando os limites e a prioridade do hardware do SNES.

## Desempenho

- Os lookups planares usados continuamente pelos backgrounds, sprites e pela
  composição de máscaras agora ficam na região livre do scratchpad da EE.
  Isso evita que a tabela dispute o cache de dados de 8 KiB com a VRAM e o
  estado da PPU.
- O caminho MIPS de expansão de máscaras passou a usar loads de 64 bits
  alinhados em vez de pares de loads desalinhados, reduzindo instruções nas
  scanlines com múltiplos backgrounds.
- O layout do scratchpad possui verificações de compilação para impedir
  sobreposição com o renderer, o DMA do GS ou o buffer temporário do mixer
  SPC.

## Compatibilidade

- Saves, configurações e save states existentes continuam compatíveis.
- Nenhum formato persistente foi alterado.
- O DSP-1 não foi modificado nesta versão.
- SuperFX continua experimental e SA-1 ainda não está implementado.

## Validação

- O carregamento de **Pinocchio (USA)** foi confirmado após a correção HiROM.
- Testes de ROM cobrem HiROM normal, falso Type-1 e imagens Type-1 legítimas.
- Testes de PPU cobrem OAM/OBJ, cache CHR, Mode 7, fila raster e frameskip
  seguro.
- Build completo para PlayStation 2 compilado e linkado com o PS2SDK.

## Créditos

Agradecimentos a **Icer Addis (iaddis)**, **Sardu**, **ReyFxck**,
**itsveenee/SNESticleAurora**, **ps2dev**, autores do **Mesen-S** e à comunidade
que enviou testes, imagens e logs.
