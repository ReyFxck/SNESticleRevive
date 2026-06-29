-- ==========================================================================
--  mesen_dsp4_capture.lua  --  captura do barramento do DSP-4 (Top Gear 3000)
-- ==========================================================================
--  Objetivo: registrar ENTRADA (writes do jogo) E SAIDA (reads = o que o
--  chip devolve) do DSP-4 num emulador de REFERENCIA (Mesen2 / Mesen-S, que
--  emula o DSP-4 corretamente).  Com esses pares entrada->saida eu reimplemento
--  o HLE do SNESticleRevive pra bater igual -- clean-room (observo o
--  COMPORTAMENTO, NAO copio o codigo do emulador).
--
--  >>> A SAIDA JA SAI NO FORMATO .vec <<<
--  Cada linha vira "W xxxx" (palavra escrita) ou "R xxxx" (palavra lida).
--  O arquivo dsp4_trace.txt pode ser jogado DIRETO no runner:
--      cp dsp4_trace.txt SNESticleRevive/tools/dsp4test/vectors/corrida01.vec
--      cd SNESticleRevive/tools/dsp4test && ./build.sh && ./dsp4_vectors
--  As linhas "R" que o runner acusar como FAIL sao exatamente a matematica
--  que falta implementar em sndsp4.cpp.
--
--  Como usar (Mesen2 no PC - Windows/Linux/Mac):
--    1. Abra o Mesen2 e carregue o Top Gear 3000 (USA) ou The Planet's Champ.
--    2. Menu  Debug > Script Window  (ou Tools > Script).
--    3. Abra/cole este arquivo e clique em Run.
--    4. Deixe a INTRO rodar (estrelas/planetas) e comece UMA corrida.
--    5. Pegue o arquivo  dsp4_trace.txt  (na pasta do Mesen) e me mande.
--
--  DSP-4 e' LoROM: o Data Register (DR) fica em $30-$3F:8000-BFFF (leitura E
--  escrita).  O barramento do SNES e' de 8 bits; a palavra de 16 bits do DR e'
--  transferida LSB primeiro, depois MSB.  Aqui REMONTAMOS os bytes em palavras
--  (2 bytes por direcao), para casar com o protocolo do HLE.
-- ==========================================================================

local MAX  = 12000        -- teto de PALAVRAS (evita arquivo gigante)
local n    = 0
local file = io.open("dsp4_trace.txt", "w")

-- acumuladores de byte por direcao (LSB depois MSB)
local wHaveLo, wLo = false, 0
local rHaveLo, rLo = false, 0

local function out(s)
  if file then file:write(s .. "\n") end
  if emu.log then emu.log(s) end
end

local function finish()
  out("# === DSP4 TRACE FIM (" .. n .. " palavras) ===")
  if file then file:flush(); file:close(); file = nil end
end

local function onWrite(address, value)
  if n >= MAX then return end
  if not wHaveLo then
    wLo, wHaveLo = value, true            -- LSB
  else
    local word = wLo + value * 256        -- MSB -> monta a palavra
    wHaveLo = false
    out(string.format("W %04X", word))
    n = n + 1
    if n >= MAX then finish() end
  end
end

local function onRead(address, value)
  if n >= MAX then return end
  if not rHaveLo then
    rLo, rHaveLo = value, true            -- LSB
  else
    local word = rLo + value * 256        -- MSB -> monta a palavra
    rHaveLo = false
    out(string.format("R %04X", word))    -- word = o que o DSP-4 DEVOLVEU (ouro!)
    n = n + 1
    if n >= MAX then finish() end
  end
end

-- Resolve o nome do enum de tipo de callback (muda entre Mesen2 e Mesen-S).
local CT = emu.callbackType or emu.memCallbackType
assert(CT, "API de callback nao encontrada -- me diga a versao do Mesen")

-- DR do DSP-4 em LoROM: $30:8000-BFFF (banco 0x30).  Se nao capturar nada,
-- pode ser que seu dump use outro banco/mirror -- me avise.
emu.addMemoryCallback(onWrite, CT.write, 0x308000, 0x30BFFF)
emu.addMemoryCallback(onRead,  CT.read,  0x308000, 0x30BFFF)

out("# === DSP4 capture (formato .vec) armado em $30:8000-BFFF (DR). ===")
out("# Rode a intro/corrida; mande o dsp4_trace.txt.")
