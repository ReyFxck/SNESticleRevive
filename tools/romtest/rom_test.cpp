#include <stdio.h>
#include <string.h>
#include <vector>

#include "types.h"
#include "dataio.h"
#include "snrom.h"

static int g_Failures = 0;

#define CHECK(condition, message) do { \
	if (!(condition)) { \
		fprintf(stderr, "FAIL: %s (%s:%d)\n", message, __FILE__, __LINE__); \
		g_Failures++; \
	} \
} while (0)

static void PutHeader(std::vector<Uint8> &rom, Uint32 headerOffset,
	const char *title, Uint8 mapMode, Uint16 checksum, Uint16 resetVector,
	Uint32 resetBase)
{
	SNRomInfoT *info = (SNRomInfoT *)&rom[headerOffset];
	memset(info, 0, sizeof(*info));
	memset(info->Title, ' ', sizeof(info->Title));
	if (title)
	{
		size_t length = strlen(title);
		if (length > sizeof(info->Title)) length = sizeof(info->Title);
		memcpy(info->Title, title, length);
	}
	info->RomMakeup = mapMode;
	info->RomType = 0x00;
	info->RomSize = 0x0C;
	info->SRAMSize = 0x00;
	info->Country = 0x01;
	info->License = 0x33;
	info->GameVersion = 0x00;
	info->Checksum = checksum;
	info->InverseChecksum = (Uint16)(checksum ^ 0xFFFF);

	rom[headerOffset + 0x3C] = (Uint8)resetVector;
	rom[headerOffset + 0x3D] = (Uint8)(resetVector >> 8);
	if (resetVector >= 0x8000)
		rom[resetBase + (resetVector & 0x7FFF)] = 0x78; // SEI
}

static void PutFalseOppositeHeader(std::vector<Uint8> &rom,
	Uint32 headerOffset, Uint8 mapMode)
{
	SNRomInfoT *info = (SNRomInfoT *)&rom[headerOffset];
	memset(info, 0, sizeof(*info));
	info->RomMakeup = mapMode;
	info->RomSize = 0xFF;
	info->SRAMSize = 0xFF;
	info->Country = 0xFF;
	info->Checksum = 0x0000;
	info->InverseChecksum = 0xFFFF;
	rom[headerOffset + 0x3C] = 0x00;
	rom[headerOffset + 0x3D] = 0x00;
}

static std::vector<Uint8> MakeType1Image(const std::vector<Uint8> &linear)
{
	const Uint32 blockBytes = 0x8000;
	const Uint32 halfBlockCount = (Uint32)linear.size() >> 16;
	const Uint32 blockCount = halfBlockCount * 2;
	std::vector<Uint8> interleaved(linear.size());

	CHECK(linear.size() >= 0x10000 && !(linear.size() & 0xFFFF),
		"imagem Type-1 de teste deve usar blocos completos de 64 KiB");
	for (Uint32 outputBlock = 0; outputBlock < blockCount; outputBlock++)
	{
		Uint32 inputBlock = (outputBlock & 1)
			? (outputBlock >> 1)
			: (halfBlockCount + (outputBlock >> 1));
		memcpy(&interleaved[inputBlock * blockBytes],
		       &linear[outputBlock * blockBytes], blockBytes);
	}
	return interleaved;
}

static void TestCleanLoRom(void)
{
	std::vector<Uint8> rom(0x100000, 0xFF);
	PutHeader(rom, 0x7FC0, "TEST LOROM", 0x20, 0x1234, 0x8000, 0x0000);

	CMemFileIO io;
	SnesRom snesRom;
	io.Open(&rom[0], (Uint32)rom.size());
	CHECK(snesRom.LoadRom(&io) == Emu::Rom::LOADERROR_NONE,
		"LoROM limpa deve carregar");
	CHECK(snesRom.m_eMapping == SNROM_MAPPING_LOROM,
		"LoROM limpa deve selecionar LoROM");
	CHECK(snesRom.GetRomTitle() && !strcmp(snesRom.GetRomTitle(), "TEST LOROM"),
		"LoROM limpa deve preservar o titulo");
}

static void TestCleanHiRom(void)
{
	std::vector<Uint8> rom(0x300000, 0xFF);
	PutHeader(rom, 0xFFC0, "TEST HIROM", 0x31, 0x5258, 0x8000, 0x8000);

	CMemFileIO io;
	SnesRom snesRom;
	io.Open(&rom[0], (Uint32)rom.size());
	CHECK(snesRom.LoadRom(&io) == Emu::Rom::LOADERROR_NONE,
		"HiROM limpa deve carregar");
	CHECK(snesRom.m_eMapping == SNROM_MAPPING_HIROM,
		"HiROM limpa deve selecionar HiROM");
	CHECK(snesRom.GetRomTitle() && !strcmp(snesRom.GetRomTitle(), "TEST HIROM"),
		"HiROM limpa deve preservar o titulo");
}

static void TestPinocchioFalseType1Regression(void)
{
	std::vector<Uint8> rom(0x300000, 0xFF);

	/* Reproduz a falha do dump limpo de Pinocchio (USA): um padrao no ponto
	   LoROM parece um header com checksum complementar e diz HiROM. O loader
	   antigo aceitava esse falso sinal antes de avaliar o header HiROM real e
	   embaralhava os 3 MiB como uma imagem copier Type-1. */
	PutFalseOppositeHeader(rom, 0x7FC0, 0x31);
	PutHeader(rom, 0xFFC0, "Pinocchio", 0x31, 0x5258, 0x8000, 0x8000);

	CMemFileIO io;
	SnesRom snesRom;
	io.Open(&rom[0], (Uint32)rom.size());
	CHECK(snesRom.LoadRom(&io) == Emu::Rom::LOADERROR_NONE,
		"Pinocchio deve carregar");
	CHECK(snesRom.m_eMapping == SNROM_MAPPING_HIROM,
		"Pinocchio deve continuar HiROM, sem falso deinterleave");
	CHECK(snesRom.GetRomTitle() && !strcmp(snesRom.GetRomTitle(), "Pinocchio"),
		"Pinocchio deve manter o header verdadeiro");
	CHECK(snesRom.GetMapperName() && !strcmp(snesRom.GetMapperName(), "HiROM"),
		"diagnostico deve identificar HiROM");
}

static void TestRealType1StillWorks(void)
{
	std::vector<Uint8> linear(0x100000, 0xFF);
	PutHeader(linear, 0xFFC0, "TYPE1 HIROM", 0x31, 0x3456,
		0x8000, 0x8000);
	std::vector<Uint8> interleaved = MakeType1Image(linear);

	/* Numa HiROM Type-1, o bloco que continha $FFC0 aparece fisicamente em
	   $7FC0 e ainda declara HiROM: este e' o sinal verdadeiro que precisa
	   continuar acionando a conversao. */
	CHECK(interleaved[0x7FD5] == 0x31,
		"fixture Type-1 deve expor header HiROM no ponto LoROM");

	CMemFileIO io;
	SnesRom snesRom;
	io.Open(&interleaved[0], (Uint32)interleaved.size());
	CHECK(snesRom.LoadRom(&io) == Emu::Rom::LOADERROR_NONE,
		"HiROM Type-1 deve carregar");
	CHECK(snesRom.m_eMapping == SNROM_MAPPING_HIROM,
		"HiROM Type-1 deve voltar ao mapper HiROM");
	CHECK(snesRom.GetRomTitle() && !strcmp(snesRom.GetRomTitle(), "TYPE1 HIROM"),
		"HiROM Type-1 deve preservar o header depois da conversao");
	CHECK(!memcmp(snesRom.GetData(), &linear[0], linear.size()),
		"conversao Type-1 deve restaurar todos os blocos da ROM");
}

int main(void)
{
	TestCleanLoRom();
	TestCleanHiRom();
	TestPinocchioFalseType1Regression();
	TestRealType1StillWorks();

	if (g_Failures)
	{
		fprintf(stderr, "%d teste(s) falharam\n", g_Failures);
		return 1;
	}

	printf("rom_test: OK\n");
	return 0;
}
