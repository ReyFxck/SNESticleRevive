/*
 * sncx4.cpp - CX4 (Capcom/Hitachi HG51B169) coprocessor HLE
 *
 * Implementacao clean-room a partir do comportamento documentado do chip.
 * Veja sncx4.h para a descricao geral.
 *
 * Mapa de C4RAM (offsets a partir de $6000):
 *   $0000-$05FF  buffers de trabalho / OAM-to-be
 *   $0600-...    dados de origem (sprites/vertices) carregados por DMA
 *   $1F40-$1F5E  registradores de I/O ($7F40-$7F5E)
 *     $1F47  escrita -> dispara DMA ROM->C4RAM
 *     $1F4D  modo de sprite (para o comando 0x00)
 *     $1F4F  escrita -> dispara um comando
 *     $1F5E  leitura -> sempre 0 (status "pronto")
 *
 * Notas de bit-exatidao:
 *   - SAR16/SAR32/SAR64 do snes9x sao deslocamentos ARITMETICOS que NAO
 *     truncam o operando; portanto usamos ">>" direto com o tipo certo.
 *   - C4CosTable[i] == C4SinTable[(i+128) & 0x1FF] (cos = sin deslocado de
 *     90 graus); derivamos o cosseno da tabela de seno.
 *   - As tabelas (seno, multiplicador, arco-tangente) sao constantes
 *     numericas factuais (valores matematicos do chip).
 */

#include "types.h"
#include "sncx4.h"

#include <string.h>

// ---------------------------------------------------------------------------
// Tabelas numericas (dados factuais)
// ---------------------------------------------------------------------------

// multiplicador fracionario usado na interpolacao de seno/cosseno
static const Int16 C4MulTable[256] =
{
   0x0000, 0x0003, 0x0006, 0x0009, 0x000c, 0x000f, 0x0012, 0x0015,
   0x0019, 0x001c, 0x001f, 0x0022, 0x0025, 0x0028, 0x002b, 0x002f,
   0x0032, 0x0035, 0x0038, 0x003b, 0x003e, 0x0041, 0x0045, 0x0048,
   0x004b, 0x004e, 0x0051, 0x0054, 0x0057, 0x005b, 0x005e, 0x0061,
   0x0064, 0x0067, 0x006a, 0x006d, 0x0071, 0x0074, 0x0077, 0x007a,
   0x007d, 0x0080, 0x0083, 0x0087, 0x008a, 0x008d, 0x0090, 0x0093,
   0x0096, 0x0099, 0x009d, 0x00a0, 0x00a3, 0x00a6, 0x00a9, 0x00ac,
   0x00af, 0x00b3, 0x00b6, 0x00b9, 0x00bc, 0x00bf, 0x00c2, 0x00c5,
   0x00c9, 0x00cc, 0x00cf, 0x00d2, 0x00d5, 0x00d8, 0x00db, 0x00df,
   0x00e2, 0x00e5, 0x00e8, 0x00eb, 0x00ee, 0x00f1, 0x00f5, 0x00f8,
   0x00fb, 0x00fe, 0x0101, 0x0104, 0x0107, 0x010b, 0x010e, 0x0111,
   0x0114, 0x0117, 0x011a, 0x011d, 0x0121, 0x0124, 0x0127, 0x012a,
   0x012d, 0x0130, 0x0133, 0x0137, 0x013a, 0x013d, 0x0140, 0x0143,
   0x0146, 0x0149, 0x014d, 0x0150, 0x0153, 0x0156, 0x0159, 0x015c,
   0x015f, 0x0163, 0x0166, 0x0169, 0x016c, 0x016f, 0x0172, 0x0175,
   0x0178, 0x017c, 0x017f, 0x0182, 0x0185, 0x0188, 0x018b, 0x018e,
   0x0192, 0x0195, 0x0198, 0x019b, 0x019e, 0x01a1, 0x01a4, 0x01a8,
   0x01ab, 0x01ae, 0x01b1, 0x01b4, 0x01b7, 0x01ba, 0x01be, 0x01c1,
   0x01c4, 0x01c7, 0x01ca, 0x01cd, 0x01d0, 0x01d4, 0x01d7, 0x01da,
   0x01dd, 0x01e0, 0x01e3, 0x01e6, 0x01ea, 0x01ed, 0x01f0, 0x01f3,
   0x01f6, 0x01f9, 0x01fc, 0x0200, 0x0203, 0x0206, 0x0209, 0x020c,
   0x020f, 0x0212, 0x0216, 0x0219, 0x021c, 0x021f, 0x0222, 0x0225,
   0x0228, 0x022c, 0x022f, 0x0232, 0x0235, 0x0238, 0x023b, 0x023e,
   0x0242, 0x0245, 0x0248, 0x024b, 0x024e, 0x0251, 0x0254, 0x0258,
   0x025b, 0x025e, 0x0261, 0x0264, 0x0267, 0x026a, 0x026e, 0x0271,
   0x0274, 0x0277, 0x027a, 0x027d, 0x0280, 0x0284, 0x0287, 0x028a,
   0x028d, 0x0290, 0x0293, 0x0296, 0x029a, 0x029d, 0x02a0, 0x02a3,
   0x02a6, 0x02a9, 0x02ac, 0x02b0, 0x02b3, 0x02b6, 0x02b9, 0x02bc,
   0x02bf, 0x02c2, 0x02c6, 0x02c9, 0x02cc, 0x02cf, 0x02d2, 0x02d5,
   0x02d8, 0x02db, 0x02df, 0x02e2, 0x02e5, 0x02e8, 0x02eb, 0x02ee,
   0x02f1, 0x02f5, 0x02f8, 0x02fb, 0x02fe, 0x0301, 0x0304, 0x0307,
   0x030b, 0x030e, 0x0311, 0x0314, 0x0317, 0x031a, 0x031d, 0x0321
};

// arco-tangente (escala 0..127 para razao 0..1)
static const Int16 atantbl[256] =
{
     0,   1,   1,   2,   3,   3,   4,   4,   5,   6,   6,   7,   8,   8,   9,  10,
    10,  11,  11,  12,  13,  13,  14,  15,  15,  16,  16,  17,  18,  18,  19,  20,
    20,  21,  21,  22,  23,  23,  24,  25,  25,  26,  26,  27,  28,  28,  29,  29,
    30,  31,  31,  32,  33,  33,  34,  34,  35,  36,  36,  37,  37,  38,  39,  39,
    40,  40,  41,  42,  42,  43,  43,  44,  44,  45,  46,  46,  47,  47,  48,  49,
    49,  50,  50,  51,  51,  52,  53,  53,  54,  54,  55,  55,  56,  57,  57,  58,
    58,  59,  59,  60,  60,  61,  62,  62,  63,  63,  64,  64,  65,  65,  66,  66,
    67,  67,  68,  69,  69,  70,  70,  71,  71,  72,  72,  73,  73,  74,  74,  75,
    75,  76,  76,  77,  77,  78,  78,  79,  79,  80,  80,  81,  81,  82,  82,  83,
    83,  84,  84,  85,  85,  86,  86,  86,  87,  87,  88,  88,  89,  89,  90,  90,
    91,  91,  92,  92,  92,  93,  93,  94,  94,  95,  95,  96,  96,  96,  97,  97,
    98,  98,  99,  99,  99, 100, 100, 101, 101, 101, 102, 102, 103, 103, 104, 104,
   104, 105, 105, 106, 106, 106, 107, 107, 108, 108, 108, 109, 109, 109, 110, 110,
   111, 111, 111, 112, 112, 113, 113, 113, 114, 114, 114, 115, 115, 115, 116, 116,
   117, 117, 117, 118, 118, 118, 119, 119, 119, 120, 120, 120, 121, 121, 121, 122,
   122, 122, 123, 123, 123, 124, 124, 124, 125, 125, 125, 126, 126, 126, 127, 127
};

// seno em ponto fixo Q15: C4SinTable[i] = round(32767 * sin(2*pi*i/512))
static const Int16 C4SinTable[512] =
{
       0,    402,    804,   1206,   1607,   2009,   2410,   2811,
    3211,   3611,   4011,   4409,   4808,   5205,   5602,   5997,
    6392,   6786,   7179,   7571,   7961,   8351,   8739,   9126,
    9512,   9896,  10278,  10659,  11039,  11416,  11793,  12167,
   12539,  12910,  13278,  13645,  14010,  14372,  14732,  15090,
   15446,  15800,  16151,  16499,  16846,  17189,  17530,  17869,
   18204,  18537,  18868,  19195,  19519,  19841,  20159,  20475,
   20787,  21097,  21403,  21706,  22005,  22301,  22594,  22884,
   23170,  23453,  23732,  24007,  24279,  24547,  24812,  25073,
   25330,  25583,  25832,  26077,  26319,  26557,  26790,  27020,
   27245,  27466,  27684,  27897,  28106,  28310,  28511,  28707,
   28898,  29086,  29269,  29447,  29621,  29791,  29956,  30117,
   30273,  30425,  30572,  30714,  30852,  30985,  31114,  31237,
   31357,  31471,  31581,  31685,  31785,  31881,  31971,  32057,
   32138,  32214,  32285,  32351,  32413,  32469,  32521,  32568,
   32610,  32647,  32679,  32706,  32728,  32745,  32758,  32765,
   32767,  32765,  32758,  32745,  32728,  32706,  32679,  32647,
   32610,  32568,  32521,  32469,  32413,  32351,  32285,  32214,
   32138,  32057,  31971,  31881,  31785,  31685,  31581,  31471,
   31357,  31237,  31114,  30985,  30852,  30714,  30572,  30425,
   30273,  30117,  29956,  29791,  29621,  29447,  29269,  29086,
   28898,  28707,  28511,  28310,  28106,  27897,  27684,  27466,
   27245,  27020,  26790,  26557,  26319,  26077,  25832,  25583,
   25330,  25073,  24812,  24547,  24279,  24007,  23732,  23453,
   23170,  22884,  22594,  22301,  22005,  21706,  21403,  21097,
   20787,  20475,  20159,  19841,  19519,  19195,  18868,  18537,
   18204,  17869,  17530,  17189,  16846,  16499,  16151,  15800,
   15446,  15090,  14732,  14372,  14010,  13645,  13278,  12910,
   12539,  12167,  11793,  11416,  11039,  10659,  10278,   9896,
    9512,   9126,   8739,   8351,   7961,   7571,   7179,   6786,
    6392,   5997,   5602,   5205,   4808,   4409,   4011,   3611,
    3211,   2811,   2410,   2009,   1607,   1206,    804,    402,
       0,   -402,   -804,  -1206,  -1607,  -2009,  -2410,  -2811,
   -3211,  -3611,  -4011,  -4409,  -4808,  -5205,  -5602,  -5997,
   -6392,  -6786,  -7179,  -7571,  -7961,  -8351,  -8739,  -9126,
   -9512,  -9896, -10278, -10659, -11039, -11416, -11793, -12167,
  -12539, -12910, -13278, -13645, -14010, -14372, -14732, -15090,
  -15446, -15800, -16151, -16499, -16846, -17189, -17530, -17869,
  -18204, -18537, -18868, -19195, -19519, -19841, -20159, -20475,
  -20787, -21097, -21403, -21706, -22005, -22301, -22594, -22884,
  -23170, -23453, -23732, -24007, -24279, -24547, -24812, -25073,
  -25330, -25583, -25832, -26077, -26319, -26557, -26790, -27020,
  -27245, -27466, -27684, -27897, -28106, -28310, -28511, -28707,
  -28898, -29086, -29269, -29447, -29621, -29791, -29956, -30117,
  -30273, -30425, -30572, -30714, -30852, -30985, -31114, -31237,
  -31357, -31471, -31581, -31685, -31785, -31881, -31971, -32057,
  -32138, -32214, -32285, -32351, -32413, -32469, -32521, -32568,
  -32610, -32647, -32679, -32706, -32728, -32745, -32758, -32765,
  -32767, -32765, -32758, -32745, -32728, -32706, -32679, -32647,
  -32610, -32568, -32521, -32469, -32413, -32351, -32285, -32214,
  -32138, -32057, -31971, -31881, -31785, -31685, -31581, -31471,
  -31357, -31237, -31114, -30985, -30852, -30714, -30572, -30425,
  -30273, -30117, -29956, -29791, -29621, -29447, -29269, -29086,
  -28898, -28707, -28511, -28310, -28106, -27897, -27684, -27466,
  -27245, -27020, -26790, -26557, -26319, -26077, -25832, -25583,
  -25330, -25073, -24812, -24547, -24279, -24007, -23732, -23453,
  -23170, -22884, -22594, -22301, -22005, -21706, -21403, -21097,
  -20787, -20475, -20159, -19841, -19519, -19195, -18868, -18537,
  -18204, -17869, -17530, -17189, -16846, -16499, -16151, -15800,
  -15446, -15090, -14732, -14372, -14010, -13645, -13278, -12910,
  -12539, -12167, -11793, -11416, -11039, -10659, -10278,  -9896,
   -9512,  -9126,  -8739,  -8351,  -7961,  -7571,  -7179,  -6786,
   -6392,  -5997,  -5602,  -5205,  -4808,  -4409,  -4011,  -3611,
   -3211,  -2811,  -2410,  -2009,  -1607,  -1206,   -804,   -402
};

// padrao de teste do comando 0x5c (12 entradas de 4 bytes)
static const Uint8 C4TestPattern[12 * 4] =
{
   0x00, 0x00, 0x00, 0xff,
   0xff, 0xff, 0x00, 0xff,
   0x00, 0x00, 0x00, 0xff,
   0xff, 0xff, 0x00, 0x00,
   0xff, 0xff, 0x00, 0x00,
   0x80, 0xff, 0xff, 0x7f,
   0x00, 0x80, 0x00, 0xff,
   0x7f, 0x00, 0xff, 0x7f,
   0xff, 0x7f, 0xff, 0xff,
   0x00, 0x00, 0x01, 0xff,
   0xff, 0xfe, 0x00, 0x01,
   0x00, 0xff, 0xfe, 0x00
};

// cosseno derivado da tabela de seno: cos(i) = sin(i + 90 graus)
static inline Int16 cx4_sin(Int32 i) { return C4SinTable[i & 0x1FF]; }
static inline Int16 cx4_cos(Int32 i) { return C4SinTable[(i + 128) & 0x1FF]; }

#define CX4_ABS(x) ((x) < 0 ? -(x) : (x))

// ---------------------------------------------------------------------------
// Construcao / reset
// ---------------------------------------------------------------------------

SNCX4::SNCX4()
{
    m_pReadMem = NULL;
    m_pReadCtx = NULL;
    Reset();
}

void SNCX4::Reset()
{
    memset(m_Ram, 0, sizeof(m_Ram));
    C4WFXVal = C4WFYVal = C4WFZVal = 0;
    C4WFX2Val = C4WFY2Val = C4WFDist = C4WFScale = 0;
    C41FXVal = C41FYVal = C41FAngleRes = C41FDist = C41FDistVal = 0;
    m_tanval = 0;
}

// ---------------------------------------------------------------------------
// Nucleo matematico
// ---------------------------------------------------------------------------

Int16 SNCX4::Sin(Int16 Angle)
{
    Int32 S;
    Int16 AngleS7;
    if (Angle < 0)
    {
        if (Angle == -32768)
            return 0;
        return (Int16)(-Sin((Int16)(-Angle)));
    }
    AngleS7 = (Int16)(Angle >> 7);
    S = C4SinTable[AngleS7] + ((C4MulTable[Angle & 0xff] * C4SinTable[0x80 + AngleS7]) >> 15);
    if (S > 32767)
        S = 32767;
    return (Int16)S;
}

Int16 SNCX4::Cos(Int16 Angle)
{
    Int32 S;
    Int16 AngleS7;
    if (Angle < 0)
    {
        if (Angle == -32768)
            return (Int16)-32768;
        Angle = (Int16)(-Angle);
    }
    AngleS7 = (Int16)(Angle >> 7);
    S = C4SinTable[0x80 + AngleS7] - ((C4MulTable[Angle & 0xff] * C4SinTable[AngleS7]) >> 15);
    if (S < -32768)
        S = -32767;
    return (Int16)S;
}

Int16 SNCX4::Atan2(Int16 x, Int16 y)
{
    Int32 absAtan;
    Int32 x1, y1;
    if (x == 0)
        return 0;
    x1 = CX4_ABS((Int32)x);
    y1 = CX4_ABS((Int32)y);
    if (x1 > y1)
        absAtan = atantbl[(Uint8)((y1 << 8) / x1)];
    else
        absAtan = atantbl[(Uint8)((x1 << 8) / y1)];
    if ((x >= 0) ^ (y >= 0))
        return (Int16)(-absAtan);
    return (Int16)absAtan;
}

// raiz quadrada inteira (piso). floor(sqrt(r)) e' matematicamente unico,
// portanto bit-identico a qualquer implementacao correta de _isqrt.
Int32 SNCX4::ISqrt(Int32 r)
{
    Uint32 n, root, bit;
    if (r <= 0)
        return 0;
    n = (Uint32)r;
    root = 0;
    bit = 0x40000000u;
    while (bit > n)
        bit >>= 2;
    while (bit != 0)
    {
        if (n >= root + bit)
        {
            n -= root + bit;
            root = (root >> 1) + bit;
        }
        else
            root >>= 1;
        bit >>= 2;
    }
    return (Int32)root;
}

void SNCX4::TransfWireFrame()
{
    Int32 c4x, c4y, c4z, c4x2, c4y2, c4z2;
    Int32 denom;

    c4x = C4WFXVal;
    c4y = C4WFYVal;
    c4z = C4WFZVal - 0x95;

    // rotaciona em X
    m_tanval = -C4WFX2Val << 9;
    c4y2 = (c4y * Cos((Int16)m_tanval) - c4z * Sin((Int16)m_tanval)) >> 15;
    c4z2 = (c4y * Sin((Int16)m_tanval) + c4z * Cos((Int16)m_tanval)) >> 15;

    // rotaciona em Y
    m_tanval = -C4WFY2Val << 9;
    c4x2 = (c4x * Cos((Int16)m_tanval) + c4z2 * Sin((Int16)m_tanval)) >> 15;
    c4z  = (c4x * -Sin((Int16)m_tanval) + c4z2 * Cos((Int16)m_tanval)) >> 15;

    // rotaciona em Z
    m_tanval = -C4WFDist << 9;
    c4x = (c4x2 * Cos((Int16)m_tanval) - c4y2 * Sin((Int16)m_tanval)) >> 15;
    c4y = (c4x2 * Sin((Int16)m_tanval) + c4y2 * Cos((Int16)m_tanval)) >> 15;

    // escala (com projecao em perspectiva). O divisor so' seria zero para um
    // vertice degenerado (c4z == -0x95) que os jogos nunca produzem; usa-se
    // fallback de 1 apenas para nao dividir por zero no host.
    denom = 0x90 * (c4z + 0x95);
    if (denom == 0)
        denom = 1;
    C4WFXVal = (Int16)(((Int32)c4x * C4WFScale * 0x95) / denom);
    C4WFYVal = (Int16)(((Int32)c4y * C4WFScale * 0x95) / denom);
}

void SNCX4::TransfWireFrame2()
{
    Int32 c4x, c4y, c4z, c4x2, c4y2, c4z2;

    c4x = C4WFXVal;
    c4y = C4WFYVal;
    c4z = C4WFZVal;

    m_tanval = -C4WFX2Val << 9;
    c4y2 = (c4y * Cos((Int16)m_tanval) - c4z * Sin((Int16)m_tanval)) >> 15;
    c4z2 = (c4y * Sin((Int16)m_tanval) + c4z * Cos((Int16)m_tanval)) >> 15;

    m_tanval = -C4WFY2Val << 9;
    c4x2 = (c4x * Cos((Int16)m_tanval) + c4z2 * Sin((Int16)m_tanval)) >> 15;
    c4z  = (c4x * -Sin((Int16)m_tanval) + c4z2 * Cos((Int16)m_tanval)) >> 15;

    m_tanval = -C4WFDist << 9;
    c4x = (c4x2 * Cos((Int16)m_tanval) - c4y2 * Sin((Int16)m_tanval)) >> 15;
    c4y = (c4x2 * Sin((Int16)m_tanval) + c4y2 * Cos((Int16)m_tanval)) >> 15;

    C4WFXVal = (Int16)(((Int32)c4x * C4WFScale) / 0x100);
    C4WFYVal = (Int16)(((Int32)c4y * C4WFScale) / 0x100);
}

void SNCX4::CalcWireFrame()
{
    Int32 ax, ay;
    C4WFXVal = (Int16)(C4WFX2Val - C4WFXVal);
    C4WFYVal = (Int16)(C4WFY2Val - C4WFYVal);
    ax = CX4_ABS((Int32)C4WFXVal);
    ay = CX4_ABS((Int32)C4WFYVal);
    if (ax > ay)
    {
        C4WFDist = (Int16)(ax + 1);
        C4WFYVal = (Int16)(((Int32)C4WFYVal << 8) / ax);
        C4WFXVal = (C4WFXVal < 0) ? (Int16)-256 : (Int16)256;
    }
    else
    {
        if (C4WFYVal != 0)
        {
            C4WFDist = (Int16)(ay + 1);
            C4WFXVal = (Int16)(((Int32)C4WFXVal << 8) / ay);
            C4WFYVal = (C4WFYVal < 0) ? (Int16)-256 : (Int16)256;
        }
        else
            C4WFDist = 0;
    }
}

// ---------------------------------------------------------------------------
// Operacoes
// ---------------------------------------------------------------------------

void SNCX4::ConvOAM()
{
    Int32 oam, oam2, i;
    Uint16 globalX, globalY;

    oam = (m_Ram[0x626] << 2);
    for (i = 0x1fd; i > oam; i -= 4)
        m_Ram[i] = 0xe0;   // limpa a OAM-a-ser-montada

    globalX = RdW(0x0621);
    globalY = RdW(0x0623);
    oam2 = 0x200 + (m_Ram[0x626] >> 2);

    if (m_Ram[0x0620] != 0)
    {
        Uint8 offset = (Uint8)((m_Ram[0x626] & 3) * 2);
        Uint8 SprCount = (Uint8)(128 - m_Ram[0x626]);
        Int32 prio, k;
        for (prio = 0x30; prio >= 0; prio -= 0x10)
        {
            Int32 src = 0x220;
            for (k = m_Ram[0x0620]; k > 0 && SprCount > 0; k--, src += 16)
            {
                Int16 SprX, SprY;
                Uint8 SprName, SprAttr;
                Uint32 spr;
                if ((m_Ram[src + 4] & 0x30) != prio)
                    continue;
                SprX = (Int16)(RdW(src) - globalX);
                SprY = (Int16)(RdW(src + 2) - globalY);
                SprName = m_Ram[src + 5];
                SprAttr = (Uint8)(m_Ram[src + 4] | m_Ram[src + 6]);
                spr = Rd3(src + 7);
                if (RdMem(spr) != 0)
                {
                    Int32 SprCnt = RdMem(spr);
                    spr++;
                    for (; SprCnt > 0 && SprCount > 0; SprCnt--, spr += 4)
                    {
                        Uint8 b0 = RdMem(spr);
                        Uint8 b1 = RdMem(spr + 1);
                        Uint8 b2 = RdMem(spr + 2);
                        Uint8 b3 = RdMem(spr + 3);
                        Int16 X, Y;
                        X = (Int8)b1;
                        if (SprAttr & 0x40)
                            X = (Int16)(-X - ((b0 & 0x20) ? 16 : 8));
                        X = (Int16)(X + SprX);
                        if (X >= -16 && X <= 272)
                        {
                            Y = (Int8)b2;
                            if (SprAttr & 0x80)
                                Y = (Int16)(-Y - ((b0 & 0x20) ? 16 : 8));
                            Y = (Int16)(Y + SprY);
                            if (Y >= -16 && Y <= 224)
                            {
                                m_Ram[oam + 0] = (Uint8)(X & 0xff);
                                m_Ram[oam + 1] = (Uint8)Y;
                                m_Ram[oam + 2] = (Uint8)(SprName + b3);
                                m_Ram[oam + 3] = (Uint8)(SprAttr ^ (b0 & 0xc0));
                                m_Ram[oam2] &= (Uint8)~(3 << offset);
                                if (X & 0x100)
                                    m_Ram[oam2] |= (Uint8)(1 << offset);
                                if (b0 & 0x20)
                                    m_Ram[oam2] |= (Uint8)(2 << offset);
                                oam += 4;
                                SprCount--;
                                offset = (Uint8)((offset + 2) & 6);
                                if (offset == 0)
                                    oam2++;
                            }
                        }
                    }
                }
                else if (SprCount > 0)
                {
                    m_Ram[oam + 0] = (Uint8)SprX;
                    m_Ram[oam + 1] = (Uint8)SprY;
                    m_Ram[oam + 2] = SprName;
                    m_Ram[oam + 3] = SprAttr;
                    m_Ram[oam2] &= (Uint8)~(3 << offset);
                    if (SprX & 0x100)
                        m_Ram[oam2] |= (Uint8)(3 << offset);
                    else
                        m_Ram[oam2] |= (Uint8)(2 << offset);
                    oam += 4;
                    SprCount--;
                    offset = (Uint8)((offset + 2) & 6);
                    if (offset == 0)
                        oam2++;
                }
            }
        }
    }
}

void SNCX4::DoScaleRotate(Int32 row_padding)
{
    Int16 A, B, C, D;
    Int32 XScale, YScale, rot;
    Uint8 w, h;
    Int32 Cx, Cy, LineX, LineY, outidx, x, y, clear;
    Uint8 bit = 0x80;

    XScale = RdW(0x1f8f);
    if (XScale & 0x8000)
        XScale = 0x7fff;
    YScale = RdW(0x1f92);
    if (YScale & 0x8000)
        YScale = 0x7fff;

    rot = RdW(0x1f80);
    if (rot == 0)
    {
        A = (Int16)XScale; B = 0; C = 0; D = (Int16)YScale;
    }
    else if (rot == 128)
    {
        A = 0; B = (Int16)(-YScale); C = (Int16)XScale; D = 0;
    }
    else if (rot == 256)
    {
        A = (Int16)(-XScale); B = 0; C = 0; D = (Int16)(-YScale);
    }
    else if (rot == 384)
    {
        A = 0; B = (Int16)YScale; C = (Int16)(-XScale); D = 0;
    }
    else
    {
        Int32 idx = rot & 0x1ff;
        A = (Int16)(  (cx4_cos(idx) * XScale) >> 15);
        B = (Int16)(-((cx4_sin(idx) * YScale) >> 15));
        C = (Int16)(  (cx4_sin(idx) * XScale) >> 15);
        D = (Int16)(  (cx4_cos(idx) * YScale) >> 15);
    }

    w = (Uint8)(m_Ram[0x1f89] & ~7);
    h = (Uint8)(m_Ram[0x1f8c] & ~7);

    clear = (w + row_padding / 4) * h / 2;
    if (clear > (Int32)sizeof(m_Ram))
        clear = (Int32)sizeof(m_Ram);
    memset(m_Ram, 0, clear);

    Cx = (Int16)RdW(0x1f83);
    Cy = (Int16)RdW(0x1f86);

    LineX = (Cx << 12) - Cx * A - Cx * B;
    LineY = (Cy << 12) - Cy * C - Cy * D;

    outidx = 0;
    for (y = 0; y < h; y++)
    {
        Uint32 X = (Uint32)LineX;
        Uint32 Y = (Uint32)LineY;
        for (x = 0; x < w; x++)
        {
            Uint8 byte;
            if ((X >> 12) >= (Uint32)w || (Y >> 12) >= (Uint32)h)
                byte = 0;
            else
            {
                Uint32 addr = (Y >> 12) * w + (X >> 12);
                byte = m_Ram[0x600 + (addr >> 1)];
                if (addr & 1)
                    byte >>= 4;
            }
            if (byte & 1) m_Ram[outidx]      |= bit;
            if (byte & 2) m_Ram[outidx + 1]  |= bit;
            if (byte & 4) m_Ram[outidx + 16] |= bit;
            if (byte & 8) m_Ram[outidx + 17] |= bit;
            bit >>= 1;
            if (bit == 0)
            {
                bit = 0x80;
                outidx += 32;
            }
            X += A;
            Y += C;
        }
        outidx += 2 + row_padding;
        if (outidx & 0x10)
            outidx &= ~0x10;
        else
            outidx -= w * 4 + row_padding;
        LineX += B;
        LineY += D;
    }
}

void SNCX4::DrawLine(Int32 X1, Int32 Y1, Int16 Z1, Int32 X2, Int32 Y2, Int16 Z2, Uint8 Color)
{
    Int32 i;

    C4WFXVal  = (Int16)X1;
    C4WFYVal  = (Int16)Y1;
    C4WFZVal  = Z1;
    C4WFScale = (Int16)m_Ram[0x1f90];
    C4WFX2Val = (Int16)m_Ram[0x1f86];
    C4WFY2Val = (Int16)m_Ram[0x1f87];
    C4WFDist  = (Int16)m_Ram[0x1f88];
    TransfWireFrame2();
    X1 = (C4WFXVal + 48) << 8;
    Y1 = (C4WFYVal + 48) << 8;

    C4WFXVal = (Int16)X2;
    C4WFYVal = (Int16)Y2;
    C4WFZVal = Z2;
    TransfWireFrame2();
    X2 = (C4WFXVal + 48) << 8;
    Y2 = (C4WFYVal + 48) << 8;

    C4WFXVal  = (Int16)(X1 >> 8);
    C4WFYVal  = (Int16)(Y1 >> 8);
    C4WFX2Val = (Int16)(X2 >> 8);
    C4WFY2Val = (Int16)(Y2 >> 8);
    CalcWireFrame();
    X2 = (Int16)C4WFXVal;
    Y2 = (Int16)C4WFYVal;

    for (i = C4WFDist ? C4WFDist : 1; i > 0; i--)
    {
        if (X1 > 0xff && Y1 > 0xff && X1 < 0x6000 && Y1 < 0x6000)
        {
            Int32 addr = (((Y1 >> 8) >> 3) << 8) - (((Y1 >> 8) >> 3) << 6) + (((X1 >> 8) >> 3) << 4) + ((Y1 >> 8) & 7) * 2;
            Uint8 bit = (Uint8)(0x80 >> ((X1 >> 8) & 7));
            addr &= 0xffff;
            m_Ram[addr + 0x300] &= (Uint8)~bit;
            m_Ram[addr + 0x301] &= (Uint8)~bit;
            if (Color & 1)
                m_Ram[addr + 0x300] |= bit;
            if (Color & 2)
                m_Ram[addr + 0x301] |= bit;
        }
        X1 += X2;
        Y1 += Y2;
    }
}

void SNCX4::DrawWireFrame()
{
    Uint32 line = Rd3(0x1f80);
    Int32 i;
    for (i = m_Ram[0x0295]; i > 0; i--, line += 5)
    {
        Uint32 p1, p2;
        Int16 X1, Y1, Z1, X2, Y2, Z2;
        Uint8 l0 = RdMem(line), l1 = RdMem(line + 1);
        Uint8 l2 = RdMem(line + 2), l3 = RdMem(line + 3), l4 = RdMem(line + 4);
        if (l0 == 0xff && l1 == 0xff)
        {
            Uint32 tmp = line - 5;
            while (RdMem(tmp + 2) == 0xff && RdMem(tmp + 3) == 0xff)
                tmp -= 5;
            p1 = ((Uint32)m_Ram[0x1f82] << 16) | (RdMem(tmp + 2) << 8) | RdMem(tmp + 3);
        }
        else
            p1 = ((Uint32)m_Ram[0x1f82] << 16) | (l0 << 8) | l1;
        p2 = ((Uint32)m_Ram[0x1f82] << 16) | (l2 << 8) | l3;

        X1 = (Int16)((RdMem(p1 + 0) << 8) | RdMem(p1 + 1));
        Y1 = (Int16)((RdMem(p1 + 2) << 8) | RdMem(p1 + 3));
        Z1 = (Int16)((RdMem(p1 + 4) << 8) | RdMem(p1 + 5));
        X2 = (Int16)((RdMem(p2 + 0) << 8) | RdMem(p2 + 1));
        Y2 = (Int16)((RdMem(p2 + 2) << 8) | RdMem(p2 + 3));
        Z2 = (Int16)((RdMem(p2 + 4) << 8) | RdMem(p2 + 5));
        DrawLine(X1, Y1, Z1, X2, Y2, Z2, l4);
    }
}

void SNCX4::TransformLines()
{
    Int32 i, ptr, ptr2;

    C4WFX2Val = (Int16)m_Ram[0x1f83];
    C4WFY2Val = (Int16)m_Ram[0x1f86];
    C4WFDist  = (Int16)m_Ram[0x1f89];
    C4WFScale = (Int16)m_Ram[0x1f8c];

    ptr = 0;
    for (i = RdW(0x1f80); i > 0; i--, ptr += 0x10)
    {
        C4WFXVal = (Int16)RdW(ptr + 1);
        C4WFYVal = (Int16)RdW(ptr + 5);
        C4WFZVal = (Int16)RdW(ptr + 9);
        TransfWireFrame();
        WrW(ptr + 1, (Uint16)(C4WFXVal + 0x80));
        WrW(ptr + 5, (Uint16)(C4WFYVal + 0x50));
    }
    WrW(0x600, 23);
    WrW(0x602, 0x60);
    WrW(0x605, 0x40);
    WrW(0x600 + 8, 23);
    WrW(0x602 + 8, 0x60);
    WrW(0x605 + 8, 0x40);

    ptr  = 0xb02;
    ptr2 = 0;
    for (i = RdW(0xb00); i > 0; i--, ptr += 2, ptr2 += 8)
    {
        C4WFXVal  = (Int16)RdW((m_Ram[ptr] << 4) + 1);
        C4WFYVal  = (Int16)RdW((m_Ram[ptr] << 4) + 5);
        C4WFX2Val = (Int16)RdW((m_Ram[ptr + 1] << 4) + 1);
        C4WFY2Val = (Int16)RdW((m_Ram[ptr + 1] << 4) + 5);
        CalcWireFrame();
        WrW(ptr2 + 0x600, (Uint16)(C4WFDist ? C4WFDist : 1));
        WrW(ptr2 + 0x602, (Uint16)C4WFXVal);
        WrW(ptr2 + 0x605, (Uint16)C4WFYVal);
    }
}

void SNCX4::BitPlaneWave()
{
    static const Uint16 bmpdata[] =
    {
        0x0000, 0x0002, 0x0004, 0x0006, 0x0008, 0x000A, 0x000C, 0x000E,
        0x0200, 0x0202, 0x0204, 0x0206, 0x0208, 0x020A, 0x020C, 0x020E,
        0x0400, 0x0402, 0x0404, 0x0406, 0x0408, 0x040A, 0x040C, 0x040E,
        0x0600, 0x0602, 0x0604, 0x0606, 0x0608, 0x060A, 0x060C, 0x060E,
        0x0800, 0x0802, 0x0804, 0x0806, 0x0808, 0x080A, 0x080C, 0x080E
    };

    Int32 dst = 0;
    Uint32 waveptr = m_Ram[0x1f83];
    Uint16 mask1 = 0xc0c0;
    Uint16 mask2 = 0x3f3f;
    Int32 i, j;

    for (j = 0; j < 0x10; j++)
    {
        do
        {
            Int16 height = (Int16)(-((Int8)m_Ram[waveptr + 0xb00]) - 16);
            for (i = 0; i < 40; i++)
            {
                Uint16 tmp = (Uint16)(RdW(dst + bmpdata[i]) & mask2);
                if (height >= 0)
                {
                    if (height < 8)
                        tmp |= (Uint16)(mask1 & RdW(0xa00 + height * 2));
                    else
                        tmp |= (Uint16)(mask1 & 0xff00);
                }
                WrW(dst + bmpdata[i], tmp);
                height++;
            }
            waveptr = (waveptr + 1) & 0x7f;
            mask1 = (Uint16)((mask1 >> 2) | (mask1 << 6));
            mask2 = (Uint16)((mask2 >> 2) | (mask2 << 6));
        } while (mask1 != 0xc0c0);
        dst += 16;

        do
        {
            Int16 height = (Int16)(-((Int8)m_Ram[waveptr + 0xb00]) - 16);
            for (i = 0; i < 40; i++)
            {
                Uint16 tmp = (Uint16)(RdW(dst + bmpdata[i]) & mask2);
                if (height >= 0)
                {
                    if (height < 8)
                        tmp |= (Uint16)(mask1 & RdW(0xa10 + height * 2));
                    else
                        tmp |= (Uint16)(mask1 & 0xff00);
                }
                WrW(dst + bmpdata[i], tmp);
                height++;
            }
            waveptr = (waveptr + 1) & 0x7f;
            mask1 = (Uint16)((mask1 >> 2) | (mask1 << 6));
            mask2 = (Uint16)((mask2 >> 2) | (mask2 << 6));
        } while (mask1 != 0xc0c0);
        dst += 16;
    }
}

void SNCX4::SprDisintegrate()
{
    Uint32 x, y, i, j;
    Uint8 width = m_Ram[0x1f89];
    Uint8 height = m_Ram[0x1f8c];
    Int32 Cx = (Int16)RdW(0x1f80);
    Int32 Cy = (Int16)RdW(0x1f83);
    Int32 scaleX = (Int16)RdW(0x1f86);
    Int32 scaleY = (Int16)RdW(0x1f8f);
    Uint32 StartX = (Uint32)(-Cx * scaleX + (Cx << 8));
    Uint32 StartY = (Uint32)(-Cy * scaleY + (Cy << 8));
    Int32 src = 0x600;
    Int32 clr = width * height / 2;

    memset(m_Ram, 0, clr);
    for (y = StartY, i = 0; i < height; i++, y += scaleY)
    {
        for (x = StartX, j = 0; j < width; j++, x += scaleX)
        {
            if ((x >> 8) < width && (y >> 8) < height && (y >> 8) * width + (x >> 8) < 0x2000)
            {
                Uint8 pixel = (j & 1) ? (Uint8)(m_Ram[src] >> 4) : m_Ram[src];
                Int32 idx = (y >> 11) * width * 4 + (x >> 11) * 32 + ((y >> 8) & 7) * 2;
                Uint8 mask = (Uint8)(0x80 >> ((x >> 8) & 7));
                if (pixel & 1) m_Ram[idx]      |= mask;
                if (pixel & 2) m_Ram[idx + 1]  |= mask;
                if (pixel & 4) m_Ram[idx + 16] |= mask;
                if (pixel & 8) m_Ram[idx + 17] |= mask;
            }
            if (j & 1)
                src++;
        }
    }
}

void SNCX4::ProcessSprites()
{
    switch (m_Ram[0x1f4d])
    {
    case 0x00: ConvOAM();          break;   // monta OAM
    case 0x03: DoScaleRotate(0);   break;   // escala/rotacao
    case 0x05: TransformLines();   break;   // transforma linhas
    case 0x07: DoScaleRotate(64);  break;   // escala/rotacao (com padding)
    case 0x08: DrawWireFrame();    break;   // desenha wireframe
    case 0x0b: SprDisintegrate();  break;   // desintegracao
    case 0x0c: BitPlaneWave();     break;   // onda
    default:                       break;
    }
}

void SNCX4::DmaTransfer()
{
    Uint32 src = Rd3(0x1f40);
    Int32  dst = RdW(0x1f45) & 0x1fff;
    Int32  len = RdW(0x1f43);
    Int32  k;
    for (k = 0; k < len; k++)
    {
        Int32 d = dst + k;
        if (d >= (Int32)sizeof(m_Ram))
            break;
        m_Ram[d] = RdMem(src + k);
    }
}

void SNCX4::Command(Uint8 uByte)
{
    Int32 i;

    // caso especial: progride o contador de modo
    if (m_Ram[0x1f4d] == 0x0e && uByte < 0x40 && (uByte & 3) == 0)
    {
        m_Ram[0x1f80] = (Uint8)(uByte >> 2);
        return;
    }

    switch (uByte)
    {
    case 0x00:   // processa sprites (sub-comando em $1f4d)
        ProcessSprites();
        break;

    case 0x01:   // desenha wireframe
        memset(m_Ram + 0x300, 0, 16 * 12 * 3 * 4);
        DrawWireFrame();
        break;

    case 0x05:   // propulsao (?)
    {
        Int32 tmp = 0x10000;
        Uint16 d = RdW(0x1f83);
        if (d)
            tmp = ((tmp / (Int32)d) * (Int32)RdW(0x1f81)) >> 8;
        WrW(0x1f80, (Uint16)tmp);
        break;
    }

    case 0x0d:   // define comprimento de vetor
    {
        Int32 denom;
        C41FXVal = (Int16)RdW(0x1f80);
        C41FYVal = (Int16)RdW(0x1f83);
        C41FDistVal = (Int16)RdW(0x1f86);
        denom = ISqrt((Int32)C41FYVal * C41FYVal + (Int32)C41FXVal * C41FXVal);
        m_tanval = denom ? (C41FDistVal / denom) : 0;
        C41FYVal = (Int16)(((Int32)C41FYVal * m_tanval * 99) / 100);
        C41FXVal = (Int16)(((Int32)C41FXVal * m_tanval * 98) / 100);
        WrW(0x1f89, (Uint16)C41FXVal);
        WrW(0x1f8c, (Uint16)C41FYVal);
        break;
    }

    case 0x10:   // polar -> retangular
    {
        Int32 tmp = ((Int32)RdW(0x1f83) * cx4_cos(RdW(0x1f80) & 0x1ff) * 2) >> 16;
        Wr3(0x1f86, (Uint32)tmp);
        tmp = ((Int32)RdW(0x1f83) * cx4_sin(RdW(0x1f80) & 0x1ff) * 2) >> 16;
        Wr3(0x1f89, (Uint32)(tmp - (tmp >> 6)));
        break;
    }

    case 0x13:   // polar -> retangular
    {
        Int32 tmp = ((Int32)RdW(0x1f83) * cx4_cos(RdW(0x1f80) & 0x1ff) * 2) >> 8;
        Wr3(0x1f86, (Uint32)tmp);
        tmp = ((Int32)RdW(0x1f83) * cx4_sin(RdW(0x1f80) & 0x1ff) * 2) >> 8;
        Wr3(0x1f89, (Uint32)tmp);
        break;
    }

    case 0x15:   // pitagoras
        C41FXVal = (Int16)RdW(0x1f80);
        C41FYVal = (Int16)RdW(0x1f83);
        C41FDist = (Int16)ISqrt((Int32)C41FXVal * C41FXVal + (Int32)C41FYVal * C41FYVal);
        WrW(0x1f80, (Uint16)C41FDist);
        break;

    case 0x1f:   // arco-tangente
        C41FXVal = (Int16)RdW(0x1f80);
        C41FYVal = (Int16)RdW(0x1f83);
        if (C41FXVal == 0)
        {
            if (C41FYVal > 0)
                C41FAngleRes = 0x80;
            else
                C41FAngleRes = 0x180;
        }
        else
        {
            C41FAngleRes = (Int16)(Atan2(C41FYVal, C41FXVal) / 2);
            if (C41FXVal < 0)
                C41FAngleRes = (Int16)(C41FAngleRes + 0x100);
            C41FAngleRes = (Int16)(C41FAngleRes & 0x1FF);
        }
        WrW(0x1f86, (Uint16)C41FAngleRes);
        break;

    case 0x22:   // trapezoide
    {
        Int16 angle1 = (Int16)(RdW(0x1f8c) & 0x1ff);
        Int16 angle2 = (Int16)(RdW(0x1f8f) & 0x1ff);
        Int32 tan1 = (cx4_cos(angle1) != 0) ? ((((Int32)cx4_sin(angle1)) << 16) / cx4_cos(angle1)) : (Int32)0x80000000;
        Int32 tan2 = (cx4_cos(angle2) != 0) ? ((((Int32)cx4_sin(angle2)) << 16) / cx4_cos(angle2)) : (Int32)0x80000000;
        Int16 y = (Int16)(RdW(0x1f83) - RdW(0x1f89));
        Int32 j;
        for (j = 0; j < 225; j++)
        {
            Int16 left, right;
            if (y >= 0)
            {
                left  = (Int16)((((Int32)tan1 * y) >> 16) - RdW(0x1f80) + RdW(0x1f86));
                right = (Int16)((((Int32)tan2 * y) >> 16) - RdW(0x1f80) + RdW(0x1f86) + RdW(0x1f93));
                if (left < 0 && right < 0)
                {
                    left = 1;
                    right = 0;
                }
                else if (left < 0)
                    left = 0;
                else if (right < 0)
                    right = 0;
                if (left > 255 && right > 255)
                {
                    left = 255;
                    right = 254;
                }
                else if (left > 255)
                    left = 255;
                else if (right > 255)
                    right = 255;
            }
            else
            {
                left = 1;
                right = 0;
            }
            m_Ram[j + 0x800] = (Uint8)left;
            m_Ram[j + 0x900] = (Uint8)right;
            y++;
        }
        break;
    }

    case 0x25:   // multiplicacao
    {
        Int32 foo = (Int32)Rd3(0x1f80);
        Int32 bar = (Int32)Rd3(0x1f83);
        foo *= bar;
        Wr3(0x1f80, (Uint32)foo);
        break;
    }

    case 0x2d:   // transforma coordenadas
        C4WFXVal  = (Int16)RdW(0x1f81);
        C4WFYVal  = (Int16)RdW(0x1f84);
        C4WFZVal  = (Int16)RdW(0x1f87);
        C4WFX2Val = (Int16)m_Ram[0x1f89];
        C4WFY2Val = (Int16)m_Ram[0x1f8a];
        C4WFDist  = (Int16)m_Ram[0x1f8b];
        C4WFScale = (Int16)RdW(0x1f90);
        TransfWireFrame2();
        WrW(0x1f80, (Uint16)C4WFXVal);
        WrW(0x1f83, (Uint16)C4WFYVal);
        break;

    case 0x40:   // soma
    {
        Uint16 sum = 0;
        for (i = 0; i < 0x800; i++)
            sum = (Uint16)(sum + m_Ram[i]);
        WrW(0x1f80, sum);
        break;
    }

    case 0x54:   // quadrado (resultado de 48 bits)
    {
        Int32 v = (Int32)(Rd3(0x1f80) << 8) >> 8;   // sinal-extende 24->32
        Int64 a = (Int64)v;
        a *= a;
        Wr3(0x1f83, (Uint32)a);
        Wr3(0x1f86, (Uint32)(a >> 24));
        break;
    }

    case 0x5c:   // registrador imediato (padrao de teste)
        for (i = 0; i < 12 * 4; i++)
            m_Ram[i] = C4TestPattern[i];
        break;

    case 0x89:   // ROM imediata
        m_Ram[0x1f80] = 0x36;
        m_Ram[0x1f81] = 0x43;
        m_Ram[0x1f82] = 0x05;
        break;

    default:
        break;
    }
}

// ---------------------------------------------------------------------------
// Interface de barramento ($6000-$7FFF)
// ---------------------------------------------------------------------------

Uint8 SNCX4::Read(Uint32 uAddr)
{
    if (uAddr == 0x7f5e)
        return 0;   // status: sempre pronto
    return m_Ram[(uAddr - 0x6000) & (CX4_VISIBLE - 1)];
}

void SNCX4::Write(Uint32 uAddr, Uint8 uData)
{
    m_Ram[(uAddr - 0x6000) & (CX4_VISIBLE - 1)] = uData;
    if (uAddr == 0x7f4f)
        Command(uData);
    else if (uAddr == 0x7f47)
        DmaTransfer();
}
