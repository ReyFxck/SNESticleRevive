
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "types.h"
#include "snrom.h"
#include "dataio.h"
#include "sndbglog.h"

/* Pontua um header LoROM candidato em 'base' (deslocamento do $FFC0 da
   metade). Usado para descobrir QUAL metade de uma ROM ExLoROM contem o
   header/vetores reais, para normalizar a ordem (igual ao scoring do
   snes9x, porem simplificado). */
static int _ExLoRomHeaderScore(const Uint8 *pRom, Uint32 base, Uint32 romBytes)
{
	if ((base + 0x40) > romBytes) return -1000;

	int score = 0;

	// reset vector ($FFFC) deve apontar para a area de ROM (>= $8000)
	Uint16 reset = (Uint16)(pRom[base + 0x3C] | (pRom[base + 0x3D] << 8));
	if (reset >= 0x8000) score += 8; else score -= 8;

	// checksum + complemento == 0xFFFF
	Uint16 cmp = (Uint16)(pRom[base + 0x1C] | (pRom[base + 0x1D] << 8));
	Uint16 chk = (Uint16)(pRom[base + 0x1E] | (pRom[base + 0x1F] << 8));
	if (chk != 0 && (Uint16)(chk + cmp) == 0xFFFF) score += 8;

	// titulo em ASCII imprimivel
	int printable = 0;
	for (int i = 0; i < 21; i++) {
		Uint8 c = pRom[base + i];
		if (c >= 0x20 && c < 0x7F) printable++;
	}
	if (printable >= 16) score += 4;

	return score;
}


//
//
//


struct SNRomCountryT
{
	const char *pName;
	SNRomVideoE eVideoType;
};

struct SNRomLicenseT
{
	Uint8	uCode;
	const char *pName;
};


//
//
//

static SNRomCountryT _SNRom_Country[]=
{
    { "Japan"                                   ,   SNROM_VIDEO_NTSC    },
    { "USA"                                     ,   SNROM_VIDEO_NTSC    },
    { "Australia, Europe, Oceania and Asia"     ,   SNROM_VIDEO_PAL     },
    { "Sweden"                                  ,   SNROM_VIDEO_PAL     },
    { "Finland"                                 ,   SNROM_VIDEO_PAL     },
    { "Denmark"                                 ,   SNROM_VIDEO_PAL     },
    { "France"                                  ,   SNROM_VIDEO_PAL     },
    { "Holland"                                 ,   SNROM_VIDEO_PAL     },
    { "Spain"                                   ,   SNROM_VIDEO_PAL     },
    { "Germany, Austria and Switzerland"        ,   SNROM_VIDEO_PAL     },
    { "Italy"                                   ,   SNROM_VIDEO_PAL     },
    { "Hong Kong and China"                     ,   SNROM_VIDEO_PAL     },
    { "Indonesia"                               ,   SNROM_VIDEO_PAL     },
    { "Korea"                                   ,   SNROM_VIDEO_PAL     },
};

static SNRomLicenseT _SNRom_License[]=
{
    { 1   , "Nintendo"                                  },
    { 3   , "Imagineer-Zoom"                            },
    { 5   , "Zamuse"                                    },
    { 6   , "Falcom"                                    },
    { 8   , "Capcom"                                    },
    { 9   , "HOT-B"                                     },
    { 10  , "Jaleco"                                    },
    { 11  , "Coconuts"                                  },
    { 12  , "Rage Software"                             },
    { 14  , "Technos"                                   },
    { 15  , "Mebio Software"                            },
    { 18  , "Gremlin Graphics"                          },
    { 19  , "Electronic Arts"                           },
    { 21  , "COBRA Team"                                },
    { 22  , "Human/Field"                               },
    { 23  , "KOEI"                                      },
    { 24  , "Hudson Soft"                               },
    { 26  , "Yanoman"                                   },
    { 28  , "Tecmo"                                     },
    { 30  , "Open System"                               },
    { 31  , "Virgin Games"                              },
    { 32  , "KSS"                                       },
    { 33  , "Sunsoft"                                   },
    { 34  , "POW"                                       },
    { 35  , "Micro World"                               },
    { 38  , "Enix"                                      },
    { 39  , "Loriciel/Electro Brain"                    },
    { 40  , "Kemco"                                     },
    { 41  , "Seta Co.,Ltd."                             },
    { 45  , "Visit Co.,Ltd."                            },
    { 49  , "Carrozzeria"                               },
    { 50  , "Dynamic"                                   },
    { 51  , "Nintendo"                                  },
    { 52  , "Magifact"                                  },
    { 53  , "Hect"                                      },
    { 60  , "Empire Software"                           },
    { 61  , "Loriciel"                                  },
    { 64  , "Seika Corp."                               },
    { 65  , "UBI Soft"                                  },
    { 70  , "System 3"                                  },
    { 71  , "Spectrum Holobyte"                         },
    { 73  , "Irem"                                      },
    { 75  , "Raya Systems/Sculptured Software"          },
    { 76  , "Renovation Products"                       },
    { 77  , "Malibu Games/Black Pearl"                  },
    { 79  , "U.S. Gold"                                 },
    { 80  , "Absolute Entertainment"                    },
    { 81  , "Acclaim"                                   },
    { 82  , "Activision"                                },
    { 83  , "American Sammy"                            },
    { 84  , "GameTek"                                   },
    { 85  , "Hi Tech Expressions"                       },
    { 86  , "LJN Toys"                                  },
    { 90  , "Mindscape"                                 },
    { 93  , "Tradewest"                                 },
    { 95  , "American Softworks Corp."                  },
    { 96  , "Titus"                                     },
    { 97  , "Virgin Interactive Entertainment"          },
    { 98  , "Maxis"                                     },
    {103  , "Ocean"                                     },
    {105  , "Electronic Arts"                           },
    {107  , "Laser Beam"                                },
    {110  , "Elite"                                     },
    {111  , "Electro Brain"                             },
    {112  , "Infogrames"                                },
    {113  , "Interplay"                                 },
    {114  , "LucasArts"                                 },
    {115  , "Parker Brothers"                           },
    {117  , "STORM"                                     },
    {120  , "THQ Software"                              },
    {121  , "Accolade Inc."                             },
    {122  , "Triffix Entertainment"                     },
    {124  , "Microprose"                                },
    {127  , "Kemco"                                     },
    {128  , "Misawa"                                    },
    {129  , "Teichio"                                   },
    {130  , "Namco Ltd."                                },
    {131  , "Lozc"                                      },
    {132  , "Koei"                                      },
    {134  , "Tokuma Shoten Intermedia"                  },
    {136  , "DATAM-Polystar"                            },
    {139  , "Bullet-Proof Software"                     },
    {140  , "Vic Tokai"                                 },
    {142  , "Character Soft"                            },
    {143  , "I''Max"                                    },
    {144  , "Takara"                                    },
    {145  , "CHUN Soft"                                 },
    {146  , "Video System Co., Ltd."                    },
    {147  , "BEC"                                       },
    {149  , "Varie"                                     },
    {151  , "Kaneco"                                    },
    {153  , "Pack in Video"                             },
    {154  , "Nichibutsu"                                },
    {155  , "TECMO"                                     },
    {156  , "Imagineer Co."                             },
    {160  , "Telenet"                                   },
    {164  , "Konami"                                    },
    {165  , "K.Amusement Leasing Co."                   },
    {167  , "Takara"                                    },
    {169  , "Technos Jap."                              },
    {170  , "JVC"                                       },
    {172  , "Toei Animation"                            },
    {173  , "Toho"                                      },
    {175  , "Namco Ltd."                                },
    {177  , "ASCII Co. Activison"                       },
    {178  , "BanDai America"                            },
    {180  , "Enix"                                      },
    {182  , "Halken"                                    },
    {186  , "Culture Brain"                             },
    {187  , "Sunsoft"                                   },
    {188  , "Toshiba EMI"                               },
    {189  , "Sony Imagesoft"                            },
    {191  , "Sammy"                                     },
    {192  , "Taito"                                     },
    {194  , "Kemco"                                     },
    {195  , "Square"                                    },
    {196  , "Tokuma Soft"                               },
    {197  , "Data East"                                 },
    {198  , "Tonkin House"                              },
    {200  , "KOEI"                                      },
    {202  , "Konami USA"                                },
    {203  , "NTVIC"                                     },
    {205  , "Meldac"                                    },
    {206  , "Pony Canyon"                               },
    {207  , "Sotsu Agency/Sunrise"                      },
    {208  , "Disco/Taito"                               },
    {209  , "Sofel"                                     },
    {210  , "Quest Corp."                               },
    {211  , "Sigma"                                     },
    {214  , "Naxat"                                     },
    {216  , "Capcom Co., Ltd."                          },
    {217  , "Banpresto"                                 },
    {218  , "Tomy"                                      },
    {219  , "Acclaim"                                   },
    {221  , "NCS"                                       },
    {222  , "Human Entertainment"                       },
    {223  , "Altron"                                    },
    {224  , "Jaleco"                                    },
    {226  , "Yutaka"                                    },
    {228  , "T&ESoft"                                   },
    {229  , "EPOCH Co.,Ltd."                            },
    {231  , "Athena"                                    },
    {232  , "Asmik"                                     },
    {233  , "Natsume"                                   },
    {234  , "King Records"                              },
    {235  , "Atlus"                                     },
    {236  , "Sony Music Entertainment"                  },
    {238  , "IGS"                                       },
    {241  , "Motown Software"                           },
    {242  , "Left Field Entertainment"                  },
    {243  , "Beam Software"                             },
    {244  , "Tec Magik"                                 },
    {249  , "Cybersoft"                                 },
    {255  , "Hudson Soft"                               },
	{0  , NULL                               }
};



//
//
//

                                                        

static SNRomCountryT *_SNRomGetCountry(Uint8 uCode)
{
    if (uCode < sizeof(_SNRom_Country) / sizeof(_SNRom_Country[0]))
    {
        return &_SNRom_Country[uCode];
    }   
    else
    {
        // invalid country code
        return NULL;
    }
}

static SNRomLicenseT *_SNRomGetLicense(Uint8 uCode)
{
    SNRomLicenseT *pLicense = _SNRom_License;

    while (pLicense->pName)
    {
        if (pLicense->uCode == uCode) 
        {
            // found license
            return pLicense;
        }

        pLicense++;
    }

    // invalid license
    return NULL;
}


SNRomHdrTypeE SnesRom::SNRomGetHdrType(SNRomHdrU *pRomHdr)
{
	// check SWC tag
	if (pRomHdr->SWC.Tag[0] == 0xAA && pRomHdr->SWC.Tag[1] == 0xBB && pRomHdr->SWC.Tag[2] == 0x04)
	{
		return SNROM_HDRTYPE_SWC;
	}

	// ???

	return SNROM_HDRTYPE_UNKNOWN;
}

//
//
//

static Bool _SNRomIsValidCartInfo(SNRomInfoT *pCartInfo)
{
	return pCartInfo && ((pCartInfo->InverseChecksum ^ pCartInfo->Checksum) == 0xFFFF);
}

//
//
//


SnesRom::SnesRom()
{
	m_bLoaded	= false;
	m_pRomMem	= NULL;
	m_pRomData	= NULL;
	m_pCartInfo = NULL;
	m_uRomBytes	= 0;
}

SnesRom::~SnesRom()
{
	Unload();
}

SNRomInfoT *SnesRom::GetCartInfo(Uint32 uOffset)
{
	if (m_pRomData)
	{
		// make sure offset doest go past end of rom data
		if ((uOffset + sizeof(SNRomInfoT)) <= m_uRomBytes)
		{
			// return cartinfo at offset
			return (SNRomInfoT *)(m_pRomData + uOffset);
		}
	} 
	return NULL;
}

void SnesRom::SetCartInfo(SNRomInfoT *pCartInfo)
{

	m_pCartInfo = pCartInfo;
	if (pCartInfo)
	{
		SNRomLicenseT* pLicense __attribute__((unused));
		SNRomCountryT* pCountry;

		pCountry = _SNRomGetCountry(pCartInfo->Country);
		pLicense = _SNRomGetLicense(pCartInfo->License);

		m_eVideoType = pCountry ? pCountry->eVideoType : SNROM_VIDEO_NTSC;
		m_uROMSize	  = 1 << (pCartInfo->RomSize - 7);
		switch (pCartInfo->SRAMSize)
		{
		default:
		case 0:
			m_uSRAMSize = 0;
			break;
		case 1:
			m_uSRAMSize = 16;
			break;
		case 2:
			m_uSRAMSize = 32;
			break;
		case 3:
			m_uSRAMSize = 64;
			break;
		}
#if SNDBG_LOG
		DLog("[snes-dsp] ROM '%.16s' makeup=%02X type=%02X size=%02X sram=%02X",
			(const char*)pCartInfo->Title,
			pCartInfo->RomMakeup, pCartInfo->RomType, pCartInfo->RomSize, pCartInfo->SRAMSize);
#endif
		switch (pCartInfo->RomType)
		{
		case 0:
		case 53:
			m_Flags		 = SNROM_FLAG_ROM;
			break;
		case 1:
			m_Flags		 = SNROM_FLAG_ROM | SNROM_FLAG_RAM;
			break;
		case 2:
			m_Flags		 = SNROM_FLAG_ROM | SNROM_FLAG_SAVERAM;
			break;
		case 3:
			m_Flags		 = SNROM_FLAG_ROM | SNROM_FLAG_DSP1;
			break;
		case 4:
			m_Flags		 = SNROM_FLAG_ROM | SNROM_FLAG_RAM | SNROM_FLAG_DSP1;
			break;
		case 5:
			m_Flags		 = SNROM_FLAG_ROM | SNROM_FLAG_SAVERAM | SNROM_FLAG_DSP1;
			break;
		case 19:
			m_Flags		 = SNROM_FLAG_ROM | SNROM_FLAG_SUPERFX;
			break;
		case 227:
			m_Flags		 = SNROM_FLAG_ROM | SNROM_FLAG_RAM | SNROM_FLAG_GAMEBOY;
			break;
		case 246:
			m_Flags		 = SNROM_FLAG_ROM | SNROM_FLAG_DSP2;
			break;
		}

		// O byte RomType nao distingue a variante do DSP (1/2/3/4): todos
		// os jogos de DSP reportam 0x03/0x04/0x05. O Dungeon Master e' o
		// UNICO jogo DSP-2, entao detecta-se pelo titulo do cabecalho e
		// troca o flag de DSP-1 para DSP-2.
		if (m_Flags & SNROM_FLAG_DSP1)
		{
			char t[8];
			int k;
			for (k = 0; k < 7; k++)
			{
				char c = (char)pCartInfo->Title[k];
				if (c >= 'a' && c <= 'z') c -= 32;   // upper
				t[k] = c;
			}
			t[7] = 0;
			if (!strncmp(t, "DUNGEON", 7))
			{
				m_Flags &= ~SNROM_FLAG_DSP1;
				m_Flags |=  SNROM_FLAG_DSP2;
			}
		}

		// OBC1 (Metal Combat: Falcon's Revenge): o cartucho reporta
		// RomType 0x13, que cairia no case de SuperFX. E' o unico jogo
		// OBC1 -> detecta pelo titulo e corrige o flag.
		{
			char t[13];
			int k;
			for (k = 0; k < 12; k++)
			{
				char c = (char)pCartInfo->Title[k];
				if (c >= 'a' && c <= 'z') c -= 32;
				t[k] = c;
			}
			t[12] = 0;
			if (!strncmp(t, "METAL COMBAT", 12))
			{
				m_Flags &= ~SNROM_FLAG_SUPERFX;
				m_Flags |=  SNROM_FLAG_OBC1;
			}
		}
	} else
	{
		m_eVideoType = SNROM_VIDEO_NTSC;
		m_Flags		 = SNROM_FLAG_ROM;
		m_uROMSize   = 0;
		m_uSRAMSize  = 0;
	}
}

Uint32	SnesRom::GetNumRomRegions()
{
	return 1;
}

char *SnesRom::GetRomRegionName(Uint32 eRegion)
{
	switch (eRegion)
	{
		case 0:
			return (char *)"ROM";
		default:
			return NULL;
	}

}
Uint32 	SnesRom::GetRomRegionSize(Uint32 eRegion)
{
	switch (eRegion)
	{
		case 0:
			return m_uRomBytes;
		default:
			return 0;
	}
}



char   *SnesRom::GetRomTitle()
{
    SNRomInfoT *pInfo;
    pInfo = m_pCartInfo;
	if (pInfo)
		return (char *)pInfo->Title;
	return NULL;
}


Emu::Rom::LoadErrorE SnesRom::LoadRom(CDataIO *pFileIO, Uint8 *pBuffer, Uint32 nBufferBytes)
{
	SNRomHdrU		RomHdr;
	SNRomHdrTypeE	eRomHdrType;
	size_t nBytesRead;
	Uint32 nFileBytes;
	Uint32 nHeaderBytes;
	Uint32 nRomBytes;

	// determine file size
	pFileIO->Seek(0, SEEK_END);
	nFileBytes= (Uint32)pFileIO->GetPos();
	pFileIO->Seek(0, SEEK_SET);
	if (nFileBytes <= 0)
	{
		return LOADERROR_BADHEADERSIZE;
	}

//	printf("%d file size\n", nFileBytes);

	// determine header size
	nHeaderBytes = (nFileBytes & 0x1FFF);
	nRomBytes    = nFileBytes - nHeaderBytes;

	// is header size valid?
	if (nHeaderBytes!=0 && nHeaderBytes!=sizeof(SNRomHdrU))
	{
		nHeaderBytes = 0;
//		return LOADERROR_BADHEADERSIZE;
	}

	m_eMapping = SNROM_MAPPING_LOROM;

	// does header exist?
	if (nHeaderBytes== sizeof(SNRomHdrU))
	{
		// header exists!

		// read rom header from file
		pFileIO->Read(&RomHdr, sizeof(RomHdr));

		// determine type of file (if possible)
		eRomHdrType = SNRomGetHdrType(&RomHdr);

		switch (eRomHdrType)
		{
		case SNROM_HDRTYPE_SWC:
			nRomBytes = RomHdr.SWC.uSize * 1024 * 1024 / 8 / 16;
			m_eMapping = (RomHdr.SWC.uImageInfo & 0x10)  ? SNROM_MAPPING_HIROM : SNROM_MAPPING_LOROM;
			break;
		default:
		case SNROM_HDRTYPE_UNKNOWN:
			m_eMapping = SNROM_MAPPING_LOROM;
			break;
		}
	} 

	// set size of ROM
	m_uRomBytes = nRomBytes;
	if (m_uRomBytes == 0)
	{
		return LOADERROR_BADROMSIZE;
	}

	// try to get data pointer from file
	m_pRomMem = NULL;
	m_pRomData = pFileIO->ReadPtr(m_uRomBytes);

	if (m_pRomData == NULL)
	{
		if (pBuffer)
		{
			if (m_uRomBytes < nBufferBytes)
			{	// use provided buffer space
				m_pRomMem = NULL;
				m_pRomData = pBuffer;
			} else
			{
				// not enough buffer space provided
				return LOADERROR_OUTOFSPACE;
			}
		} else
		{

			// allocate memory for rom
			m_pRomMem = 
			m_pRomData = (Uint8 *)malloc(m_uRomBytes);
			if (!m_pRomData)
			{
				return LOADERROR_OUTOFSPACE;
			}
		}

		// read rom data
		nBytesRead = pFileIO->Read(m_pRomData, m_uRomBytes);
		if (nBytesRead != m_uRomBytes)
		{
			Unload();
			return LOADERROR_READFILE;
		}
	}

	SNRomInfoT *pCartInfo;	

	// get cart info for rom
	pCartInfo = GetCartInfo(32704);
	if (_SNRomIsValidCartInfo(pCartInfo))
	{
		// cart mapping found in lo-rom
		m_eMapping = SNROM_MAPPING_LOROM;
	} else
	{
		// try to get cart info for hi-rom
		pCartInfo = GetCartInfo(65472);
		if (_SNRomIsValidCartInfo(pCartInfo))
		{
			// cart mapping found in hi-rom
			m_eMapping = SNROM_MAPPING_HIROM;
		} else
		{
			// cart info not found
			pCartInfo = NULL;
		}
	}

	SetCartInfo(pCartInfo);

	// ---- ExLoROM (Jumbo LoROM): LoROM maior que 4MB ----
	// Hacks grandes (ex.: SMW expandida pelo Lunar Magic) usam ExLoROM:
	// a metade de cima dos bancos ($80-$FF) deixa de ser espelho e passa a
	// conter dados extras, chegando a 8MB. Seguimos o snes9x
	// (Map_JumboLoROMMap): a ROM e' normalizada para que a metade que tem o
	// header/vetores fique em offset 0x400000 (mapeada em $00-$3F, de onde a
	// CPU le os vetores) e os outros 4MB em offset 0 ($80-$FF).
	if (m_eMapping == SNROM_MAPPING_LOROM && m_uRomBytes > 0x400000)
	{
		int score0  = _ExLoRomHeaderScore(m_pRomData, 0x007FC0, m_uRomBytes);
		int score4M = _ExLoRomHeaderScore(m_pRomData, 0x407FC0, m_uRomBytes);

		// header na frente do arquivo -> trocar as metades para coloca-lo
		// em 0x400000 (caso "SMALLFIRST" do snes9x).
		if (score0 > score4M)
		{
			Uint32 smallBytes = m_uRomBytes - 0x400000;
			Uint8 *pTmp = (Uint8 *)malloc(smallBytes);
			if (pTmp)
			{
				memcpy (pTmp, m_pRomData, smallBytes);                     // metade da frente (com header)
				memmove(m_pRomData, m_pRomData + smallBytes, 0x400000);    // 4MB de tras -> frente
				memcpy (m_pRomData + 0x400000, pTmp, smallBytes);          // header -> 0x400000
				free(pTmp);
			}
		}

		m_eMapping = SNROM_MAPPING_EXLOROM;
	}

	m_bLoaded   = true;
	return LOADERROR_NONE;
}

void SnesRom::Unload()
{
	if (m_pRomMem)
	{
		free(m_pRomMem);
		m_pRomMem = NULL;
	}

	m_pCartInfo = NULL;
	m_pRomData = NULL;
	m_uRomBytes = 0;
	m_bLoaded   = false;
}



Uint32 SnesRom::GetNumExts()
{
	/* The original iaddis SNESticle only registered .smc and .fig (the
	   two SNES ROM extensions that were common when it was written).
	   Modern dumps almost always come as .sfc (Super Famicom) and the
	   older Super Wild Card dumps use .swc; without these the browser
	   silently classifies those files as BROWSER_ENTRYTYPE_OTHER and
	   refuses to launch them. List all four flavours so the launcher
	   recognises the ROMs people actually have. */
	return 4;
}

char *SnesRom::GetExtName(Uint32 uExt)
{
	switch (uExt)
	{
		case 0:
			return (char *)"smc";
		case 1:
			return (char *)"sfc";
		case 2:
			return (char *)"swc";
		case 3:
			return (char *)"fig";
		default:
			return NULL;
	}
}

/* virtual */
char   *SnesRom::GetMapperName()
{
	return NULL;
}