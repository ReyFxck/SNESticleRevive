-- ==========================================================================
--  mesen_dsp4_capture.lua  --  captura do barramento do DSP-4 (Top Gear 3000)
-- ==========================================================================
--  Objetivo: registrar ENTRADA (writes do jogo) E SAIDA (reads = o que o
--  chip devolve) do DSP-4 num emulador de REFERENCIA (Mesen2 / Mesen-S, que
--  emula o DSP-4 corretamente).  Com esses pares entrada->saida eu reimplemento
--  o HLE do SNESticleRevive pra bater igual -- clean-room (observo o
--  comportamento, NAO copio o codigo do emulador).
--
--  Como usar (Mesen2 no PC - Windows/Linux/Mac):
--    1. Abra o Mesen2 e carregue o Top Gear 3000 (USA) ou The Planet's Champ.
--    2. Menu  Debug > Script Window  (ou Tools > Script).
--    3. Abra/cole este arquivo e clique em Run.
--    4. Deixe a INTRO rodar (estrelas/planetas) e comece UMA corrida.
--    5. Pegue o arquivo  dsp4_trace.txt  (na pasta do Mesen) ou copie o log
--       da janela de script e me mande.
--
--  DSP-4 e' LoROM: o Data Register (DR) fica em $30-$3F:8000-BFFF (leitura E
--  escrita).  Logamos byte a byte (o barramento do SNES e' de 8 bits; o DR
--  de 16 bits e' transferido LSB depois MSB).
-- ==========================================================================

local MAX  = 12000        -- teto de transacoes (evita arquivo gigante)
local n    = 0
local last = nil          -- "W" ou "R" da ultima transacao (pra agrupar)
local file = io.open("dsp4_trace.txt", "w")

local function out(s)
  if file then file:write(s .. "\n") end
  if emu.log then emu.log(s) end
end

local function onWrite(address, value)
  if n >= MAX then return end
  out(string.format("W %02X", value))
  n = n + 1
  if n == MAX then out("=== DSP4 TRACE FIM ==="); if file then file:flush(); file:close(); file = nil end end
end

local function onRead(address, value)
  if n >= MAX then return end
  out(string.format("R %02X", value))   -- value = o que o DSP-4 DEVOLVEU (ouro!)
  n = n + 1
end

-- Resolve o nome do enum de tipo de callback (muda entre Mesen2 e Mesen-S).
local CT = emu.callbackType or emu.memCallbackType
assert(CT, "API de callback nao encontrada -- me diga a versao do Mesen")

-- DR do DSP-4 em LoROM: $30:8000-BFFF (banco 0x30).  Se nao capturar nada,
-- pode ser que seu dump use outro banco/mirror -- me avise.
emu.addMemoryCallback(onWrite, CT.write, 0x308000, 0x30BFFF)
emu.addMemoryCallback(onRead,  CT.read,  0x308000, 0x30BFFF)

out("=== DSP4 capture armado em $30:8000-BFFF (DR). Rode a intro/corrida. ===")
