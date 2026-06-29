/*
 * sndsp4.cpp - DSP-4 (NEC uPD7725) coprocessor HLE
 *
 * Veja sndsp4.h para o contexto e a nota de clean-room.
 *
 * Resumo do que o chip faz em Top Gear 3000:
 *   - Comandos aritmeticos simples (multiplicacao 0x00, lookup 0x0A,
 *     mapeamento horizontal 0x11).
 *   - Comandos de PROJECAO ("streaming"): recebem os parametros da
 *     pista, projetam em perspectiva e devolvem, linha-a-linha, os
 *     valores que o jogo escreve nas tabelas de HDMA (ponteiro + scroll
 *     vertical/horizontal de BG1/BG2) e nas janelas (window) dos
 *     poligonos.  Tambem projetam a posicao dos sprites e empacotam OAM.
 *
 * O DSP-4 NAO mexe direto no PPU: ele so' calcula; o jogo e' que escreve
 * os registradores.  Por isso a "correcao" do Mode-7/HDMA depende apenas
 * de devolvermos os bytes corretos aqui.
 */

#include "types.h"
#include "sndsp4.h"
#include "console.h"

#include <string.h>
#include <stdio.h>

// Captura de protocolo (diagnostico).  Desligada por padrao; ligue com
// -DDSP4_CAPTURE=1 no build para gravar o stream no log (SIO/logs.txt).
#ifndef DSP4_CAPTURE
#define DSP4_CAPTURE 0
#endif

#if DSP4_CAPTURE
extern "C" void DLog(const char *fmt, ...);
#endif

// 1.7.8  -> 1.15.16   (sign-extend de 16 bits e desloca 8)
#define SEX78(a)  (((Int32)(Int16)(a)) << 8)
// 1.15.0 -> 1.15.16   (sign-extend de 16 bits e desloca 16)
#define SEX16(a)  (((Int32)(Int16)(a)) << 16)

// Suspende o comando streaming: zera o cursor de entrada, guarda o ponto
// de retomada e devolve o controle ao barramento (que vai juntar mais
// m_nInBytes bytes antes de re-chamar este mesmo comando).
#define DSP4_WAIT(n)  do { m_iInByte = 0; m_iResume = (n); return; } while (0)

//==========================================================================
//  Construcao / Reset
//==========================================================================
SNDSP4::SNDSP4()
{
    memset(m_SeenCmd, 0, sizeof(m_SeenCmd));
    Reset();
}

void SNDSP4::Reset()
{
    m_bWaitCmd  = TRUE;
    m_bHalfCmd  = FALSE;
    m_uCommand  = 0;

    m_nInBytes  = 0;
    m_iInByte   = 0;
    m_nOutBytes = 0;
    m_iOutByte  = 0;
    m_iResume   = 0;

    memset(m_Param, 0, sizeof(m_Param));
    memset(m_Out,   0, sizeof(m_Out));
    // m_SeenCmd nao e' limpo aqui (loga cada opcode uma vez por sessao).

    m_WorldX = m_WorldY = m_WorldDX = m_WorldDY = 0;
    m_WorldDDX = m_WorldDDY = 0;
    m_WorldXEnv = 0;
    m_WorldYOfs = 0;

    m_ViewX1 = m_ViewY1 = m_ViewX2 = m_ViewY2 = 0;
    m_ViewXOfs1 = m_ViewYOfs1 = m_ViewXOfs2 = m_ViewYOfs2 = 0;
    m_ViewDX = m_ViewDY = 0;
    m_ViewYOfsEnv = 0;
    m_ViewTurnoffX = m_ViewTurnoffDX = 0;

    m_Distance = m_Segments = m_Lcv = 0;

    memset(m_PolyBottom, 0, sizeof(m_PolyBottom));
    memset(m_PolyTop,    0, sizeof(m_PolyTop));
    memset(m_PolyCX,     0, sizeof(m_PolyCX));
    memset(m_PolyPtr,    0, sizeof(m_PolyPtr));
    memset(m_PolyRaster, 0, sizeof(m_PolyRaster));
    memset(m_PolyClipLf, 0, sizeof(m_PolyClipLf));
    memset(m_PolyClipRt, 0, sizeof(m_PolyClipRt));
    memset(m_PolyStart,  0, sizeof(m_PolyStart));
    memset(m_PolyPlane,  0, sizeof(m_PolyPlane));

    m_ViewportCX = m_ViewportCY = 0;
    m_ViewportLeft = m_ViewportRight = m_ViewportTop = m_ViewportBottom = 0;
    m_Raster = 0;
    m_SpriteX = m_SpriteY = m_SpriteAttr = 0;
    m_SpriteSize = m_SpriteClipY = 0;
    m_SpriteCount = 0;

    m_OAMRowMax = 0;
    memset(m_OAMRow,  0, sizeof(m_OAMRow));
    memset(m_OAMAttr, 0, sizeof(m_OAMAttr));
    m_OAMIndex = 0;
    m_OAMBits  = 0;
}

//==========================================================================
//  Helpers de buffer (little-endian)
//==========================================================================
Int16 SNDSP4::ReadWord()
{
    Int16 v = (Int16)((Uint16)m_Param[m_iInByte] |
                      ((Uint16)m_Param[m_iInByte + 1] << 8));
    m_iInByte += 2;
    return v;
}

Int32 SNDSP4::ReadDword()
{
    Int32 v = (Int32)((Uint32)m_Param[m_iInByte]
                    | ((Uint32)m_Param[m_iInByte + 1] << 8)
                    | ((Uint32)m_Param[m_iInByte + 2] << 16)
                    | ((Uint32)m_Param[m_iInByte + 3] << 24));
    m_iInByte += 4;
    return v;
}

void SNDSP4::WriteWord(Int32 d)
{
    if (m_nOutBytes + 2 <= (Int32)sizeof(m_Out)) {
        m_Out[m_nOutBytes + 0] = (Uint8)(d & 0xFF);
        m_Out[m_nOutBytes + 1] = (Uint8)((d >> 8) & 0xFF);
    }
    m_nOutBytes += 2;
}

void SNDSP4::WriteByte(Int32 d)
{
    if (m_nOutBytes + 1 <= (Int32)sizeof(m_Out))
        m_Out[m_nOutBytes] = (Uint8)(d & 0xFF);
    m_nOutBytes += 1;
}

void SNDSP4::ClearOut()
{
    m_nOutBytes = 0;
    m_iOutByte  = 0;
}

//==========================================================================
//  1/x aproximado por tabela (saturado a [0..63]).
//==========================================================================
Int16 SNDSP4::Inverse(Int16 value)
{
    static const Uint16 s_DivLut[64] =
    {
        0x0000, 0x8000, 0x4000, 0x2aaa, 0x2000, 0x1999, 0x1555, 0x1249,
        0x1000, 0x0e38, 0x0ccc, 0x0ba2, 0x0aaa, 0x09d8, 0x0924, 0x0888,
        0x0800, 0x0787, 0x071c, 0x06bc, 0x0666, 0x0618, 0x05d1, 0x0590,
        0x0555, 0x051e, 0x04ec, 0x04bd, 0x0492, 0x0469, 0x0444, 0x0421,
        0x0400, 0x03e0, 0x03c3, 0x03a8, 0x038e, 0x0375, 0x035e, 0x0348,
        0x0333, 0x031f, 0x030c, 0x02fa, 0x02e8, 0x02d8, 0x02c8, 0x02b9,
        0x02aa, 0x029c, 0x028f, 0x0282, 0x0276, 0x026a, 0x025e, 0x0253,
        0x0249, 0x023e, 0x0234, 0x022b, 0x0222, 0x0219, 0x0210, 0x0208
    };
    if (value < 0)  value = 0;
    if (value > 63) value = 63;
    return (Int16)s_DivLut[value];
}

//==========================================================================
//  Tabela de bytes de parametro por comando.  Retorna -1 para comandos
//  desconhecidos (que sao ignorados, mantendo a FSM sincronizada).
//==========================================================================
Int32 SNDSP4::CommandInBytes(Uint16 uCmd)
{
    switch (uCmd)
    {
    case 0x0000: return 4;
    case 0x0001: return 44;
    case 0x0003: return 0;
    case 0x0005: return 0;
    case 0x0006: return 0;
    case 0x0007: return 34;
    case 0x0008: return 90;
    case 0x0009: return 14;
    case 0x000A: return 6;
    case 0x000B: return 6;
    case 0x000D: return 42;
    case 0x000E: return 0;
    case 0x000F: return 46;
    case 0x0010: return 36;
    case 0x0011: return 8;

    // comandos de teste documentados (mantidos para o self-test do jogo)
    case 0x0013: return 0;   // Test Transfer DATA ROM
    case 0x0014: return 0;   // Test ROM Version

    default:     return -1;  // desconhecido -> ignora
    }
}

//==========================================================================
//  Despacho de comandos
//==========================================================================
void SNDSP4::Execute()
{
    switch (m_uCommand)
    {
    // ---- versao do chip ----
    case 0x0014:
        ClearOut();
        WriteWord(0x0400);   // 0x0400 identifica o DSP-4
        break;

    case 0x0013:
        // Dump da Data ROM do silicio: o HLE nao embute a ROM, devolve
        // vazio (a leitura subsequente serve 0xFFFF, o terminador).
        ClearOut();
        break;

    // ---- multiplicacao 16x16 ----
    case 0x0000:
    {
        Int16 mul   = ReadWord();
        Int16 mcand = ReadWord();
        Int32 prod  = ((Int32)mcand * (Int32)mul) << 1 >> 1;
        ClearOut();
        WriteWord(prod);
        WriteWord(prod >> 16);
        break;
    }

    case 0x0001: Op01(); break;   // projecao de pista (1P)
    case 0x0003: Op03(); break;   // selecao de sprite (1P)
    case 0x0005: Op05(); break;   // limpa OAM
    case 0x0006: Op06(); break;   // transfere OAM
    case 0x0007: Op07(); break;   // bifurcacao de pista (1P)
    case 0x0008: Op08(); break;   // poligono solido
    case 0x0009: Op09(); break;   // projecao de sprites

    // ---- lookup de 4 nibbles (0x0A) ----
    case 0x000A:
    {
        ReadWord();                 // palavra 0 (ignorada)
        Int16 n2 = ReadWord();      // palavra 1 (usada)
        ReadWord();                 // palavra 2 (ignorada)
        Int16 o1, o2, o3, o4;
        Op0A(n2, &o2, &o1, &o4, &o3);
        ClearOut();
        WriteWord(o1);
        WriteWord(o2);
        WriteWord(o3);
        WriteWord(o4);
        break;
    }

    // ---- emite um sprite OAM (0x0B) ----
    case 0x000B:
    {
        Int16 sp_x    = ReadWord();
        Int16 sp_y    = ReadWord();
        Int16 sp_attr = ReadWord();
        Bool  draw    = TRUE;
        ClearOut();
        Op0B(&draw, sp_x, sp_y, sp_attr, 0, 1);
        break;
    }

    case 0x000D: Op0D(); break;   // projecao de pista (multiplayer)
    case 0x000E: Op0E(); break;   // selecao (multiplayer)
    case 0x000F: Op0F(); break;   // projecao de pista com luz (1P)
    case 0x0010: Op10(); break;   // bifurcacao com luz (1P)

    // ---- mapeamento horizontal (0x11) ----
    case 0x0011:
    {
        Int16 d = ReadWord();
        Int16 c = ReadWord();
        Int16 b = ReadWord();
        Int16 a = ReadWord();
        Int16 m;
        Op11(a, b, c, d, &m);
        ClearOut();
        WriteWord(m);
        break;
    }

    default:
        break;
    }
}

//==========================================================================
//  Ops simples
//==========================================================================
void SNDSP4::Op03()
{
    m_OAMRowMax = 33;
    memset(m_OAMRow, 0, sizeof(m_OAMRow));
}

void SNDSP4::Op05()
{
    m_OAMIndex = 0;
    m_OAMBits  = 0;
    memset(m_OAMAttr, 0, sizeof(m_OAMAttr));
    m_SpriteCount = 0;
}

void SNDSP4::Op06()
{
    ClearOut();
    for (int i = 0; i < 16; i++)
        WriteWord(m_OAMAttr[i]);
}

void SNDSP4::Op0E()
{
    m_OAMRowMax = 16;
    memset(m_OAMRow, 0, sizeof(m_OAMRow));
}

void SNDSP4::Op0A(Int16 n, Int16 *o1, Int16 *o2, Int16 *o3, Int16 *o4)
{
    static const Uint16 s_Vals[16] =
    {
        0x0000, 0x0030, 0x0060, 0x0090, 0x00c0, 0x00f0, 0x0120, 0x0150,
        0xfe80, 0xfeb0, 0xfee0, 0xff10, 0xff40, 0xff70, 0xffa0, 0xffd0
    };
    *o4 = (Int16)s_Vals[(n & 0x000f)];
    *o3 = (Int16)s_Vals[(n & 0x00f0) >> 4];
    *o2 = (Int16)s_Vals[(n & 0x0f00) >> 8];
    *o1 = (Int16)s_Vals[(n & 0xf000) >> 12];
}

void SNDSP4::Op11(Int16 a, Int16 b, Int16 c, Int16 d, Int16 *m)
{
    // 0x155 = 341 = largura horizontal da tela
    *m = (Int16)(((a * 0x0155 >> 2)  & 0xf000)
              |  ((b * 0x0155 >> 6)  & 0x0f00)
              |  ((c * 0x0155 >> 10) & 0x00f0)
              |  ((d * 0x0155 >> 14) & 0x000f));
}

//==========================================================================
//  OAM packing (0x0B): decide se desenha o tile e empacota a saida.
//==========================================================================
void SNDSP4::Op0B(Bool *draw, Int16 sp_x, Int16 sp_y, Int16 sp_attr, Bool size, Bool stop)
{
    Int16 Row1 = (Int16)((sp_y >> 3) & 0x1f);
    Int16 Row2 = (Int16)((Row1 + 1) & 0x1f);

    // limites verticais
    if (!((sp_y < 0) || ((sp_y & 0x01ff) < 0x00eb)))
        *draw = 0;

    if (size) {
        if (m_OAMRow[Row1] + 1 >= m_OAMRowMax) *draw = 0;
        if (m_OAMRow[Row2] + 1 >= m_OAMRowMax) *draw = 0;
    } else {
        if (m_OAMRow[Row1] >= m_OAMRowMax) *draw = 0;
    }

    if (m_SpriteCount >= 128) *draw = 0;

    if (*draw)
    {
        if (size) { m_OAMRow[Row1] += 2; m_OAMRow[Row2] += 2; }
        else      { m_OAMRow[Row1] += 1; }

        // 1 = ha saida de OAM
        WriteWord(1);
        // empacota: x, y, name, attr
        WriteByte(sp_x & 0xff);
        WriteByte(sp_y & 0xff);
        WriteWord(sp_attr);

        m_SpriteCount++;

        // guarda bits de (MSB de X) e (tamanho) para recuperar depois
        if (m_OAMIndex < 16) {
            m_OAMAttr[m_OAMIndex] |= (Uint16)(((sp_x < 0 || sp_x > 255) ? 1 : 0) << m_OAMBits);
            m_OAMBits++;
            m_OAMAttr[m_OAMIndex] |= (Uint16)((size ? 1 : 0) << m_OAMBits);
            m_OAMBits++;
        } else {
            m_OAMBits += 2;
        }

        if (m_OAMBits == 16) { m_OAMBits = 0; m_OAMIndex++; }
    }
    else if (stop)
    {
        WriteWord(0);   // sem saida de OAM
    }
}

//==========================================================================
//  Op01 - projecao de pista (single-player)
//==========================================================================
void SNDSP4::Op01()
{
    m_bWaitCmd = FALSE;

    switch (m_iResume) {
    case 1: goto resume1;
    case 2: goto resume2;
    case 3: goto resume3;
    }

    // ------- ordena as entradas iniciais -------
    m_WorldY        = ReadDword();
    m_PolyBottom[0][0] = ReadWord();
    m_PolyTop[0][0]    = ReadWord();
    m_PolyCX[1][0]     = ReadWord();
    m_ViewportBottom   = ReadWord();
    m_WorldX        = ReadDword();
    m_PolyCX[0][0]     = ReadWord();
    m_PolyPtr[0][0]    = ReadWord();
    m_WorldYOfs        = ReadWord();
    m_WorldDY       = ReadDword();
    m_WorldDX       = ReadDword();
    m_Distance         = ReadWord();
    ReadWord();                       // 0x0000
    m_WorldXEnv     = ReadDword();
    m_WorldDDY         = ReadWord();
    m_WorldDDX         = ReadWord();
    m_ViewYOfsEnv      = ReadWord();

    // (x, y, offset) inicial na primeira linha de raster
    m_ViewX1    = (Int16)((m_WorldX + m_WorldXEnv) >> 16);
    m_ViewY1    = (Int16)(m_WorldY >> 16);
    m_ViewXOfs1 = (Int16)(m_WorldX >> 16);
    m_ViewYOfs1 = m_WorldYOfs;
    m_ViewTurnoffX  = 0;
    m_ViewTurnoffDX = 0;

    m_PolyRaster[0][0] = m_PolyBottom[0][0];

    do {
        // projecao em perspectiva dos pontos (x, y, scroll) do mundo
        m_ViewX2    = (Int16)((((m_WorldX + m_WorldXEnv) >> 16) * m_Distance >> 15)
                            + (m_ViewTurnoffX * m_Distance >> 15));
        m_ViewY2    = (Int16)((m_WorldY >> 16) * m_Distance >> 15);
        m_ViewXOfs2 = m_ViewX2;
        m_ViewYOfs2 = (Int16)((m_WorldYOfs * m_Distance >> 15) + m_PolyBottom[0][0] - m_ViewY2);

        ClearOut();
        WriteWord((m_WorldX + m_WorldXEnv) >> 16);
        WriteWord(m_ViewX2);
        WriteWord(m_WorldY >> 16);
        WriteWord(m_ViewY2);

        // numero de linhas de raster usadas
        m_Segments = (Int16)(m_PolyRaster[0][0] - m_ViewY2);

        if (m_ViewY2 >= m_PolyRaster[0][0])
            m_Segments = 0;
        else
            m_PolyRaster[0][0] = m_ViewY2;

        if (m_ViewY2 < m_PolyTop[0][0]) {
            m_Segments = 0;
            if (m_ViewY1 >= m_PolyTop[0][0])
                m_Segments = (Int16)(m_ViewY1 - m_PolyTop[0][0]);
        }

        WriteWord(m_Segments);

        if (m_Segments) {
            Int32 px_dx, py_dy;
            Int32 x_scroll, y_scroll;

            px_dx = (m_ViewXOfs2 - m_ViewXOfs1) * Inverse(m_Segments) << 1;
            py_dy = (m_ViewYOfs2 - m_ViewYOfs1) * Inverse(m_Segments) << 1;

            x_scroll = SEX16(m_PolyCX[0][0] + m_ViewXOfs1);
            y_scroll = SEX16(-m_ViewportBottom + m_ViewYOfs1 + m_ViewYOfsEnv
                           + m_PolyCX[1][0] - m_WorldYOfs);

            for (m_Lcv = 0; m_Lcv < m_Segments; m_Lcv++) {
                WriteWord(m_PolyPtr[0][0]);
                WriteWord((y_scroll + 0x8000) >> 16);
                WriteWord((x_scroll + 0x8000) >> 16);
                m_PolyPtr[0][0] -= 4;
                x_scroll += px_dx;
                y_scroll += py_dy;
            }
        }

        // pos-atualizacao
        m_ViewX1    = m_ViewX2;
        m_ViewY1    = m_ViewY2;
        m_ViewXOfs1 = m_ViewXOfs2;
        m_ViewYOfs1 = m_ViewYOfs2;

        m_WorldDX += SEX78(m_WorldDDX);
        m_WorldDY += SEX78(m_WorldDDY);
        m_WorldX  += (m_WorldDX + m_WorldXEnv);
        m_WorldY  += m_WorldDY;
        m_ViewTurnoffX += m_ViewTurnoffDX;

        // proximo comando
        m_nInBytes = 2;
        DSP4_WAIT(1);

    resume1:
        m_Distance = ReadWord();
        if (m_Distance == -0x8000)
            break;

        // bifurcacao da pista
        if ((Uint16)m_Distance == 0x8001) {
            m_nInBytes = 6;
            DSP4_WAIT(2);

        resume2:
            m_Distance       = ReadWord();
            m_ViewTurnoffX   = ReadWord();
            m_ViewTurnoffDX  = ReadWord();

            m_ViewX1    += (m_ViewTurnoffX * m_Distance >> 15);
            m_ViewXOfs1 += (m_ViewTurnoffX * m_Distance >> 15);
            m_ViewTurnoffX += m_ViewTurnoffDX;

            m_nInBytes = 2;
            DSP4_WAIT(1);
        }

        m_nInBytes = 6;
        DSP4_WAIT(3);

    resume3:
        m_WorldDDY    = ReadWord();
        m_WorldDDX    = ReadWord();
        m_ViewYOfsEnv = ReadWord();
        m_WorldXEnv   = 0;
    } while (1);

    m_bWaitCmd = TRUE;
}

//==========================================================================
//  Op07 - projecao da bifurcacao de pista (single-player)
//==========================================================================
void SNDSP4::Op07()
{
    m_bWaitCmd = FALSE;

    switch (m_iResume) {
    case 1: goto resume1;
    case 2: goto resume2;
    }

    m_WorldY        = ReadDword();
    m_PolyBottom[0][0] = ReadWord();
    m_PolyTop[0][0]    = ReadWord();
    m_PolyCX[1][0]     = ReadWord();
    m_ViewportBottom   = ReadWord();
    m_WorldX        = ReadDword();
    m_PolyCX[0][0]     = ReadWord();
    m_PolyPtr[0][0]    = ReadWord();
    m_WorldYOfs        = ReadWord();
    m_Distance         = ReadWord();
    m_ViewY2           = ReadWord();
    m_ViewDY           = (Int16)(ReadWord() * m_Distance >> 15);
    m_ViewX2           = ReadWord();
    m_ViewDX           = (Int16)(ReadWord() * m_Distance >> 15);
    m_ViewYOfsEnv      = ReadWord();

    m_ViewX1    = (Int16)(m_WorldX >> 16);
    m_ViewY1    = (Int16)(m_WorldY >> 16);
    m_ViewXOfs1 = m_ViewX1;
    m_ViewYOfs1 = m_WorldYOfs;

    m_PolyRaster[0][0] = m_PolyBottom[0][0];

    do {
        m_ViewX2 += m_ViewDX;
        m_ViewY2 += m_ViewDY;

        m_ViewXOfs2 = m_ViewX2;
        m_ViewYOfs2 = (Int16)((m_WorldYOfs * m_Distance >> 15) + m_PolyBottom[0][0] - m_ViewY2);

        ClearOut();
        WriteWord(m_ViewX2);
        WriteWord(m_ViewY2);

        m_Segments = (Int16)(m_ViewY1 - m_ViewY2);

        if (m_ViewY2 >= m_PolyRaster[0][0])
            m_Segments = 0;
        else
            m_PolyRaster[0][0] = m_ViewY2;

        if (m_ViewY2 < m_PolyTop[0][0]) {
            m_Segments = 0;
            if (m_ViewY1 >= m_PolyTop[0][0])
                m_Segments = (Int16)(m_ViewY1 - m_PolyTop[0][0]);
        }

        WriteWord(m_Segments);

        if (m_Segments) {
            Int32 px_dx, py_dy;
            Int32 x_scroll, y_scroll;

            px_dx = (m_ViewXOfs2 - m_ViewXOfs1) * Inverse(m_Segments) << 1;
            py_dy = (m_ViewYOfs2 - m_ViewYOfs1) * Inverse(m_Segments) << 1;

            x_scroll = SEX16(m_PolyCX[0][0] + m_ViewXOfs1);
            y_scroll = SEX16(-m_ViewportBottom + m_ViewYOfs1 + m_ViewYOfsEnv
                           + m_PolyCX[1][0] - m_WorldYOfs);

            for (m_Lcv = 0; m_Lcv < m_Segments; m_Lcv++) {
                WriteWord(m_PolyPtr[0][0]);
                WriteWord((y_scroll + 0x8000) >> 16);
                WriteWord((x_scroll + 0x8000) >> 16);
                m_PolyPtr[0][0] -= 4;
                x_scroll += px_dx;
                y_scroll += py_dy;
            }
        }

        m_ViewX1    = m_ViewX2;
        m_ViewY1    = m_ViewY2;
        m_ViewXOfs1 = m_ViewXOfs2;
        m_ViewYOfs1 = m_ViewYOfs2;

        m_nInBytes = 2;
        DSP4_WAIT(1);

    resume1:
        m_Distance = ReadWord();
        if (m_Distance == -0x8000)
            break;

        m_nInBytes = 10;
        DSP4_WAIT(2);

    resume2:
        m_ViewY2      = ReadWord();
        m_ViewDY      = (Int16)(ReadWord() * m_Distance >> 15);
        m_ViewX2      = ReadWord();
        m_ViewDX      = (Int16)(ReadWord() * m_Distance >> 15);
        m_ViewYOfsEnv = ReadWord();
    } while (1);

    m_bWaitCmd = TRUE;
}

//==========================================================================
//  Op08 - poligono solido (2 formas)
//==========================================================================
void SNDSP4::Op08()
{
    Int16 win_left, win_right;
    Int16 view_x[2], view_y[2];
    Int16 envelope[2][2];

    m_bWaitCmd = FALSE;

    switch (m_iResume) {
    case 1: goto resume1;
    case 2: goto resume2;
    }

    m_PolyClipRt[0][0] = ReadWord();
    m_PolyClipRt[0][1] = ReadWord();
    m_PolyClipRt[1][0] = ReadWord();
    m_PolyClipRt[1][1] = ReadWord();

    m_PolyClipLf[0][0] = ReadWord();
    m_PolyClipLf[0][1] = ReadWord();
    m_PolyClipLf[1][0] = ReadWord();
    m_PolyClipLf[1][1] = ReadWord();

    ReadWord(); ReadWord(); ReadWord(); ReadWord();   // constante
    ReadWord(); ReadWord(); ReadWord(); ReadWord();   // constante

    m_PolyCX[0][0] = ReadWord();
    m_PolyCX[0][1] = ReadWord();
    m_PolyCX[1][0] = ReadWord();
    m_PolyCX[1][1] = ReadWord();

    m_PolyPtr[0][0] = ReadWord();
    m_PolyPtr[0][1] = ReadWord();
    m_PolyPtr[1][0] = ReadWord();
    m_PolyPtr[1][1] = ReadWord();

    m_PolyBottom[0][0] = ReadWord();
    m_PolyBottom[0][1] = ReadWord();
    m_PolyBottom[1][0] = ReadWord();
    m_PolyBottom[1][1] = ReadWord();

    m_PolyTop[0][0] = ReadWord();
    m_PolyTop[0][1] = ReadWord();
    m_PolyTop[1][0] = ReadWord();
    m_PolyTop[1][1] = ReadWord();

    ReadWord(); ReadWord(); ReadWord(); ReadWord();   // desconhecido

    m_Distance = ReadWord();
    view_x[0]  = ReadWord();
    view_y[0]  = ReadWord();
    view_x[1]  = ReadWord();
    view_y[1]  = ReadWord();

    envelope[0][0] = ReadWord();
    envelope[0][1] = ReadWord();
    envelope[1][0] = ReadWord();
    envelope[1][1] = ReadWord();

    m_PolyStart[0] = view_x[0];
    m_PolyStart[1] = view_x[1];

    m_PolyRaster[0][0] = view_y[0];
    m_PolyRaster[0][1] = view_y[0];
    m_PolyRaster[1][0] = view_y[1];
    m_PolyRaster[1][1] = view_y[1];

    m_PolyPlane[0] = m_Distance;
    m_PolyPlane[1] = m_Distance;

    win_left  = (Int16)(m_PolyCX[0][0] - view_x[0] + envelope[0][0]);
    win_right = (Int16)(m_PolyCX[0][1] - view_x[0] + envelope[0][1]);

    if (win_left  < m_PolyClipLf[0][0]) win_left  = m_PolyClipLf[0][0];
    if (win_left  > m_PolyClipRt[0][0]) win_left  = m_PolyClipRt[0][0];
    if (win_right < m_PolyClipLf[0][1]) win_right = m_PolyClipLf[0][1];
    if (win_right > m_PolyClipRt[0][1]) win_right = m_PolyClipRt[0][1];

    ClearOut();
    WriteByte(win_left  & 0xff);
    WriteByte(win_right & 0xff);

    do {
        Int16 polygon;

        m_nInBytes = 2;
        DSP4_WAIT(1);

    resume1:
        m_Distance = ReadWord();
        if (m_Distance == -0x8000)
            break;

        m_nInBytes = 16;
        DSP4_WAIT(2);

    resume2:
        view_x[0] = ReadWord();
        view_y[0] = ReadWord();
        view_x[1] = ReadWord();
        view_y[1] = ReadWord();

        envelope[0][0] = ReadWord();
        envelope[0][1] = ReadWord();
        envelope[1][0] = ReadWord();
        envelope[1][1] = ReadWord();

        ClearOut();

        for (polygon = 0; polygon < 2; polygon++) {
            Int32 left_inc, right_inc;
            Int16 x1_final, x2_final;
            Int16 env[2][2];
            Int16 poly;

            m_Segments = (Int16)(m_PolyRaster[polygon][0] - view_y[polygon]);

            if (m_Segments > 0) {
                m_PolyRaster[polygon][0] = view_y[polygon];
                m_PolyRaster[polygon][1] = view_y[polygon];
            } else
                m_Segments = 0;

            if (view_y[polygon] < m_PolyTop[polygon][0]) {
                m_Segments = 0;
                if (view_y[polygon] >= m_PolyTop[polygon][0])
                    m_Segments = (Int16)(view_y[polygon] - m_PolyTop[polygon][0]);
            }

            WriteWord(m_Segments);

            poly = polygon;

            if (m_Segments) {
                Int32 w_left, w_right;

                if ((Uint16)envelope[polygon][0] == (Uint16)0xc001)
                    poly = 1;
                else if (envelope[polygon][1] == 0x3fff)
                    poly = 1;

                // lado esquerdo
                env[0][0] = (Int16)(envelope[polygon][0] * m_PolyPlane[poly] >> 15);
                env[0][1] = (Int16)(envelope[polygon][0] * m_Distance >> 15);

                x1_final = (Int16)(view_x[poly] + env[0][0]);
                x2_final = (Int16)(m_PolyStart[poly] + env[0][1]);

                left_inc = (x2_final - x1_final) * Inverse(m_Segments) << 1;
                if (m_Segments == 1) left_inc = -left_inc;

                // lado direito
                env[1][0] = (Int16)(envelope[polygon][1] * m_PolyPlane[poly] >> 15);
                env[1][1] = (Int16)(envelope[polygon][1] * m_Distance >> 15);

                x1_final = (Int16)(view_x[poly] + env[1][0]);
                x2_final = (Int16)(m_PolyStart[poly] + env[1][1]);

                right_inc = (x2_final - x1_final) * Inverse(m_Segments) << 1;
                if (m_Segments == 1) right_inc = -right_inc;

                w_left  = SEX16(m_PolyCX[polygon][0] - m_PolyStart[poly] + env[0][0]);
                w_right = SEX16(m_PolyCX[polygon][1] - m_PolyStart[poly] + env[1][0]);

                m_PolyPlane[polygon] = m_Distance;

                for (m_Lcv = 0; m_Lcv < m_Segments; m_Lcv++) {
                    Int16 x_left, x_right;

                    w_left  += left_inc;
                    w_right += right_inc;

                    x_left  = (Int16)(w_left  >> 16);
                    x_right = (Int16)(w_right >> 16);

                    if (x_left  < m_PolyClipLf[polygon][0]) x_left  = m_PolyClipLf[polygon][0];
                    if (x_left  > m_PolyClipRt[polygon][0]) x_left  = m_PolyClipRt[polygon][0];
                    if (x_right < m_PolyClipLf[polygon][1]) x_right = m_PolyClipLf[polygon][1];
                    if (x_right > m_PolyClipRt[polygon][1]) x_right = m_PolyClipRt[polygon][1];

                    WriteWord(m_PolyPtr[polygon][0]);
                    WriteByte(x_left  & 0xff);
                    WriteByte(x_right & 0xff);

                    m_PolyPtr[polygon][0] -= 4;
                    m_PolyPtr[polygon][1] -= 4;
                }
            }

            m_PolyStart[polygon] = view_x[poly];
        }
    } while (1);

    ClearOut();
    WriteWord(0);

    m_bWaitCmd = TRUE;
}

//==========================================================================
//  Op09 - projecao de sprites
//==========================================================================
void SNDSP4::Op09()
{
    m_bWaitCmd = FALSE;

    switch (m_iResume) {
    case 1: goto resume1;
    case 2: goto resume2;
    case 3: goto resume3;
    case 4: goto resume4;
    case 5: goto resume5;
    case 6: goto resume6;
    }

    m_ViewportCX     = ReadWord();
    m_ViewportCY     = ReadWord();
    ReadWord();                       // 0x0000
    m_ViewportLeft   = ReadWord();
    m_ViewportRight  = ReadWord();
    m_ViewportTop    = ReadWord();
    m_ViewportBottom = ReadWord();

    m_PolyBottom[0][0] = (Int16)(m_ViewportBottom - m_ViewportCY);
    m_PolyRaster[0][0] = 0x100;

    do {
        m_nInBytes = 4;
        DSP4_WAIT(1);

    resume1:
        m_Raster = ReadWord();

        if (m_Raster < m_PolyRaster[0][0]) {
            m_SpriteClipY = (Int16)(m_ViewportBottom - (m_PolyBottom[0][0] - m_Raster));
            m_PolyRaster[0][0] = m_Raster;
        }

        m_Distance = ReadWord();
        if (m_Distance == -0x8000)
            goto terminate;

        if (m_Distance == 0x0000)
            continue;

        if ((Uint16)m_Distance == 0x9000) {
            // sprite do veiculo
            Int16  car_left, car_right, car_back;
            Int16  impact_left, impact_back;
            Int16  world_spx, world_spy;
            Int16  view_spx, view_spy;
            Uint16 energy;

            m_nInBytes = 14;
            DSP4_WAIT(2);

        resume2:
            energy      = (Uint16)ReadWord();
            impact_back = ReadWord();
            car_back    = ReadWord();
            impact_left = ReadWord();
            car_left    = ReadWord();
            m_Distance  = ReadWord();
            car_right   = ReadWord();

            world_spx = (Int16)(car_right - car_left);
            world_spy = car_back;

            world_spx -= (Int16)(energy * (impact_left - car_left) >> 16);
            world_spy -= (Int16)(energy * (car_back - impact_back) >> 16);

            view_spx = (Int16)(world_spx * m_Distance >> 15);
            view_spy = (Int16)(world_spy * m_Distance >> 15);

            m_SpriteX = (Int16)(m_ViewportCX + view_spx);
            m_SpriteY = (Int16)(m_ViewportBottom - (m_PolyBottom[0][0] - view_spy));

            ClearOut();
            WriteWord(world_spx);

            m_nInBytes = 4;
            DSP4_WAIT(3);

        resume3:
            m_SpriteY += ReadWord();
        }
        else {
            // sprite de terreno
            Int16 world_spx, world_spy;
            Int16 view_spx, view_spy;

            m_nInBytes = 10;
            DSP4_WAIT(4);

        resume4:
            m_PolyCX[0][0]     = ReadWord();
            m_PolyRaster[0][1] = ReadWord();
            world_spx          = ReadWord();
            world_spy          = ReadWord();

            m_Segments = (Int16)(m_PolyBottom[0][0] - m_Raster);

            view_spx = (Int16)(world_spx * m_Distance >> 15);
            view_spy = (Int16)(world_spy * m_Distance >> 15);

            m_SpriteX = (Int16)(m_ViewportCX + view_spx - m_PolyCX[0][0]);
            m_SpriteY = (Int16)(m_ViewportBottom - m_Segments + view_spy);
        }

        m_SpriteSize = 1;
        m_SpriteAttr = ReadWord();

        // converte os dados de tile para o formato OAM do SNES
        do {
            Int16  sp_x, sp_y, sp_attr, sp_dattr;
            Int16  sp_dx, sp_dy;
            Int16  pixels;
            Uint16 header;
            Bool   draw;

            m_nInBytes = 2;
            DSP4_WAIT(5);

        resume5:
            draw = TRUE;

            m_Raster = ReadWord();
            if (m_Raster == -0x8000)
                goto terminate;

            if (m_Raster == 0x0000 && !m_SpriteSize)
                break;

            if (m_Raster == 0x0000) {
                m_SpriteSize = !m_SpriteSize;
                continue;
            }

            header = (Uint16)m_Raster;
            header >>= 8;
            if (header != 0x20 && header != 0x2e && header != 0x40 &&
                header != 0x60 && header != 0xa0 && header != 0xc0 &&
                header != 0xe0)
                break;

            m_nInBytes = 4;
            DSP4_WAIT(6);

        resume6:
            draw = TRUE;

            sp_dattr = m_Raster;
            sp_dy = ReadWord();
            sp_dx = ReadWord();

            sp_x = (Int16)(m_SpriteX + sp_dx);
            sp_y = (Int16)(m_SpriteY + sp_dy);

            sp_attr = (Int16)(m_SpriteAttr + sp_dattr);

            pixels = m_SpriteSize ? 15 : 7;

            ClearOut();

            // tile transparente para cortar partes do sprite (overdraw)
            if (m_SpriteClipY - pixels <= sp_y && sp_y <= m_SpriteClipY &&
                sp_x >= m_ViewportLeft - pixels && sp_x <= m_ViewportRight &&
                m_SpriteClipY >= m_ViewportTop - pixels && m_SpriteClipY <= m_ViewportBottom)
                Op0B(&draw, sp_x, m_SpriteClipY, 0x00EE, m_SpriteSize, 0);

            // tile normal do sprite
            if (sp_x >= m_ViewportLeft - pixels && sp_x <= m_ViewportRight &&
                sp_y >= m_ViewportTop - pixels && sp_y <= m_ViewportBottom &&
                sp_y <= m_SpriteClipY)
                Op0B(&draw, sp_x, sp_y, sp_attr, m_SpriteSize, 0);

            // sem dados de OAM seguintes
            Op0B(&draw, 0, 0x0100, 0, 0, 1);
        } while (1);
    } while (1);

terminate:
    m_bWaitCmd = TRUE;
}

//==========================================================================
//  Op0D - projecao de pista (multiplayer)
//==========================================================================
void SNDSP4::Op0D()
{
    m_bWaitCmd = FALSE;

    switch (m_iResume) {
    case 1: goto resume1;
    case 2: goto resume2;
    }

    m_WorldY        = ReadDword();
    m_PolyBottom[0][0] = ReadWord();
    m_PolyTop[0][0]    = ReadWord();
    m_PolyCX[1][0]     = ReadWord();
    m_ViewportBottom   = ReadWord();
    m_WorldX        = ReadDword();
    m_PolyCX[0][0]     = ReadWord();
    m_PolyPtr[0][0]    = ReadWord();
    m_WorldYOfs        = ReadWord();
    m_WorldDY       = ReadDword();
    m_WorldDX       = ReadDword();
    m_Distance         = ReadWord();
    ReadWord();                       // 0x0000
    m_WorldXEnv     = SEX78(ReadWord());
    m_WorldDDY         = ReadWord();
    m_WorldDDX         = ReadWord();
    m_ViewYOfsEnv      = ReadWord();

    m_ViewX1    = (Int16)((m_WorldX + m_WorldXEnv) >> 16);
    m_ViewY1    = (Int16)(m_WorldY >> 16);
    m_ViewXOfs1 = (Int16)(m_WorldX >> 16);
    m_ViewYOfs1 = m_WorldYOfs;

    m_PolyRaster[0][0] = m_PolyBottom[0][0];

    do {
        m_ViewX2    = (Int16)((((m_WorldX + m_WorldXEnv) >> 16) * m_Distance >> 15)
                            + (m_ViewTurnoffX * m_Distance >> 15));
        m_ViewY2    = (Int16)((m_WorldY >> 16) * m_Distance >> 15);
        m_ViewXOfs2 = m_ViewX2;
        m_ViewYOfs2 = (Int16)((m_WorldYOfs * m_Distance >> 15) + m_PolyBottom[0][0] - m_ViewY2);

        ClearOut();
        WriteWord((m_WorldX + m_WorldXEnv) >> 16);
        WriteWord(m_ViewX2);
        WriteWord(m_WorldY >> 16);
        WriteWord(m_ViewY2);

        m_Segments = (Int16)(m_ViewY1 - m_ViewY2);

        if (m_ViewY2 >= m_PolyRaster[0][0])
            m_Segments = 0;
        else
            m_PolyRaster[0][0] = m_ViewY2;

        if (m_ViewY2 < m_PolyTop[0][0]) {
            m_Segments = 0;
            if (m_ViewY1 >= m_PolyTop[0][0])
                m_Segments = (Int16)(m_ViewY1 - m_PolyTop[0][0]);
        }

        WriteWord(m_Segments);

        if (m_Segments) {
            Int32 px_dx, py_dy;
            Int32 x_scroll, y_scroll;

            px_dx = (m_ViewXOfs2 - m_ViewXOfs1) * Inverse(m_Segments) << 1;
            py_dy = (m_ViewYOfs2 - m_ViewYOfs1) * Inverse(m_Segments) << 1;

            x_scroll = SEX16(m_PolyCX[0][0] + m_ViewXOfs1);
            y_scroll = SEX16(-m_ViewportBottom + m_ViewYOfs1 + m_ViewYOfsEnv
                           + m_PolyCX[1][0] - m_WorldYOfs);

            for (m_Lcv = 0; m_Lcv < m_Segments; m_Lcv++) {
                WriteWord(m_PolyPtr[0][0]);
                WriteWord((y_scroll + 0x8000) >> 16);
                WriteWord((x_scroll + 0x8000) >> 16);
                m_PolyPtr[0][0] -= 4;
                x_scroll += px_dx;
                y_scroll += py_dy;
            }
        }

        m_ViewX1    = m_ViewX2;
        m_ViewY1    = m_ViewY2;
        m_ViewXOfs1 = m_ViewXOfs2;
        m_ViewYOfs1 = m_ViewYOfs2;

        m_WorldDX += SEX78(m_WorldDDX);
        m_WorldDY += SEX78(m_WorldDDY);
        m_WorldX  += (m_WorldDX + m_WorldXEnv);
        m_WorldY  += m_WorldDY;

        m_nInBytes = 2;
        DSP4_WAIT(1);

    resume1:
        m_Distance = ReadWord();
        if (m_Distance == -0x8000)
            break;

        m_nInBytes = 6;
        DSP4_WAIT(2);

    resume2:
        m_WorldDDY    = ReadWord();
        m_WorldDDX    = ReadWord();
        m_ViewYOfsEnv = ReadWord();
        m_WorldXEnv   = 0;
    } while (1);

    m_bWaitCmd = TRUE;
}

//==========================================================================
//  Op0F - projecao de pista com iluminacao dinamica (single-player)
//==========================================================================
void SNDSP4::Op0F()
{
    m_bWaitCmd = FALSE;

    switch (m_iResume) {
    case 1: goto resume1;
    case 2: goto resume2;
    case 3: goto resume3;
    case 4: goto resume4;
    }

    ReadWord();                       // 0x0000
    m_WorldY        = ReadDword();
    m_PolyBottom[0][0] = ReadWord();
    m_PolyTop[0][0]    = ReadWord();
    m_PolyCX[1][0]     = ReadWord();
    m_ViewportBottom   = ReadWord();
    m_WorldX        = ReadDword();
    m_PolyCX[0][0]     = ReadWord();
    m_PolyPtr[0][0]    = ReadWord();
    m_WorldYOfs        = ReadWord();
    m_WorldDY       = ReadDword();
    m_WorldDX       = ReadDword();
    m_Distance         = ReadWord();
    ReadWord();                       // 0x0000
    m_WorldXEnv     = ReadDword();
    m_WorldDDY         = ReadWord();
    m_WorldDDX         = ReadWord();
    m_ViewYOfsEnv      = ReadWord();

    m_ViewX1    = (Int16)((m_WorldX + m_WorldXEnv) >> 16);
    m_ViewY1    = (Int16)(m_WorldY >> 16);
    m_ViewXOfs1 = (Int16)(m_WorldX >> 16);
    m_ViewYOfs1 = m_WorldYOfs;
    m_ViewTurnoffX  = 0;
    m_ViewTurnoffDX = 0;

    m_PolyRaster[0][0] = m_PolyBottom[0][0];

    do {
        m_ViewX2    = (Int16)(((m_WorldX + m_WorldXEnv) >> 16) * m_Distance >> 15);
        m_ViewY2    = (Int16)((m_WorldY >> 16) * m_Distance >> 15);
        m_ViewXOfs2 = m_ViewX2;
        m_ViewYOfs2 = (Int16)((m_WorldYOfs * m_Distance >> 15) + m_PolyBottom[0][0] - m_ViewY2);

        ClearOut();
        WriteWord((m_WorldX + m_WorldXEnv) >> 16);
        WriteWord(m_ViewX2);
        WriteWord(m_WorldY >> 16);
        WriteWord(m_ViewY2);

        m_Segments = (Int16)(m_PolyRaster[0][0] - m_ViewY2);

        if (m_ViewY2 >= m_PolyRaster[0][0])
            m_Segments = 0;
        else
            m_PolyRaster[0][0] = m_ViewY2;

        if (m_ViewY2 < m_PolyTop[0][0]) {
            m_Segments = 0;
            if (m_ViewY1 >= m_PolyTop[0][0])
                m_Segments = (Int16)(m_ViewY1 - m_PolyTop[0][0]);
        }

        WriteWord(m_Segments);

        if (m_Segments) {
            Int32 px_dx, py_dy;
            Int32 x_scroll, y_scroll;

            // iluminacao dinamica: 4 blocos de (distancia, cor)
            for (m_Lcv = 0; m_Lcv < 4; m_Lcv++) {
                m_nInBytes = 4;
                DSP4_WAIT(1);

            resume1:
                {
                    Int16 dist  = ReadWord();
                    Int16 color = ReadWord();
                    Int16 red   =  color        & 0x1f;
                    Int16 green = (color >>  5) & 0x1f;
                    Int16 blue  = (color >> 10) & 0x1f;

                    red   = (Int16)((red   * dist >> 15) & 0x1f);
                    green = (Int16)((green * dist >> 15) & 0x1f);
                    blue  = (Int16)((blue  * dist >> 15) & 0x1f);
                    color = (Int16)(red | (green << 5) | (blue << 10));

                    ClearOut();
                    WriteWord(color);
                }
            }

            px_dx = (m_ViewXOfs2 - m_ViewXOfs1) * Inverse(m_Segments) << 1;
            py_dy = (m_ViewYOfs2 - m_ViewYOfs1) * Inverse(m_Segments) << 1;

            x_scroll = SEX16(m_PolyCX[0][0] + m_ViewXOfs1);
            y_scroll = SEX16(-m_ViewportBottom + m_ViewYOfs1 + m_ViewYOfsEnv
                           + m_PolyCX[1][0] - m_WorldYOfs);

            for (m_Lcv = 0; m_Lcv < m_Segments; m_Lcv++) {
                WriteWord(m_PolyPtr[0][0]);
                WriteWord((y_scroll + 0x8000) >> 16);
                WriteWord((x_scroll + 0x8000) >> 16);
                m_PolyPtr[0][0] -= 4;
                x_scroll += px_dx;
                y_scroll += py_dy;
            }
        }

        m_ViewX1    = m_ViewX2;
        m_ViewY1    = m_ViewY2;
        m_ViewXOfs1 = m_ViewXOfs2;
        m_ViewYOfs1 = m_ViewYOfs2;

        m_WorldDX += SEX78(m_WorldDDX);
        m_WorldDY += SEX78(m_WorldDDY);
        m_WorldX  += (m_WorldDX + m_WorldXEnv);
        m_WorldY  += m_WorldDY;
        m_ViewTurnoffX += m_ViewTurnoffDX;

        m_nInBytes = 2;
        DSP4_WAIT(2);

    resume2:
        m_Distance = ReadWord();
        if (m_Distance == -0x8000)
            break;

        if ((Uint16)m_Distance == 0x8001) {
            m_nInBytes = 6;
            DSP4_WAIT(3);

        resume3:
            m_Distance      = ReadWord();
            m_ViewTurnoffX  = ReadWord();
            m_ViewTurnoffDX = ReadWord();

            m_ViewX1    += (m_ViewTurnoffX * m_Distance >> 15);
            m_ViewXOfs1 += (m_ViewTurnoffX * m_Distance >> 15);
            m_ViewTurnoffX += m_ViewTurnoffDX;

            m_nInBytes = 2;
            DSP4_WAIT(2);
        }

        m_nInBytes = 6;
        DSP4_WAIT(4);

    resume4:
        m_WorldDDY    = ReadWord();
        m_WorldDDX    = ReadWord();
        m_ViewYOfsEnv = ReadWord();
        m_WorldXEnv   = 0;
    } while (1);

    m_bWaitCmd = TRUE;
}

//==========================================================================
//  Op10 - projecao da bifurcacao com iluminacao (single-player)
//==========================================================================
void SNDSP4::Op10()
{
    m_bWaitCmd = FALSE;

    switch (m_iResume) {
    case 1: goto resume1;
    case 2: goto resume2;
    case 3: goto resume3;
    }

    ReadWord();                       // 0x0000
    m_WorldY        = ReadDword();
    m_PolyBottom[0][0] = ReadWord();
    m_PolyTop[0][0]    = ReadWord();
    m_PolyCX[1][0]     = ReadWord();
    m_ViewportBottom   = ReadWord();
    m_WorldX        = ReadDword();
    m_PolyCX[0][0]     = ReadWord();
    m_PolyPtr[0][0]    = ReadWord();
    m_WorldYOfs        = ReadWord();
    m_Distance         = ReadWord();
    m_ViewY2           = ReadWord();
    m_ViewDY           = (Int16)(ReadWord() * m_Distance >> 15);
    m_ViewX2           = ReadWord();
    m_ViewDX           = (Int16)(ReadWord() * m_Distance >> 15);
    m_ViewYOfsEnv      = ReadWord();

    m_ViewX1    = (Int16)(m_WorldX >> 16);
    m_ViewY1    = (Int16)(m_WorldY >> 16);
    m_ViewXOfs1 = m_ViewX1;
    m_ViewYOfs1 = m_WorldYOfs;

    m_PolyRaster[0][0] = m_PolyBottom[0][0];

    do {
        m_ViewX2 += m_ViewDX;
        m_ViewY2 += m_ViewDY;

        m_ViewXOfs2 = m_ViewX2;
        m_ViewYOfs2 = (Int16)((m_WorldYOfs * m_Distance >> 15) + m_PolyBottom[0][0] - m_ViewY2);

        ClearOut();
        WriteWord(m_ViewX2);
        WriteWord(m_ViewY2);

        m_Segments = (Int16)(m_ViewY1 - m_ViewY2);

        if (m_ViewY2 >= m_PolyRaster[0][0])
            m_Segments = 0;
        else
            m_PolyRaster[0][0] = m_ViewY2;

        if (m_ViewY2 < m_PolyTop[0][0]) {
            m_Segments = 0;
            if (m_ViewY1 >= m_PolyTop[0][0])
                m_Segments = (Int16)(m_ViewY1 - m_PolyTop[0][0]);
        }

        WriteWord(m_Segments);

        if (m_Segments) {
            for (m_Lcv = 0; m_Lcv < 4; m_Lcv++) {
                m_nInBytes = 4;
                DSP4_WAIT(1);

            resume1:
                {
                    Int16 dist  = ReadWord();
                    Int16 color = ReadWord();
                    Int16 red   =  color        & 0x1f;
                    Int16 green = (color >>  5) & 0x1f;
                    Int16 blue  = (color >> 10) & 0x1f;

                    red   = (Int16)((red   * dist >> 15) & 0x1f);
                    green = (Int16)((green * dist >> 15) & 0x1f);
                    blue  = (Int16)((blue  * dist >> 15) & 0x1f);
                    color = (Int16)(red | (green << 5) | (blue << 10));

                    ClearOut();
                    WriteWord(color);
                }
            }
        }

        if (m_Segments) {
            Int32 px_dx, py_dy;
            Int32 x_scroll, y_scroll;

            px_dx = (m_ViewXOfs2 - m_ViewXOfs1) * Inverse(m_Segments) << 1;
            py_dy = (m_ViewYOfs2 - m_ViewYOfs1) * Inverse(m_Segments) << 1;

            x_scroll = SEX16(m_PolyCX[0][0] + m_ViewXOfs1);
            y_scroll = SEX16(-m_ViewportBottom + m_ViewYOfs1 + m_ViewYOfsEnv
                           + m_PolyCX[1][0] - m_WorldYOfs);

            for (m_Lcv = 0; m_Lcv < m_Segments; m_Lcv++) {
                WriteWord(m_PolyPtr[0][0]);
                WriteWord((y_scroll + 0x8000) >> 16);
                WriteWord((x_scroll + 0x8000) >> 16);
                m_PolyPtr[0][0] -= 4;
                x_scroll += px_dx;
                y_scroll += py_dy;
            }
        }

        m_ViewX1    = m_ViewX2;
        m_ViewY1    = m_ViewY2;
        m_ViewXOfs1 = m_ViewXOfs2;
        m_ViewYOfs1 = m_ViewYOfs2;

        m_nInBytes = 2;
        DSP4_WAIT(2);

    resume2:
        m_Distance = ReadWord();
        if (m_Distance == -0x8000)
            break;

        m_nInBytes = 10;
        DSP4_WAIT(3);

    resume3:
        m_ViewY2 = ReadWord();
        m_ViewDY = (Int16)(ReadWord() * m_Distance >> 15);
        m_ViewX2 = ReadWord();
        m_ViewDX = (Int16)(ReadWord() * m_Distance >> 15);
    } while (1);

    m_bWaitCmd = TRUE;
}

//==========================================================================
//  Interface de barramento (ISNDSP)
//==========================================================================
void SNDSP4::WriteData(Uint32 /*uAddr*/, Uint8 uData)
{
#if DSP4_CAPTURE
    DLog("w %02X", (unsigned)uData);
#endif

    // Quirk: escrever enquanto ainda ha saida nao lida apenas avanca o
    // cursor de leitura (modela o handshake do uPD7725).
    if (m_iOutByte < m_nOutBytes) {
        m_iOutByte++;
        return;
    }

    if (m_bWaitCmd) {
        if (m_bHalfCmd) {
            // 2o byte: parte alta do comando
            m_uCommand = (Uint16)((m_uCommand & 0x00FF) | ((Uint16)uData << 8));
            m_iInByte   = 0;
            m_bWaitCmd  = FALSE;
            m_bHalfCmd  = FALSE;
            m_nOutBytes = 0;
            m_iOutByte  = 0;
            m_iResume   = 0;

            Int32 n = CommandInBytes(m_uCommand);
            if (n < 0) {
                // comando desconhecido: ignora (mantem FSM sincronizada)
                Uint8 op = (Uint8)(m_uCommand & 0xFF);
                if (!m_SeenCmd[op]) {
                    m_SeenCmd[op] = 1;
                    ConDebug("[dsp4] comando desconhecido 0x%04X (ignorado)\n",
                             (unsigned)m_uCommand);
                }
                m_bWaitCmd = TRUE;
            } else {
                m_nInBytes = n;
            }
        } else {
            // 1o byte: parte baixa do comando
            m_uCommand = uData;
            m_bHalfCmd = TRUE;
        }
    } else {
        if (m_iInByte < (Int32)sizeof(m_Param))
            m_Param[m_iInByte] = uData;
        m_iInByte++;
    }

    if (!m_bWaitCmd && m_nInBytes == m_iInByte) {
        m_bWaitCmd = TRUE;
        m_iOutByte = 0;
        m_iInByte  = 0;
        Execute();
    }
}

Uint8 SNDSP4::ReadData(Uint32 /*uAddr*/)
{
#if DSP4_CAPTURE
    DLog("r");
#endif

    if (m_nOutBytes) {
        Uint8 b = m_Out[m_iOutByte & 0x7FF];
        m_iOutByte++;
        if (m_nOutBytes == m_iOutByte)
            m_nOutBytes = 0;
        return b;
    }
    // alem do fim de um comando valido -> 0xFF (DR=0xFFFF)
    return 0xFF;
}

Uint8 SNDSP4::ReadStatus(Uint32 /*uAddr*/)
{
    // DSP-4 HLE: sempre pronto (RQM=1 -> bit7).
    return 0x80;
}
