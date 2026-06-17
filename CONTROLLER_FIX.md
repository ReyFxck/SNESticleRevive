# Correção do Suporte a Controle no PS2 Real

## Problema Original

O controle funcionava no emulador (PCSX2/NetherSX2) mas **não funcionava no PS2 real**. O sintoma era: o menu aparecia na tela, mas o controle não respondia em hardware retail.

### Causa Raiz

O código antigo carregava a sequência **XSIO2MAN + XMTAPMAN + XPADMAN** da BIOS (rom0:) em `mainloop_iop.cpp`. Essa abordagem causava um conflito:

1. **main.cpp** carregava `sio2man.irx` moderno (PS2SDK) para o memory card
2. **mainloop_iop.cpp** depois tentava carregar `rom0:XSIO2MAN` (BIOS) para o controle
3. No **PS2 real**: XSIO2MAN tenta registrar os mesmos serviços RPC SIO2 que o sio2man moderno já registrou → XSIO2MAN carrega mas seu RPC nunca fica utilizável → XPADMAN não consegue se comunicar com o transporte SIO2 → **controle fica mudo**
4. No **emulador**: PCSX2/NetherSX2 toleram esse conflito na emulação, então o controle funciona

Adicionalmente, o `libxpad.c` foi compilado com `NEW_PADMAN` (linha 25), mas o `rom0:XPADMAN` na maioria das BIOS retail usa o protocolo legado `ROM_PADMAN`, criando outro mismatch de protocolo.

---

## Solução Implementada

A correção moderna segue o mesmo padrão usado por **picodrive PS2**, **OPL**, **uLaunchELF** e **hugorsgarcia/PS2SNESticle**: empilhar o **PS2SDK padman.irx** moderno em cima do **sio2man.irx** moderno que já está rodando.

### Arquitetura da Solução

```
┌─────────────────────────────────────────────────────────────┐
│  EE (Emotion Engine)                                        │
│  ┌──────────────────────────────────────────────────────┐   │
│  │ input.cpp: InputInit(FALSE)                          │   │
│  │   → usa libpad padrão (padPortOpen/padRead/etc)     │   │
│  │   → _Input_bXPad = FALSE (não usa libxpad)          │   │
│  └──────────────────────────────────────────────────────┘   │
└─────────────────────────────────────────────────────────────┘
                            ↓ SifRpc
┌─────────────────────────────────────────────────────────────┐
│  IOP (Input/Output Processor)                               │
│  ┌──────────────────────────────────────────────────────┐   │
│  │ Stack moderno (carregado por embedded_irx.cpp):      │   │
│  │                                                       │   │
│  │  1. sio2man.irx  ← transportador SIO2 (memory card) │   │
│  │         ↓                                             │   │
│  │  2. padman.irx   ← gerenciador de controle (PS2SDK) │   │
│  │         ↓                                             │   │
│  │  3. mtapman.irx  ← suporte a multitap (opcional)    │   │
│  └──────────────────────────────────────────────────────┘   │
└─────────────────────────────────────────────────────────────┘
```

### Mudanças no Código

#### 1. `src/platform/ps2/system/embedded_irx.cpp` (linhas 289-349)

Nova função **`PadLoadEmbeddedIrx()`** que:
- Carrega `padman.irx` embedado (obrigatório)
- Carrega `mtapman.irx` embedado (opcional, para multitap)
- Usa flag estática `s_pad_loaded_result` para ser re-entrante
- Retorna 0 em sucesso, -1 se padman falhou

```cpp
extern "C" int PadLoadEmbeddedIrx(void)
{
    // Carrega padman.irx do PS2SDK (embedado via bin2c)
    ret = EmbeddedIrxLoad(padman_irx, sizeof(padman_irx), 0, NULL);
    
    // Carrega mtapman.irx (falha é não-fatal)
    ret = EmbeddedIrxLoad(mtapman_irx, sizeof(mtapman_irx), 0, NULL);
}
```

#### 2. `src/platform/ps2/system/mainloop_iop.cpp` (linhas 279-327)

Sequência de carregamento moderna:

```cpp
BOOTLOG("[boot] PadLoadEmbeddedIrx: try\n");
if (PadLoadEmbeddedIrx() == 0)
{
    BOOTLOG("[boot] PadLoadEmbeddedIrx OK -> padInit/InputInit(FALSE)\n");
    if (padInit(0) != 1)
    {
        BOOTLOG("[boot] padInit failed -- controller unavailable\n");
    }
    else
    {
        InputInit(FALSE);  // ← FALSE = usa libpad, não xpad
        BOOTLOG("[boot] padInit/InputInit done\n");
    }
}
else
{
    // Fallback para rom0:PADMAN se o embedado falhar
    // (PS2SDK libpad detecta automaticamente ROM_PADMAN vs NEW_PADMAN)
    IOPLoadModule("rom0:PADMAN", NULL, 0, NULL);
    padInit(0);
    InputInit(FALSE);
}
```

**Chave**: `InputInit(FALSE)` força o uso da libpad padrão ao invés de libxpad.

#### 3. `src/platform/ps2/input/input.cpp` (linha 265)

```cpp
void InputInit(Bool bXLib)
{
    _Input_bXPad = bXLib;  // FALSE → usa libpad, não xpad
    // ...
}
```

Todas as funções de input (`_Input_GetPadState`, `_Input_InitPad`, etc.) verificam `_Input_bXPad` e chamam:
- `padPortOpen/padRead/padGetState` quando FALSE (libpad padrão)
- `xpadPortOpen/xpadRead/xpadGetState` quando TRUE (libxpad legado)

#### 4. Correções Críticas no `input.cpp`

##### a) `_Input_WaitReqComplete` (linha 85)
Aguarda que comandos `padSetMainMode` / `padEnterPressMode` completem no IOP antes de prosseguir. **Essencial no PS2 real** para garantir que o pad negocie modo DualShock.

```cpp
static void _Input_WaitReqComplete(int port, int slot)
{
    int tm = 50; // 5s timeout
    do {
        st = _Input_GetReqState(port, slot);
        if (st == PAD_RSTAT_COMPLETE || st == PAD_RSTAT_FAILED)
            break;
        usleep(100 * 1000);  // 100ms
    } while (--tm > 0);
}
```

##### b) `_Input_WaitPadReady` (linha 145)
Aguarda o pad atingir `PAD_STATE_STABLE` usando `usleep()` ao invés de `WaitForNextVRstart()` (que dependia de handlers INTC conflitantes com gsKit).

```cpp
static void _Input_WaitPadReady(int port, int slot)
{
    int tm = 50; // 5s timeout
    do {
        ret = _Input_GetPadState(port, slot);
        if (ret == PAD_STATE_STABLE || ret == PAD_STATE_DISCONN)
            break;
        usleep(100 * 1000);  // 100ms
    } while (--tm > 0);
}
```

##### c) Detecção de "ghost-pressed" reads (linha 408)
libpad pode retornar `btns == 0` (todos os botões pressionados) quando o buffer DMA ainda está zero-inicializado. Isso é fisicamente impossível no d-pad (UP+DOWN+LEFT+RIGHT simultaneamente) e causa cursores do menu se moverem sozinhos.

```cpp
if ((Uint16)padStatus.btns == 0 || padStatus.mode == 0)
{
    _Input_bPadConnected[iPad] = 1;
    continue;  // mantém estado anterior, tenta novamente no próximo frame
}
```

#### 5. `Makefile` (linhas 421-423)

Os IRX são embedados no ELF via `bin2c`:

```makefile
PADMAN_IRX_PATH  ?= $(PS2SDK)/iop/irx/padman.irx
MTAPMAN_IRX_PATH ?= $(PS2SDK)/iop/irx/mtapman.irx

$(EMBED_DIR)/padman_irx.h: $(PADMAN_IRX_PATH) | $(EMBED_DIR)
	$(call RUN_BIN2C,$<,$@,padman_irx)
$(EMBED_DIR)/mtapman_irx.h: $(MTAPMAN_IRX_PATH) | $(EMBED_DIR)
	$(call RUN_BIN2C,$<,$@,mtapman_irx)
```

---

## Resultado

✅ **Funciona no PS2 real** (hardware retail testado por usuários)  
✅ **Funciona no emulador** (PCSX2/NetherSX2)  
✅ **Suporta DualShock 2** (modo analógico com trava)  
✅ **Suporta controles digitais** (fallback automático)  
✅ **Suporta multitap** (via mtapman.irx, até 5 controles)  
✅ **ELF autocontido** (não precisa de IRX externos no disco)

---

## Referências

- **picodrive PS2** (irixxxx fork): `platform/ps2/plat.c` + `platform/ps2/in_ps2.c`
- **OPL** (Open PS2 Loader): sequência de carregamento de módulos IOP
- **hugorsgarcia/PS2SNESticle**: implementação de referência da stack moderna
- **PS2SDK libpad**: `/opt/ps2dev/ps2sdk/ee/include/libpad.h`
- **SIO2MAN conflict analysis**: discussão no ps2dev discord, 2024-2025

---

## Compilação

O código está pronto para compilar. Os IRX são gerados automaticamente durante o build:

```bash
make clean
make fast    # compila com paralelização
# ou
make all     # compila sequencial
```

Os headers `padman_irx.h` e `mtapman_irx.h` são gerados em `build/embed/` pelo `bin2c` e incluídos automaticamente por `embedded_irx.cpp`.

---

## Diagnóstico

Se o controle ainda não funcionar, verifique os logs SIO (conecte cabo serial ou veja log do emulador):

```
[boot] PadLoadEmbeddedIrx: try
[boot] PadLoadEmbeddedIrx OK -> padInit/InputInit(FALSE)
[boot] padInit/InputInit done
Input: pad p=0 s=0 opened, mode=0x7 (DS2/analog)
```

- **`mode=0x7`** = DualShock 2 (analógico) ✅
- **`mode=0x4`** = digital ⚠️ (funciona, mas sem analógico)
- **`padInit failed`** = padman.irx não carregou ❌

---

**Autor das correções**: Baseado em análise da comunidade ps2dev e implementação de referência de hugorsgarcia/PS2SNESticle  
**Data**: 2024-2025  
**Testado em**: PS2 Slim/Fat retail + PCSX2 + NetherSX2
