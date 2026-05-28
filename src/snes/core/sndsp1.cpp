/*
 * sndsp1.cpp - DSP-1 / DSP-1B coprocessor emulation
 *
 * Esta implementacao reproduz o comportamento do DSP-1 a partir da
 * engenharia reversa publica do chip, publicada em Junho/2006 por:
 *
 *     Overload, The Dumper, Neviksti e Andreas Naive
 *
 * (Posteriormente revisada por byuu com a correcao em DataRom[0x3c].)
 *
 * As tabelas SinTable[256], MulTable[256] e DataRom[1024] sao dados
 * factuais do silicio do DSP-1B, obtidos via opcode 0x1F (MemoryDump)
 * em hardware real e amplamente publicados em dominio publico.
 *
 * O codigo aqui foi escrito do zero seguindo a documentacao e usando
 * as mesmas formulas matematicas — nao foram copiados blocos de codigo
 * de bsnes, snes9x ou similares.
 *
 * Otimizado para PS2:  apenas inteiros 16/32 bits, multiplicacao
 * com shift, sem alocacao dinamica, sem ponto-flutuante.
 */

#include "types.h"
#include "snes.h"
#include "sndsp1.h"

#include <string.h>

//==========================================================================
//  TABELAS  (dados factuais do silicio, publicados em 2006)
//==========================================================================

// SinTable: 256 pontos de uma senoide 16-bit signed, dois ciclos
// (0..255 = um periodo, 256..511 espelhado negativo).  Indexada por
// (Angle>>8) e interpolada com MulTable.
static const Int16 g_SinTable[256] = {
    0x0000, 0x0324, 0x0647, 0x096a, 0x0c8b, 0x0fab, 0x12c8, 0x15e2,
    0x18f8, 0x1c0b, 0x1f19, 0x2223, 0x2528, 0x2826, 0x2b1f, 0x2e11,
    0x30fb, 0x33de, 0x36ba, 0x398c, 0x3c56, 0x3f17, 0x41ce, 0x447a,
    0x471c, 0x49b4, 0x4c3f, 0x4ebf, 0x5133, 0x539b, 0x55f5, 0x5842,
    0x5a82, 0x5cb4, 0x5ed7, 0x60ec, 0x62f2, 0x64e8, 0x66cf, 0x68a6,
    0x6a6d, 0x6c24, 0x6dca, 0x6f5f, 0x70e2, 0x7255, 0x73b5, 0x7504,
    0x7641, 0x776c, 0x7884, 0x798a, 0x7a7d, 0x7b5d, 0x7c29, 0x7ce3,
    0x7d8a, 0x7e1d, 0x7e9d, 0x7f09, 0x7f62, 0x7fa7, 0x7fd8, 0x7ff6,
    0x7fff, 0x7ff6, 0x7fd8, 0x7fa7, 0x7f62, 0x7f09, 0x7e9d, 0x7e1d,
    0x7d8a, 0x7ce3, 0x7c29, 0x7b5d, 0x7a7d, 0x798a, 0x7884, 0x776c,
    0x7641, 0x7504, 0x73b5, 0x7255, 0x70e2, 0x6f5f, 0x6dca, 0x6c24,
    0x6a6d, 0x68a6, 0x66cf, 0x64e8, 0x62f2, 0x60ec, 0x5ed7, 0x5cb4,
    0x5a82, 0x5842, 0x55f5, 0x539b, 0x5133, 0x4ebf, 0x4c3f, 0x49b4,
    0x471c, 0x447a, 0x41ce, 0x3f17, 0x3c56, 0x398c, 0x36ba, 0x33de,
    0x30fb, 0x2e11, 0x2b1f, 0x2826, 0x2528, 0x2223, 0x1f19, 0x1c0b,
    0x18f8, 0x15e2, 0x12c8, 0x0fab, 0x0c8b, 0x096a, 0x0647, 0x0324,
   -0x0000,-0x0324,-0x0647,-0x096a,-0x0c8b,-0x0fab,-0x12c8,-0x15e2,
   -0x18f8,-0x1c0b,-0x1f19,-0x2223,-0x2528,-0x2826,-0x2b1f,-0x2e11,
   -0x30fb,-0x33de,-0x36ba,-0x398c,-0x3c56,-0x3f17,-0x41ce,-0x447a,
   -0x471c,-0x49b4,-0x4c3f,-0x4ebf,-0x5133,-0x539b,-0x55f5,-0x5842,
   -0x5a82,-0x5cb4,-0x5ed7,-0x60ec,-0x62f2,-0x64e8,-0x66cf,-0x68a6,
   -0x6a6d,-0x6c24,-0x6dca,-0x6f5f,-0x70e2,-0x7255,-0x73b5,-0x7504,
   -0x7641,-0x776c,-0x7884,-0x798a,-0x7a7d,-0x7b5d,-0x7c29,-0x7ce3,
   -0x7d8a,-0x7e1d,-0x7e9d,-0x7f09,-0x7f62,-0x7fa7,-0x7fd8,-0x7ff6,
   -0x7fff,-0x7ff6,-0x7fd8,-0x7fa7,-0x7f62,-0x7f09,-0x7e9d,-0x7e1d,
   -0x7d8a,-0x7ce3,-0x7c29,-0x7b5d,-0x7a7d,-0x798a,-0x7884,-0x776c,
   -0x7641,-0x7504,-0x73b5,-0x7255,-0x70e2,-0x6f5f,-0x6dca,-0x6c24,
   -0x6a6d,-0x68a6,-0x66cf,-0x64e8,-0x62f2,-0x60ec,-0x5ed7,-0x5cb4,
   -0x5a82,-0x5842,-0x55f5,-0x539b,-0x5133,-0x4ebf,-0x4c3f,-0x49b4,
   -0x471c,-0x447a,-0x41ce,-0x3f17,-0x3c56,-0x398c,-0x36ba,-0x33de,
   -0x30fb,-0x2e11,-0x2b1f,-0x2826,-0x2528,-0x2223,-0x1f19,-0x1c0b,
   -0x18f8,-0x15e2,-0x12c8,-0x0fab,-0x0c8b,-0x096a,-0x0647,-0x0324
};

// MulTable: tabela auxiliar usada na interpolacao linear de Sin/Cos
// (representa o passo do delta para os 256 sub-passos entre dois nodes).
static const Int16 g_MulTable[256] = {
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

// MaxAZS_Exp: limiar de "clipping" do angulo Azs em funcao do expoente
// de CentreZ apos normalizacao (16 entradas).
static const Int16 g_MaxAZS_Exp[16] = {
    0x38b4, 0x38b7, 0x38ba, 0x38be, 0x38c0, 0x38c4, 0x38c7, 0x38ca,
    0x38ce, 0x38d0, 0x38d4, 0x38d7, 0x38da, 0x38dd, 0x38e0, 0x38e4
};

// DataRom: 1024 palavras de 16 bits que sao o dump literal da Mask
// ROM interna do DSP-1B (comando 0x1F).  Contem:
//   0x022..0x031 e 0x031..0x040 - duas tabelas de shift
//   0x065..0x0E4 - tabela inicial para Newton-Raphson de 1/x
//   0x0E5..0x115 - tabela inicial para sqrt
//   0x116..0x197 - sin (com mais resolucao que SinTable)
//   0x196..0x215 - cos
//   0x21C..0x31C - arccos (nao usada em runtime)
// Correcao em DataRom[0x3c] (de 0x0001 para 0x0010) descoberta por byuu.
static const Uint16 g_DataRom[1024] = {
    0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
    0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
    0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
    0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
    0x0000, 0x0000, 0x0001, 0x0002, 0x0004, 0x0008, 0x0010, 0x0020,
    0x0040, 0x0080, 0x0100, 0x0200, 0x0400, 0x0800, 0x1000, 0x2000,
    0x4000, 0x7fff, 0x4000, 0x2000, 0x1000, 0x0800, 0x0400, 0x0200,
    0x0100, 0x0080, 0x0040, 0x0020, 0x0010, 0x0008, 0x0004, 0x0002,
    0x0001, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
    0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
    0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
    0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
    0x0000, 0x0000, 0x8000, 0xffe5, 0x0100, 0x7fff, 0x7f02, 0x7e08,
    0x7d12, 0x7c1f, 0x7b30, 0x7a45, 0x795d, 0x7878, 0x7797, 0x76ba,
    0x75df, 0x7507, 0x7433, 0x7361, 0x7293, 0x71c7, 0x70fe, 0x7038,
    0x6f75, 0x6eb4, 0x6df6, 0x6d3a, 0x6c81, 0x6bca, 0x6b16, 0x6a64,
    0x69b4, 0x6907, 0x685b, 0x67b2, 0x670b, 0x6666, 0x65c4, 0x6523,
    0x6484, 0x63e7, 0x634c, 0x62b3, 0x621c, 0x6186, 0x60f2, 0x6060,
    0x5fd0, 0x5f41, 0x5eb5, 0x5e29, 0x5d9f, 0x5d17, 0x5c91, 0x5c0c,
    0x5b88, 0x5b06, 0x5a85, 0x5a06, 0x5988, 0x590b, 0x5890, 0x5816,
    0x579d, 0x5726, 0x56b0, 0x563b, 0x55c8, 0x5555, 0x54e4, 0x5474,
    0x5405, 0x5398, 0x532b, 0x52bf, 0x5255, 0x51ec, 0x5183, 0x511c,
    0x50b6, 0x5050, 0x4fec, 0x4f89, 0x4f26, 0x4ec5, 0x4e64, 0x4e05,
    0x4da6, 0x4d48, 0x4cec, 0x4c90, 0x4c34, 0x4bda, 0x4b81, 0x4b28,
    0x4ad0, 0x4a79, 0x4a23, 0x49cd, 0x4979, 0x4925, 0x48d1, 0x487f,
    0x482d, 0x47dc, 0x478c, 0x473c, 0x46ed, 0x469f, 0x4651, 0x4604,
    0x45b8, 0x456c, 0x4521, 0x44d7, 0x448d, 0x4444, 0x43fc, 0x43b4,
    0x436d, 0x4326, 0x42e0, 0x429a, 0x4255, 0x4211, 0x41cd, 0x4189,
    0x4146, 0x4104, 0x40c2, 0x4081, 0x4040, 0x3fff, 0x41f7, 0x43e1,
    0x45bd, 0x478d, 0x4951, 0x4b0b, 0x4cbb, 0x4e61, 0x4fff, 0x5194,
    0x5322, 0x54a9, 0x5628, 0x57a2, 0x5914, 0x5a81, 0x5be9, 0x5d4a,
    0x5ea7, 0x5fff, 0x6152, 0x62a0, 0x63ea, 0x6530, 0x6672, 0x67b0,
    0x68ea, 0x6a20, 0x6b53, 0x6c83, 0x6daf, 0x6ed9, 0x6fff, 0x7122,
    0x7242, 0x735f, 0x747a, 0x7592, 0x76a7, 0x77ba, 0x78cb, 0x79d9,
    0x7ae5, 0x7bee, 0x7cf5, 0x7dfa, 0x7efe, 0x7fff, 0x0000, 0x0324,
    0x0647, 0x096a, 0x0c8b, 0x0fab, 0x12c8, 0x15e2, 0x18f8, 0x1c0b,
    0x1f19, 0x2223, 0x2528, 0x2826, 0x2b1f, 0x2e11, 0x30fb, 0x33de,
    0x36ba, 0x398c, 0x3c56, 0x3f17, 0x41ce, 0x447a, 0x471c, 0x49b4,
    0x4c3f, 0x4ebf, 0x5133, 0x539b, 0x55f5, 0x5842, 0x5a82, 0x5cb4,
    0x5ed7, 0x60ec, 0x62f2, 0x64e8, 0x66cf, 0x68a6, 0x6a6d, 0x6c24,
    0x6dca, 0x6f5f, 0x70e2, 0x7255, 0x73b5, 0x7504, 0x7641, 0x776c,
    0x7884, 0x798a, 0x7a7d, 0x7b5d, 0x7c29, 0x7ce3, 0x7d8a, 0x7e1d,
    0x7e9d, 0x7f09, 0x7f62, 0x7fa7, 0x7fd8, 0x7ff6, 0x7fff, 0x7ff6,
    0x7fd8, 0x7fa7, 0x7f62, 0x7f09, 0x7e9d, 0x7e1d, 0x7d8a, 0x7ce3,
    0x7c29, 0x7b5d, 0x7a7d, 0x798a, 0x7884, 0x776c, 0x7641, 0x7504,
    0x73b5, 0x7255, 0x70e2, 0x6f5f, 0x6dca, 0x6c24, 0x6a6d, 0x68a6,
    0x66cf, 0x64e8, 0x62f2, 0x60ec, 0x5ed7, 0x5cb4, 0x5a82, 0x5842,
    0x55f5, 0x539b, 0x5133, 0x4ebf, 0x4c3f, 0x49b4, 0x471c, 0x447a,
    0x41ce, 0x3f17, 0x3c56, 0x398c, 0x36ba, 0x33de, 0x30fb, 0x2e11,
    0x2b1f, 0x2826, 0x2528, 0x2223, 0x1f19, 0x1c0b, 0x18f8, 0x15e2,
    0x12c8, 0x0fab, 0x0c8b, 0x096a, 0x0647, 0x0324, 0x7fff, 0x7ff6,
    0x7fd8, 0x7fa7, 0x7f62, 0x7f09, 0x7e9d, 0x7e1d, 0x7d8a, 0x7ce3,
    0x7c29, 0x7b5d, 0x7a7d, 0x798a, 0x7884, 0x776c, 0x7641, 0x7504,
    0x73b5, 0x7255, 0x70e2, 0x6f5f, 0x6dca, 0x6c24, 0x6a6d, 0x68a6,
    0x66cf, 0x64e8, 0x62f2, 0x60ec, 0x5ed7, 0x5cb4, 0x5a82, 0x5842,
    0x55f5, 0x539b, 0x5133, 0x4ebf, 0x4c3f, 0x49b4, 0x471c, 0x447a,
    0x41ce, 0x3f17, 0x3c56, 0x398c, 0x36ba, 0x33de, 0x30fb, 0x2e11,
    0x2b1f, 0x2826, 0x2528, 0x2223, 0x1f19, 0x1c0b, 0x18f8, 0x15e2,
    0x12c8, 0x0fab, 0x0c8b, 0x096a, 0x0647, 0x0324, 0x0000, 0xfcdc,
    0xf9b9, 0xf696, 0xf375, 0xf055, 0xed38, 0xea1e, 0xe708, 0xe3f5,
    0xe0e7, 0xdddd, 0xdad8, 0xd7da, 0xd4e1, 0xd1ef, 0xcf05, 0xcc22,
    0xc946, 0xc674, 0xc3aa, 0xc0e9, 0xbe32, 0xbb86, 0xb8e4, 0xb64c,
    0xb3c1, 0xb141, 0xaecd, 0xac65, 0xaa0b, 0xa7be, 0xa57e, 0xa34c,
    0xa129, 0x9f14, 0x9d0e, 0x9b18, 0x9931, 0x975a, 0x9593, 0x93dc,
    0x9236, 0x90a1, 0x8f1e, 0x8dab, 0x8c4b, 0x8afc, 0x89bf, 0x8894,
    0x877c, 0x8676, 0x8583, 0x84a3, 0x83d7, 0x831d, 0x8276, 0x81e3,
    0x8163, 0x80f7, 0x809e, 0x8059, 0x8028, 0x800a, 0x6488, 0x0080,
    0x03ff, 0x0116, 0x0002, 0x0080, 0x4000, 0x3fd7, 0x3faf, 0x3f86,
    0x3f5d, 0x3f34, 0x3f0c, 0x3ee3, 0x3eba, 0x3e91, 0x3e68, 0x3e40,
    0x3e17, 0x3dee, 0x3dc5, 0x3d9c, 0x3d74, 0x3d4b, 0x3d22, 0x3cf9,
    0x3cd0, 0x3ca7, 0x3c7f, 0x3c56, 0x3c2d, 0x3c04, 0x3bdb, 0x3bb2,
    0x3b89, 0x3b60, 0x3b37, 0x3b0e, 0x3ae5, 0x3abc, 0x3a93, 0x3a69,
    0x3a40, 0x3a17, 0x39ee, 0x39c5, 0x399c, 0x3972, 0x3949, 0x3920,
    0x38f6, 0x38cd, 0x38a4, 0x387a, 0x3851, 0x3827, 0x37fe, 0x37d4,
    0x37aa, 0x3781, 0x3757, 0x372d, 0x3704, 0x36da, 0x36b0, 0x3686,
    0x365c, 0x3632, 0x3609, 0x35df, 0x35b4, 0x358a, 0x3560, 0x3536,
    0x350c, 0x34e1, 0x34b7, 0x348d, 0x3462, 0x3438, 0x340d, 0x33e3,
    0x33b8, 0x338d, 0x3363, 0x3338, 0x330d, 0x32e2, 0x32b7, 0x328c,
    0x3261, 0x3236, 0x320b, 0x31df, 0x31b4, 0x3188, 0x315d, 0x3131,
    0x3106, 0x30da, 0x30ae, 0x3083, 0x3057, 0x302b, 0x2fff, 0x2fd2,
    0x2fa6, 0x2f7a, 0x2f4d, 0x2f21, 0x2ef4, 0x2ec8, 0x2e9b, 0x2e6e,
    0x2e41, 0x2e14, 0x2de7, 0x2dba, 0x2d8d, 0x2d60, 0x2d32, 0x2d05,
    0x2cd7, 0x2ca9, 0x2c7b, 0x2c4d, 0x2c1f, 0x2bf1, 0x2bc3, 0x2b94,
    0x2b66, 0x2b37, 0x2b09, 0x2ada, 0x2aab, 0x2a7c, 0x2a4c, 0x2a1d,
    0x29ed, 0x29be, 0x298e, 0x295e, 0x292e, 0x28fe, 0x28ce, 0x289d,
    0x286d, 0x283c, 0x280b, 0x27da, 0x27a9, 0x2777, 0x2746, 0x2714,
    0x26e2, 0x26b0, 0x267e, 0x264c, 0x2619, 0x25e7, 0x25b4, 0x2581,
    0x254d, 0x251a, 0x24e6, 0x24b2, 0x247e, 0x244a, 0x2415, 0x23e1,
    0x23ac, 0x2376, 0x2341, 0x230b, 0x22d6, 0x229f, 0x2269, 0x2232,
    0x21fc, 0x21c4, 0x218d, 0x2155, 0x211d, 0x20e5, 0x20ad, 0x2074,
    0x203b, 0x2001, 0x1fc7, 0x1f8d, 0x1f53, 0x1f18, 0x1edd, 0x1ea1,
    0x1e66, 0x1e29, 0x1ded, 0x1db0, 0x1d72, 0x1d35, 0x1cf6, 0x1cb8,
    0x1c79, 0x1c39, 0x1bf9, 0x1bb8, 0x1b77, 0x1b36, 0x1af4, 0x1ab1,
    0x1a6e, 0x1a2a, 0x19e6, 0x19a1, 0x195c, 0x1915, 0x18ce, 0x1887,
    0x183f, 0x17f5, 0x17ac, 0x1761, 0x1715, 0x16c9, 0x167c, 0x162e,
    0x15df, 0x158e, 0x153d, 0x14eb, 0x1497, 0x1442, 0x13ec, 0x1395,
    0x133c, 0x12e2, 0x1286, 0x1228, 0x11c9, 0x1167, 0x1104, 0x109e,
    0x1036, 0x0fcc, 0x0f5f, 0x0eef, 0x0e7b, 0x0e04, 0x0d89, 0x0d0a,
    0x0c86, 0x0bfd, 0x0b6d, 0x0ad6, 0x0a36, 0x098d, 0x08d7, 0x0811,
    0x0736, 0x063e, 0x0519, 0x039a, 0x0000, 0x7fff, 0x0100, 0x0080,
    0x021d, 0x00c8, 0x00ce, 0x0048, 0x0a26, 0x277a, 0x00ce, 0x6488,
    0x14ac, 0x0001, 0x00f9, 0x00fc, 0x00ff, 0x00fc, 0x00f9, 0xffff,
    0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff,
    0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff,
    0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff,
    0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff,
    0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff,
    0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff,
    0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff,
    0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff,
    0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff,
    0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff,
    0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff,
    0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff,
    0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff,
    0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff,
    0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff,
    0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff,
    0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff,
    0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff,
    0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff,
    0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff,
    0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff,
    0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff,
    0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff,
    0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff,
    0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff,
    0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff
};

//==========================================================================
//  TABELA DE TAMANHOS DOS COMANDOS
//
//  Cada entrada e' (nInputs, nOutputs) em palavras de 16-bit.  Opcodes
//  invalidos / aliases sao tratados como aliasados (mascarando os bits
//  altos do opcode segundo o mapa real do chip).
//==========================================================================
struct DSP1_CmdInfo {
    Uint8 nIn;
    Uint16 nOut;
};

static const DSP1_CmdInfo g_CmdTable[0x40] = {
    /*0x00*/ {2, 1},  /*0x01*/ {4, 0},  /*0x02*/ {7, 4},  /*0x03*/ {3, 3},
    /*0x04*/ {2, 2},  /*0x05*/ {4, 0},  /*0x06*/ {3, 3},  /*0x07*/ {1, 1},
    /*0x08*/ {3, 2},  /*0x09*/ {3, 3},  /*0x0A*/ {1, 4},  /*0x0B*/ {3, 1},
    /*0x0C*/ {3, 2},  /*0x0D*/ {3, 3},  /*0x0E*/ {2, 2},  /*0x0F*/ {1, 1},
    /*0x10*/ {2, 2},  /*0x11*/ {4, 0},  /*0x12*/ {7, 4},  /*0x13*/ {3, 3},
    /*0x14*/ {6, 3},  /*0x15*/ {4, 0},  /*0x16*/ {3, 3},  /*0x17*/ {1, 1024},
    /*0x18*/ {4, 1},  /*0x19*/ {3, 3},  /*0x1A*/ {0, 0},  /*0x1B*/ {3, 1},
    /*0x1C*/ {6, 3},  /*0x1D*/ {3, 3},  /*0x1E*/ {2, 2},  /*0x1F*/ {1, 1024},
    /*0x20*/ {2, 1},  /*0x21*/ {4, 0},  /*0x22*/ {7, 4},  /*0x23*/ {3, 3},
    /*0x24*/ {2, 2},  /*0x25*/ {4, 0},  /*0x26*/ {3, 3},  /*0x27*/ {1, 1},
    /*0x28*/ {3, 1},  /*0x29*/ {3, 3},  /*0x2A*/ {0, 0},  /*0x2B*/ {3, 1},
    /*0x2C*/ {3, 2},  /*0x2D*/ {3, 3},  /*0x2E*/ {2, 2},  /*0x2F*/ {1, 1},
    /*0x30*/ {2, 2},  /*0x31*/ {4, 0},  /*0x32*/ {7, 4},  /*0x33*/ {3, 3},
    /*0x34*/ {6, 3},  /*0x35*/ {4, 0},  /*0x36*/ {3, 3},  /*0x37*/ {1, 1024},
    /*0x38*/ {4, 1},  /*0x39*/ {3, 3},  /*0x3A*/ {0, 0},  /*0x3B*/ {3, 1},
    /*0x3C*/ {6, 3},  /*0x3D*/ {3, 3},  /*0x3E*/ {2, 2},  /*0x3F*/ {1, 1024}
};

//==========================================================================
//  Helpers de matematica  (sin / cos / inverso / normalize / shiftR)
//==========================================================================

static Int16 DSP1_Sin(Int16 iAngle)
{
    if (iAngle < 0) {
        if (iAngle == -32768) return 0;
        Int16 v = DSP1_Sin(-iAngle);
        return (Int16)-v;
    }
    Int32 s = g_SinTable[iAngle >> 8]
            + ((Int32)g_MulTable[iAngle & 0xFF] * g_SinTable[0x40 + (iAngle >> 8)] >> 15);
    if (s > 32767) s = 32767;
    return (Int16)s;
}

static Int16 DSP1_Cos(Int16 iAngle)
{
    if (iAngle < 0) {
        // cos(-180) = cos(180) = -1.  Em Q15, -1 deve ser -32767
        // (0x8001), nao -32768 (0x8000).  Usar -32768 estoura
        // multiplicacoes (Int32)X * (-32768) e produz saturacao
        // assimetrica com sinal incorreto.
        if (iAngle == -32768) return -32767;
        iAngle = -iAngle;
    }
    Int32 s = g_SinTable[0x40 + (iAngle >> 8)]
            - ((Int32)g_MulTable[iAngle & 0xFF] * g_SinTable[iAngle >> 8] >> 15);
    if (s < -32768) s = -32767;
    return (Int16)s;
}

// Normalize: representa m como (Coefficient * 2^Exponent) com
// |Coefficient| em [1/2, 1].  Coefficient saida em 1.15 (signed 16).
// Exponent passado como referencia e decrementado pelo shift.
static void DSP1_Normalize(Int16 m, Int16 &Coefficient, Int16 &Exponent)
{
    Int16 i = 0x4000;
    Int16 e = 0;
    if (m < 0) {
        while ((m & i) && i) { i >>= 1; e++; }
    } else {
        while (!(m & i) && i) { i >>= 1; e++; }
    }
    if (e > 0)
        Coefficient = (Int16)((Int32)m * g_DataRom[0x21 + e] << 1);
    else
        Coefficient = m;
    Exponent -= e;
}

// Mesmo que DSP1_Normalize, mas com entrada int32 (Product).  Mantem
// a parte alta normalizada e adiciona contribuicao da parte baixa
// conforme o expoente.
static void DSP1_NormalizeDouble(Int32 Product, Int16 &Coefficient, Int16 &Exponent)
{
    Int16 n = (Int16)(Product & 0x7FFF);
    Int16 m = (Int16)(Product >> 15);
    Int16 i = 0x4000;
    Int16 e = 0;
    if (m < 0) {
        while ((m & i) && i) { i >>= 1; e++; }
    } else {
        while (!(m & i) && i) { i >>= 1; e++; }
    }
    if (e > 0) {
        Coefficient = (Int16)((Int32)m * g_DataRom[0x0021 + e] << 1);
        if (e < 15) {
            Coefficient = (Int16)(Coefficient + ((Int32)n * g_DataRom[0x0040 - e] >> 15));
        } else {
            i = 0x4000;
            if (m < 0) {
                while ((n & i) && i) { i >>= 1; e++; }
            } else {
                while (!(n & i) && i) { i >>= 1; e++; }
            }
            if (e > 15) {
                Coefficient = (Int16)((Int32)n * g_DataRom[0x0012 + e] << 1);
            } else {
                Coefficient = (Int16)(Coefficient + n);
            }
        }
    } else {
        Coefficient = m;
    }
    Exponent = e;
}

// Desnormaliza: dado Coefficient (1.15) e Exponent, devolve o valor em
// inteiro 16-bit, saturando se o expoente exceder o range.
static Int16 DSP1_DenormalizeAndClip(Int16 C, Int16 E)
{
    if (E > 0) {
        if (C > 0) return  32767;
        if (C < 0) return -32767;
        return 0;
    }
    if (E < 0) {
        return (Int16)((Int32)C * g_DataRom[0x0031 + E] >> 15);
    }
    return C;
}

static Int16 DSP1_ShiftR(Int16 C, Int16 E)
{
    return (Int16)((Int32)C * g_DataRom[0x0031 + E] >> 15);
}

// Inverso: dado (Coefficient, Exponent), produz iC, iE tais que
// (iC * 2^iE) = 1 / (Coefficient * 2^Exponent).  iC vem normalizado.
// Implementacao: chute inicial vindo do DataRom + 2 passos de Newton.
static void DSP1_Inverse(Int16 Coefficient, Int16 Exponent, Int16 &iCoefficient, Int16 &iExponent)
{
    if (Coefficient == 0) {
        iCoefficient = 0x7FFF;
        iExponent    = 0x002F;
        return;
    }

    Int16 sign = 1;
    if (Coefficient < 0) {
        if (Coefficient < -32767) Coefficient = -32767;
        Coefficient = (Int16)-Coefficient;
        sign = -1;
    }

    // normaliza ate 0x4000 <= |Coefficient| < 0x8000
    while (Coefficient < 0x4000) {
        Coefficient = (Int16)(Coefficient << 1);
        Exponent--;
    }

    if (Coefficient == 0x4000) {
        if (sign == 1) {
            iCoefficient = 0x7FFF;
        } else {
            iCoefficient = -0x4000;
            Exponent--;
        }
    } else {
        // chute inicial pela tabela
        Int16 i = (Int16)g_DataRom[((Coefficient - 0x4000) >> 7) + 0x0065];
        // dois passos de Newton: x_{n+1} = 2*x_n*(1 - C*x_n)
        i = (Int16)((i + ((-i * ((Coefficient * i) >> 15)) >> 15)) << 1);
        i = (Int16)((i + ((-i * ((Coefficient * i) >> 15)) >> 15)) << 1);
        iCoefficient = (Int16)(i * sign);
    }
    iExponent = (Int16)(1 - Exponent);
}

//==========================================================================
//  SNDSP1 - classe
//==========================================================================

static SNDSP1 *g_pSNDSP1_Instance = 0;

SNDSP1::SNDSP1()
{
    g_pSNDSP1_Instance = this;
    Reset();
}

SNDSP1 *SNDSP1::GetInstance()
{
    return g_pSNDSP1_Instance;
}

void SNDSP1::Reset()
{
    m_uSR           = (Uint8)(SR_DRC | SR_RQM);
    m_uDR           = 0x0080;
    m_uFsmState     = FSM_WAIT_CMD;
    m_uCommand      = 0;
    m_uDataCounter  = 0;
    m_bFreeze       = 0;

    memset(m_InWords, 0, sizeof(m_InWords));
    memset(m_OutWords, 0, sizeof(m_OutWords));

    memset(m_MatA, 0, sizeof(m_MatA));
    memset(m_MatB, 0, sizeof(m_MatB));
    memset(m_MatC, 0, sizeof(m_MatC));

    m_CentreX = m_CentreY = m_CentreZ = 0;
    m_CentreZ_C = m_CentreZ_E = 0;
    m_VOffset = 0;
    m_Les = m_C_Les = m_E_Les = 0;
    m_SinAas = m_CosAas = 0;
    m_SinAzs = m_CosAzs = 0;
    m_SinAZS = m_CosAZS = 0;
    m_SecAZS_C1 = m_SecAZS_E1 = 0;
    m_SecAZS_C2 = m_SecAZS_E2 = 0;
    m_Nx = m_Ny = m_Nz = 0;
    m_Gx = m_Gy = m_Gz = 0;
    m_Hx = m_Hy = 0;
    m_Vx = m_Vy = m_Vz = 0;
}

//==========================================================================
//  Implementacao dos comandos
//==========================================================================

// ---- 0x00 Multiply  /  0x20 Multiply2 ----
static void DSP1_DoMultiply(Int16 *in, Int16 *out)
{
    out[0] = (Int16)(((Int32)in[0] * in[1]) >> 15);
}
static void DSP1_DoMultiply2(Int16 *in, Int16 *out)
{
    out[0] = (Int16)((((Int32)in[0] * in[1]) >> 15) + 1);
}

// ---- 0x04 Triangle ----
static void DSP1_DoTriangle(Int16 *in, Int16 *out)
{
    Int16 a = in[0];
    Int16 r = in[1];
    out[0] = (Int16)(((Int32)DSP1_Sin(a) * r) >> 15);  // Y
    out[1] = (Int16)(((Int32)DSP1_Cos(a) * r) >> 15);  // X
}

// ---- 0x08 Radius ----
static void DSP1_DoRadius(Int16 *in, Int16 *out)
{
    Int16 X = in[0], Y = in[1], Z = in[2];
    Int32 R = ((Int32)X*X + (Int32)Y*Y + (Int32)Z*Z) << 1;
    out[0] = (Int16)(R & 0xFFFF);          // low
    out[1] = (Int16)((R >> 16) & 0xFFFF);  // high
}

// ---- 0x18 Range / 0x38 Range2 ----
static void DSP1_DoRange(Int16 *in, Int16 *out)
{
    Int16 X = in[0], Y = in[1], Z = in[2], R = in[3];
    out[0] = (Int16)(((Int32)X*X + (Int32)Y*Y + (Int32)Z*Z - (Int32)R*R) >> 15);
}
static void DSP1_DoRange2(Int16 *in, Int16 *out)
{
    Int16 X = in[0], Y = in[1], Z = in[2], R = in[3];
    out[0] = (Int16)((((Int32)X*X + (Int32)Y*Y + (Int32)Z*Z - (Int32)R*R) >> 15) + 1);
}

// ---- 0x28 Distance ----  sqrt(X^2+Y^2+Z^2) usando tabela interna
static void DSP1_DoDistance(Int16 *in, Int16 *out)
{
    Int16 X = in[0], Y = in[1], Z = in[2];
    Int32 R = (Int32)X*X + (Int32)Y*Y + (Int32)Z*Z;
    if (R == 0) { out[0] = 0; return; }

    Int16 C, E = 0;
    DSP1_NormalizeDouble(R, C, E);
    if (E & 1) C = (Int16)(((Int32)C * 0x4000) >> 15);

    Int16 Pos = (Int16)(((Int32)C * 0x40) >> 15);
    Int16 N1 = (Int16)g_DataRom[0x00D5 + Pos];
    Int16 N2 = (Int16)g_DataRom[0x00D6 + Pos];

    Int16 d = (Int16)((((Int32)(N2 - N1)) * (C & 0x1FF) >> 9) + N1);
    d = (Int16)(d >> (E >> 1));
    out[0] = d;
}

// ---- 0x0C Rotate ----
static void DSP1_DoRotate(Int16 *in, Int16 *out)
{
    Int16 A = in[0];
    Int16 X = in[1];
    Int16 Y = in[2];
    Int16 s = DSP1_Sin(A);
    Int16 c = DSP1_Cos(A);
    out[0] = (Int16)((((Int32)Y * s) >> 15) + (((Int32)X * c) >> 15));   // X2
    out[1] = (Int16)((((Int32)Y * c) >> 15) - (((Int32)X * s) >> 15));   // Y2
}

// ---- 0x1C Polar ----   (Rz * Ry * Rx) * (X,Y,Z)
static void DSP1_DoPolar(Int16 *in, Int16 *out)
{
    Int16 Az = in[0], Ay = in[1], Ax = in[2];
    Int16 X1 = in[3], Y1 = in[4], Z1 = in[5];
    Int16 X, Y, Z;

    // Rz
    X = (Int16)((((Int32)Y1 * DSP1_Sin(Az)) >> 15) + (((Int32)X1 * DSP1_Cos(Az)) >> 15));
    Y = (Int16)((((Int32)Y1 * DSP1_Cos(Az)) >> 15) - (((Int32)X1 * DSP1_Sin(Az)) >> 15));
    X1 = X; Y1 = Y;
    // Ry
    Z = (Int16)((((Int32)X1 * DSP1_Sin(Ay)) >> 15) + (((Int32)Z1 * DSP1_Cos(Ay)) >> 15));
    X = (Int16)((((Int32)X1 * DSP1_Cos(Ay)) >> 15) - (((Int32)Z1 * DSP1_Sin(Ay)) >> 15));
    out[0] = X;  Z1 = Z;
    // Rx
    Y = (Int16)((((Int32)Z1 * DSP1_Sin(Ax)) >> 15) + (((Int32)Y1 * DSP1_Cos(Ax)) >> 15));
    Z = (Int16)((((Int32)Z1 * DSP1_Cos(Ax)) >> 15) - (((Int32)Y1 * DSP1_Sin(Ax)) >> 15));
    out[1] = Y;
    out[2] = Z;
}

// ---- 0x10 Inverse ----
static void DSP1_DoInverse(Int16 *in, Int16 *out)
{
    DSP1_Inverse(in[0], in[1], out[0], out[1]);
}

// ---- 0x0F MemoryTest / 0x27 MemorySize ----
static void DSP1_DoMemoryTest(Int16 *in, Int16 *out)
{
    (void)in;
    out[0] = (Int16)0xAAAA;
}
static void DSP1_DoMemorySize(Int16 *in, Int16 *out)
{
    (void)in;
    out[0] = 0x0100;
}
// ---- 0x1F MemoryDump ----
static void DSP1_DoMemoryDump(Int16 *in, Int16 *out)
{
    (void)in;
    memcpy(out, g_DataRom, 2048);
}

//---- Attitude A/B/C (op 01/11/21) ----
static void DSP1_BuildAttitude(Int16 (*M)[3], Int16 S, Int16 Rz, Int16 Ry, Int16 Rx)
{
    Int16 SinRz = DSP1_Sin(Rz);
    Int16 CosRz = DSP1_Cos(Rz);
    Int16 SinRy = DSP1_Sin(Ry);
    Int16 CosRy = DSP1_Cos(Ry);
    Int16 SinRx = DSP1_Sin(Rx);
    Int16 CosRx = DSP1_Cos(Rx);
    S = (Int16)(S >> 1);

    M[0][0] = (Int16)((((Int32)S * CosRz) >> 15) * CosRy >> 15);
    M[0][1] = (Int16)((((Int32)S * SinRz >> 15) * CosRx >> 15)
                   + ((((Int32)S * CosRz >> 15) * SinRx >> 15) * SinRy >> 15));
    M[0][2] = (Int16)((((Int32)S * SinRz >> 15) * SinRx >> 15)
                   - ((((Int32)S * CosRz >> 15) * CosRx >> 15) * SinRy >> 15));

    M[1][0] = (Int16)(-(((Int32)S * SinRz >> 15) * CosRy >> 15));
    M[1][1] = (Int16)((((Int32)S * CosRz >> 15) * CosRx >> 15)
                   - ((((Int32)S * SinRz >> 15) * SinRx >> 15) * SinRy >> 15));
    M[1][2] = (Int16)((((Int32)S * CosRz >> 15) * SinRx >> 15)
                   + ((((Int32)S * SinRz >> 15) * CosRx >> 15) * SinRy >> 15));

    M[2][0] = (Int16)(((Int32)S * SinRy) >> 15);
    M[2][1] = (Int16)(-((((Int32)S * SinRx) >> 15) * CosRy >> 15));
    M[2][2] = (Int16)((((Int32)S * CosRx) >> 15) * CosRy >> 15);
}

//---- Subjective / Objective (M * v   OU   M^T * v) ----
static void DSP1_Subjective(const Int16 (*M)[3], Int16 *in, Int16 *out)
{
    Int16 F = in[0], L = in[1], U = in[2];
    out[0] = (Int16)(((Int32)M[0][0]*F >> 15) + ((Int32)M[0][1]*L >> 15) + ((Int32)M[0][2]*U >> 15));
    out[1] = (Int16)(((Int32)M[1][0]*F >> 15) + ((Int32)M[1][1]*L >> 15) + ((Int32)M[1][2]*U >> 15));
    out[2] = (Int16)(((Int32)M[2][0]*F >> 15) + ((Int32)M[2][1]*L >> 15) + ((Int32)M[2][2]*U >> 15));
}
static void DSP1_Objective(const Int16 (*M)[3], Int16 *in, Int16 *out)
{
    Int16 X = in[0], Y = in[1], Z = in[2];
    out[0] = (Int16)(((Int32)M[0][0]*X >> 15) + ((Int32)M[1][0]*Y >> 15) + ((Int32)M[2][0]*Z >> 15));
    out[1] = (Int16)(((Int32)M[0][1]*X >> 15) + ((Int32)M[1][1]*Y >> 15) + ((Int32)M[2][1]*Z >> 15));
    out[2] = (Int16)(((Int32)M[0][2]*X >> 15) + ((Int32)M[1][2]*Y >> 15) + ((Int32)M[2][2]*Z >> 15));
}

//---- Scalar (produto escalar com a 1a coluna da matriz) ----
static Int16 DSP1_Scalar(const Int16 (*M)[3], Int16 X, Int16 Y, Int16 Z)
{
    return (Int16)(((Int32)X*M[0][0] + (Int32)Y*M[1][0] + (Int32)Z*M[2][0]) >> 15);
}

// ---- 0x14 Gyrate ----  novas atitudes a partir de DeltaF/L/U
static void DSP1_DoGyrate(Int16 *in, Int16 *out)
{
    Int16 Az = in[0];
    Int16 Ax = in[1];
    Int16 Ay = in[2];
    Int16 U  = in[3];
    Int16 F  = in[4];
    Int16 L  = in[5];

    Int16 SinAy = DSP1_Sin(Ay);
    Int16 CosAy = DSP1_Cos(Ay);

    Int16 CSec, ESec, CSin, C, E;

    DSP1_Inverse(DSP1_Cos(Ax), 0, CSec, ESec);

    // Rz
    DSP1_NormalizeDouble((Int32)U * CosAy - (Int32)F * SinAy, C, E);
    E = (Int16)(ESec - E);
    DSP1_Normalize((Int16)(((Int32)C * CSec) >> 15), C, E);
    out[0] = (Int16)(Az + DSP1_DenormalizeAndClip(C, E));

    // Rx
    out[1] = (Int16)(Ax + (Int16)(((Int32)U * SinAy) >> 15) + (Int16)(((Int32)F * CosAy) >> 15));

    // Ry
    DSP1_NormalizeDouble((Int32)U * CosAy + (Int32)F * SinAy, C, E);
    E = (Int16)(ESec - E);
    DSP1_Normalize(DSP1_Sin(Ax), CSin, E);
    DSP1_Normalize((Int16)(-(((Int32)C * (((Int32)CSec * CSin) >> 15)) >> 15)), C, E);
    out[2] = (Int16)(Ay + DSP1_DenormalizeAndClip(C, E) + L);
}

//==========================================================================
//  Mode-7 / Camera commands  (Parameter / Project / Raster / Target)
//==========================================================================

void SNDSP1::Execute(Uint8 uCmd)
{
    Int16 *in  = m_InWords;
    Int16 *out = m_OutWords;
    Uint8 c    = (Uint8)(uCmd & 0x3F);

    switch (c)
    {
    // ----- ariths -----
    case 0x00: DSP1_DoMultiply (in, out); break;
    case 0x20: DSP1_DoMultiply2(in, out); break;
    case 0x04: case 0x24: DSP1_DoTriangle(in, out); break;
    case 0x08: DSP1_DoRadius(in, out); break;
    case 0x18: DSP1_DoRange (in, out); break;
    case 0x38: DSP1_DoRange2(in, out); break;
    case 0x28: DSP1_DoDistance(in, out); break;
    case 0x0C: case 0x2C: DSP1_DoRotate(in, out); break;
    case 0x1C: case 0x3C: DSP1_DoPolar (in, out); break;
    case 0x10: case 0x30: DSP1_DoInverse(in, out); break;

    // ----- memory tests -----
    case 0x0F: case 0x2F: DSP1_DoMemoryTest(in, out); break;
    case 0x27: DSP1_DoMemorySize(in, out); break;
    case 0x1F: case 0x37: case 0x3F: DSP1_DoMemoryDump(in, out); break;

    // ----- attitude / subjective / objective / scalar -----
    case 0x01: case 0x05: case 0x31: case 0x35:
        DSP1_BuildAttitude(m_MatA, in[0], in[1], in[2], in[3]); break;
    case 0x11: case 0x15:
        DSP1_BuildAttitude(m_MatB, in[0], in[1], in[2], in[3]); break;
    case 0x21: case 0x25:
        DSP1_BuildAttitude(m_MatC, in[0], in[1], in[2], in[3]); break;

    case 0x03: case 0x33: DSP1_Subjective(m_MatA, in, out); break;
    case 0x13:           DSP1_Subjective(m_MatB, in, out); break;
    case 0x23:           DSP1_Subjective(m_MatC, in, out); break;

    case 0x09: case 0x0D: case 0x39: case 0x3D:
        DSP1_Objective(m_MatA, in, out); break;
    case 0x19: case 0x1D:
        DSP1_Objective(m_MatB, in, out); break;
    case 0x29: case 0x2D:
        DSP1_Objective(m_MatC, in, out); break;

    case 0x0B: case 0x3B:
        out[0] = DSP1_Scalar(m_MatA, in[0], in[1], in[2]); break;
    case 0x1B:
        out[0] = DSP1_Scalar(m_MatB, in[0], in[1], in[2]); break;
    case 0x2B:
        out[0] = DSP1_Scalar(m_MatC, in[0], in[1], in[2]); break;

    case 0x14: case 0x34: DSP1_DoGyrate(in, out); break;

    // ----- camera setup (Parameter) -----
    case 0x02: case 0x12: case 0x22: case 0x32: {  // Parameter

        Int16 Fx = in[0], Fy = in[1], Fz = in[2];
        Int16 Lfe = in[3];
        Int16 Les = in[4];
        Int16 Aas = in[5];
        Int16 Azs = in[6];
        Int16 AZS = Azs;
        Int16 C, E, CSec;

        m_Les   = Les;
        m_E_Les = 0;
        DSP1_Normalize(Les, m_C_Les, m_E_Les);

        m_SinAas = DSP1_Sin(Aas);
        m_CosAas = DSP1_Cos(Aas);
        m_SinAzs = DSP1_Sin(Azs);
        m_CosAzs = DSP1_Cos(Azs);

        m_Nx = (Int16)(((Int32)m_SinAzs * -m_SinAas) >> 15);
        m_Ny = (Int16)(((Int32)m_SinAzs *  m_CosAas) >> 15);
        m_Nz = (Int16)(((Int32)m_CosAzs *  0x7FFF ) >> 15);

        m_Hx = (Int16)(((Int32)m_CosAas * 0x7FFF) >> 15);
        m_Hy = (Int16)(((Int32)m_SinAas * 0x7FFF) >> 15);

        m_Vx = (Int16)(((Int32)m_CosAzs * -m_SinAas) >> 15);
        m_Vy = (Int16)(((Int32)m_CosAzs *  m_CosAas) >> 15);
        m_Vz = (Int16)(((Int32)-m_SinAzs * 0x7FFF) >> 15);

        Int16 LfeNx = (Int16)(((Int32)Lfe * m_Nx) >> 15);
        Int16 LfeNy = (Int16)(((Int32)Lfe * m_Ny) >> 15);
        Int16 LfeNz = (Int16)(((Int32)Lfe * m_Nz) >> 15);

        m_CentreX = (Int16)(Fx + LfeNx);
        m_CentreY = (Int16)(Fy + LfeNy);
        m_CentreZ = (Int16)(Fz + LfeNz);

        Int16 LesNx = (Int16)(((Int32)Les * m_Nx) >> 15);
        Int16 LesNy = (Int16)(((Int32)Les * m_Ny) >> 15);
        Int16 LesNz = (Int16)(((Int32)Les * m_Nz) >> 15);

        m_Gx = (Int16)(m_CentreX - LesNx);
        m_Gy = (Int16)(m_CentreY - LesNy);
        m_Gz = (Int16)(m_CentreZ - LesNz);

        E = 0;
        DSP1_Normalize(m_CentreZ, C, E);
        m_CentreZ_C = C;
        m_CentreZ_E = E;

        Int16 MaxAZS = g_MaxAZS_Exp[-E];
        if (AZS < 0) {
            MaxAZS = (Int16)-MaxAZS;
            if (AZS < MaxAZS + 1) AZS = (Int16)(MaxAZS + 1);
        } else {
            if (AZS > MaxAZS) AZS = MaxAZS;
        }

        m_SinAZS = DSP1_Sin(AZS);
        m_CosAZS = DSP1_Cos(AZS);

        DSP1_Inverse(m_CosAZS, 0, m_SecAZS_C1, m_SecAZS_E1);
        DSP1_Normalize((Int16)(((Int32)C * m_SecAZS_C1) >> 15), C, E);
        E = (Int16)(E + m_SecAZS_E1);
        C = (Int16)(((Int32)DSP1_DenormalizeAndClip(C, E) * m_SinAZS) >> 15);

        m_CentreX = (Int16)(m_CentreX + (((Int32)C * m_SinAas) >> 15));
        m_CentreY = (Int16)(m_CentreY - (((Int32)C * m_CosAas) >> 15));

        out[2] = m_CentreX;  // Cx
        out[3] = m_CentreY;  // Cy

        Int16 Vof = 0;
        if ((Azs != AZS) || (Azs == MaxAZS)) {
            if (Azs == -32768) Azs = -32767;
            Int16 c2 = (Int16)(Azs - MaxAZS);
            if (c2 >= 0) c2--;
            Int16 Aux = (Int16)~(c2 << 2);

            c2 = (Int16)(((Int32)Aux * g_DataRom[0x0328]) >> 15);
            c2 = (Int16)((((Int32)c2 * Aux) >> 15) + g_DataRom[0x0327]);
            Vof = (Int16)(Vof - (((Int32)(((Int32)c2 * Aux) >> 15) * Les) >> 15));

            c2 = (Int16)(((Int32)Aux * Aux) >> 15);
            Aux = (Int16)((((Int32)c2 * g_DataRom[0x0324]) >> 15) + g_DataRom[0x0325]);
            m_CosAZS = (Int16)(m_CosAZS + (((Int32)(((Int32)c2 * Aux) >> 15) * m_CosAZS) >> 15));
        }

        m_VOffset = (Int16)(((Int32)Les * m_CosAZS) >> 15);

        DSP1_Inverse(m_SinAZS, 0, CSec, E);
        DSP1_Normalize(m_VOffset, C, E);
        DSP1_Normalize((Int16)(((Int32)C * CSec) >> 15), C, E);
        if (C == -32768) { C = (Int16)(C >> 1); E++; }
        out[1] = DSP1_DenormalizeAndClip((Int16)-C, E);  // Vva
        out[0] = Vof;                                    // Vof

        DSP1_Inverse(m_CosAZS, 0, m_SecAZS_C2, m_SecAZS_E2);
        break;
    }

    // ----- Project (op06) -----
    case 0x06: case 0x16: case 0x26: case 0x36: {  // Project

        Int16 X = in[0], Y = in[1], Z = in[2];
        Int16 Px, Py, Pz;
        Int16 E = 0, E2 = 0, E3 = 0, E4 = 0, refE, E6, E7;
        Int16 C2, C4, C6, C9, C10, C11, C12;
        Int32 aux4;

        DSP1_NormalizeDouble((Int32)X - m_Gx, Px, E4);
        DSP1_NormalizeDouble((Int32)Y - m_Gy, Py, E );
        DSP1_NormalizeDouble((Int32)Z - m_Gz, Pz, E3);
        Px = (Int16)(Px >> 1); E4--;
        Py = (Int16)(Py >> 1); E--;
        Pz = (Int16)(Pz >> 1); E3--;

        refE = (E < E3) ? E : E3;
        refE = (refE < E4) ? refE : E4;

        Px = DSP1_ShiftR(Px, (Int16)(E4   - refE));
        Py = DSP1_ShiftR(Py, (Int16)(E    - refE));
        Pz = DSP1_ShiftR(Pz, (Int16)(E3   - refE));

        C11 = (Int16)(-(((Int32)Px * m_Nx) >> 15));
        C9  = (Int16)(-(((Int32)Py * m_Ny) >> 15));
        Int16 C8 = (Int16)(-(((Int32)Pz * m_Nz) >> 15));
        C12 = (Int16)(C11 + C9 + C8);

        aux4 = C12;
        // Shift seguro: refE original esta tipicamente em [-15..-1],
        // entao 16-refE pode chegar a 31.  Shift left de Int32 por
        // valores >= 31 e' UB em C++ (na EE do PS2 com gcc 3.x devolve
        // 0/lixo).  Aqui saturamos para evitar isso.
        //
        // IMPORTANTE: refE NAO pode ser sobrescrito aqui!  Ele e'
        // reusado nas linhas finais "m_E_Les - E2 + refE + E7" para
        // calcular o expoente das saidas H/V.  Mantemos o "shift"
        // como variavel local apenas para a logica de saturacao.
        //
        // Sequencia conforme documentacao publica do DSP-1:
        //   aux4 = (C12 << (16 - refE)) >> 1;
        //   if (aux4 == -1) aux4 = 0;
        // O ">>1" deve vir ANTES do teste por -1 (caso aux4=-2 vire
        // -1 apos o shift, o teste deve apanhar essa correcao).
        {
            Int16 shift = (Int16)(16 - refE);
            if (shift >= 0) {
                if (shift >= 31) aux4 = 0;
                else             aux4 <<= shift;
            } else {
                Int16 r = (Int16)(-shift);
                if (r >= 31) aux4 = (aux4 < 0) ? -1 : 0;
                else         aux4 >>= r;
            }
            // (refE deve permanecer = min(E, E3, E4) original)
        }
        aux4 >>= 1;
        if (aux4 == -1) aux4 = 0;

        Int32 aux = (Uint16)m_Les + aux4;
        DSP1_NormalizeDouble(aux, C10, E2);
        E2 = (Int16)(15 - E2);

        DSP1_Inverse(C10, 0, C4, E4);
        C2 = (Int16)(((Int32)C4 * m_C_Les) >> 15);

        // H
        E7 = 0;
        Int16 C16 = (Int16)(((Int32)Px * m_Hx) >> 15);
        Int16 C20 = (Int16)(((Int32)Py * m_Hy) >> 15);
        Int16 C17 = (Int16)(C16 + C20);
        Int16 C18 = (Int16)(((Int32)C17 * C2) >> 15);
        Int16 C19;
        DSP1_Normalize(C18, C19, E7);
        out[0] = DSP1_DenormalizeAndClip(C19, (Int16)(m_E_Les - E2 + refE + E7));

        // V
        E6 = 0;
        Int16 C21 = (Int16)(((Int32)Px * m_Vx) >> 15);
        Int16 C22 = (Int16)(((Int32)Py * m_Vy) >> 15);
        Int16 C23 = (Int16)(((Int32)Pz * m_Vz) >> 15);
        Int16 C24 = (Int16)(C21 + C22 + C23);
        Int16 C26 = (Int16)(((Int32)C24 * C2) >> 15);
        Int16 C25;
        DSP1_Normalize(C26, C25, E6);
        out[1] = DSP1_DenormalizeAndClip(C25, (Int16)(m_E_Les - E2 + refE + E6));

        // M
        DSP1_Normalize(C2, C6, E4);
        out[2] = DSP1_DenormalizeAndClip(C6, (Int16)(E4 + m_E_Les - E2 - 7));
        break;
    }

    // ----- Raster (op0A) -- matriz Mode-7 por scanline ----
    case 0x0A: {
        Int16 Vs = in[0];
        Int16 C, E, C1, E1;

        DSP1_Inverse((Int16)((((Int32)Vs * m_SinAzs) >> 15) + m_VOffset), 7, C, E);
        E = (Int16)(E + m_CentreZ_E);
        C1 = (Int16)(((Int32)C * m_CentreZ_C) >> 15);

        E1 = (Int16)(E + m_SecAZS_E2);

        DSP1_Normalize(C1, C, E);
        C = DSP1_DenormalizeAndClip(C, E);

        out[0] = (Int16)(((Int32)C * m_CosAas) >> 15);     // An
        out[2] = (Int16)(((Int32)C * m_SinAas) >> 15);     // Cn

        DSP1_Normalize((Int16)(((Int32)C1 * m_SecAZS_C2) >> 15), C, E1);
        C = DSP1_DenormalizeAndClip(C, E1);

        out[1] = (Int16)(((Int32)C * -m_SinAas) >> 15);    // Bn
        out[3] = (Int16)(((Int32)C *  m_CosAas) >> 15);    // Dn
        break;
    }

    // ----- Target (op0E) ----  (H,V) -> (X,Y) no plano
    case 0x0E: case 0x1E: case 0x2E: case 0x3E: {
        Int16 H = in[0], V = in[1];
        Int16 C, E, C1, E1;

        DSP1_Inverse((Int16)((((Int32)V * m_SinAzs) >> 15) + m_VOffset), 8, C, E);
        E = (Int16)(E + m_CentreZ_E);
        C1 = (Int16)(((Int32)C * m_CentreZ_C) >> 15);

        E1 = (Int16)(E + m_SecAZS_E1);

        H = (Int16)(H << 8);
        DSP1_Normalize(C1, C, E);
        C = (Int16)(((Int32)DSP1_DenormalizeAndClip(C, E) * H) >> 15);
        Int16 X = (Int16)(m_CentreX + (Int16)(((Int32)C * m_CosAas) >> 15));
        Int16 Y = (Int16)(m_CentreY + (Int16)(((Int32)C * m_SinAas) >> 15));

        V = (Int16)(V << 8);
        DSP1_Normalize((Int16)(((Int32)C1 * m_SecAZS_C1) >> 15), C, E1);
        C = (Int16)(((Int32)DSP1_DenormalizeAndClip(C, E1) * V) >> 15);
        X = (Int16)(X + (Int16)(((Int32)C * -m_SinAas) >> 15));
        Y = (Int16)(Y + (Int16)(((Int32)C *  m_CosAas) >> 15));

        out[0] = X;
        out[1] = Y;
        break;
    }

    default:
        // opcode invalido: zera saidas
        memset(out, 0, sizeof(m_OutWords));
        break;
    }
}

//==========================================================================
//  FSM / Bus Interface
//==========================================================================

void SNDSP1::FsmStep(bool bRead, Uint8 &rData)
{
    // espera de RQM
    if (!(m_uSR & SR_RQM)) return;

    // bind DR <-> rData segundo DRS (byte alto / byte baixo)
    if (bRead) {
        if (m_uSR & SR_DRS) rData = (Uint8)(m_uDR >> 8);
        else                rData = (Uint8)(m_uDR & 0xFF);
    } else {
        if (m_uSR & SR_DRS) {
            m_uDR = (Uint16)((m_uDR & 0x00FF) | ((Uint16)rData << 8));
        } else {
            m_uDR = (Uint16)((m_uDR & 0xFF00) | rData);
        }
    }

    switch (m_uFsmState) {
    case FSM_WAIT_CMD: {
        m_uCommand = (Uint8)m_uDR;
        if (!(m_uCommand & 0xC0)) {
            switch (m_uCommand & 0x3F) {
            case 0x1A:
            case 0x2A:
            case 0x3A:
                m_bFreeze = 1;
                break;
            default:
                m_uDataCounter = 0;
                m_uFsmState    = FSM_READ_DATA;
                m_uSR         &= (Uint8)~SR_DRC;
                // O proximo byte que a CPU escrever e' o MSB da
                // primeira word de parametro.  DRS=1 indica que o
                // proximo acesso e' o byte ALTO do DR (MSB-first).
                // Sem isso os bytes de cada word ficam invertidos
                // e todos os parametros numericos chegam com
                // endianness trocado (bug fatal para Mario Kart).
                m_uSR |= SR_DRS;
                break;
            }
        }
        break;
    }

    case FSM_READ_DATA: {
        m_uSR ^= SR_DRS;
        if (!(m_uSR & SR_DRS)) {
            // veio o LSB - palavra completa
            m_InWords[m_uDataCounter++] = (Int16)m_uDR;

            Uint8 nIn = g_CmdTable[m_uCommand & 0x3F].nIn;
            if (m_uDataCounter >= nIn) {
                Execute(m_uCommand);
                Uint16 nOut = g_CmdTable[m_uCommand & 0x3F].nOut;
                if (nOut > 0) {
                    m_uDataCounter = 0;
                    m_uDR          = (Uint16)m_OutWords[0];
                    m_uFsmState    = FSM_WRITE_DATA;
                    // Primeiro byte que a CPU le e' o MSB do primeiro
                    // resultado.  Garantimos DRS=1 para que ReadData
                    // devolva DR>>8 (byte alto) primeiro.
                    m_uSR |= SR_DRS;
                } else {
                    m_uDR       = 0x0080;
                    m_uFsmState = FSM_WAIT_CMD;
                    m_uSR      |= SR_DRC;
                }
            }
        }
        break;
    }

    case FSM_WRITE_DATA: {
        m_uSR ^= SR_DRS;
        if (!(m_uSR & SR_DRS)) {
            ++m_uDataCounter;
            Uint16 nOut = g_CmdTable[m_uCommand & 0x3F].nOut;
            if (m_uDataCounter >= nOut) {
                // Op0A em modo continuo: re-executa com Vs incrementado
                // e enfileira proximo conjunto de saidas.  A interrupcao
                // do stream e' tratada em WriteData(): qualquer escrita
                // da CPU enquanto estamos aqui forca volta a WAIT_CMD.
                Uint8 cmd = (Uint8)(m_uCommand & 0x3F);
                if (cmd == 0x0A) {
                    m_InWords[0]++;
                    Execute(m_uCommand);
                    m_uDataCounter = 0;
                    m_uDR = (Uint16)m_OutWords[0];
                    // Reinicia ciclo de leitura: primeiro byte = MSB
                    m_uSR |= SR_DRS;
                } else {
                    m_uDR       = 0x0080;
                    m_uFsmState = FSM_WAIT_CMD;
                    m_uSR      |= SR_DRC;
                }
            } else {
                m_uDR = (Uint16)m_OutWords[m_uDataCounter];
                // Proximo word pronto: primeiro byte lido = MSB
                m_uSR |= SR_DRS;
            }
        }
        break;
    }
    }

    if (m_bFreeze) m_uSR &= (Uint8)~SR_RQM;
}

void SNDSP1::WriteData(Uint32 /*uAddr*/, Uint8 uData)
{
    // Stream-break para Op0A em modo continuo: qualquer escrita
    // enquanto o DSP esta entregando matrizes Mode-7 e' interpretada
    // como pedido de novo opcode pelo jogo.  Sem esse atalho o DSP
    // ficaria preso emitindo An/Bn/Cn/Dn ad infinitum.
    if (m_uFsmState == FSM_WRITE_DATA && (m_uCommand & 0x3F) == 0x0A) {
        m_uFsmState = FSM_WAIT_CMD;
        m_uSR      |= SR_DRC;
    }
    Uint8 d = uData;
    FsmStep(false, d);
}

Uint8 SNDSP1::ReadData(Uint32 /*uAddr*/)
{
    Uint8 d = 0;
    FsmStep(true, d);
    return d;
}

Uint8 SNDSP1::ReadStatus(Uint32 /*uAddr*/)
{
    // SR e' lido pela CPU como um byte unico em $Bx:Cxxx (LoROM) /
    // $0x:7xxx (HiROM).  Devolve sempre o byte alto que carrega
    // RQM/DRC/DRS.  A versao antiga retornava 0 alternadamente
    // (toggle "byte baixo") - isso punia o jogo a ver RQM=0 em
    // metade das leituras, dessincronizando o handshake e fazendo
    // Mario Kart desistir de bytes do DR.
    return m_uSR;
}
