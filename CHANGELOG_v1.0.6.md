# SNESticle Revive PS2 v1.0.6

Atualização concentrada em desempenho, sincronização e correções de
renderização do núcleo SNES.

## Destaques

- O emulador agora recupera VBlanks perdidos executando os quadros necessários
  sem redesenhá-los antes de apresentar a imagem mais recente. Isso mantém
  lógica, controles, SPC e áudio próximos da velocidade correta quando uma
  cena ultrapassa o orçamento de um VBlank.
- Reduzido o custo dos caminhos de PPU raster, Mode 7 e HDMA, removendo trabalho
  repetido e acessos desnecessários durante a composição das scanlines.
- Melhorado o desempenho observado em cenas pesadas de **Top Gear**, **Top
  Gear 2** e **Mortal Kombat**. Quando a carga ainda excede a capacidade do
  PS2, o emulador prioriza a cadência do jogo e do áudio em vez de entrar em
  câmera lenta.

## Correções

- **Pilotwings:** corrigidos a transformação de Mode 7, o reagendamento de IRQ
  horizontal/vertical durante a scanline e duas particularidades do DSP-1
  original usadas pelo jogo.
- Corrigido o latch de escritas na CGRAM para que alterações de paleta sejam
  aplicadas na posição correta do raster.
- A fila de escritas da PPU foi compactada e ampliada, evitando pressão e
  trabalho extra em jogos que atualizam muitos registradores por quadro.
- Otimizados os caminhos de DMA/HDMA e o descarte seguro de quadros sem pular a
  execução do hardware emulado.
## Compatibilidade

- Saves, configurações e save states existentes continuam válidos.
- Nenhum formato de arquivo persistente foi alterado.
- A recuperação automática de desempenho é desativada durante reprodução ou
  gravação de filmes e durante sessões que exigem execução determinística.

## Validação

- Build completo para PlayStation 2 compilado e linkado com o PS2SDK.
- Testes host-side de DSP-1, Mode 7, OAM, fila da PPU e recuperação de VBlank
  passaram.
- Testes práticos confirmaram melhora expressiva em **Top Gear**, **Top Gear
  2** e **Mortal Kombat**, além das correções visuais de **Pilotwings**.

## Créditos

Agradecimentos a **Icer Addis (iaddis)**, **Sardu**, **ReyFxck**,
**itsveenee/SNESticleAurora**, **ps2dev**, autores do **Mesen-S** e à comunidade
que enviou testes, imagens e logs.
