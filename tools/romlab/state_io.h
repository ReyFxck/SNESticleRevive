#ifndef SNESTICLE_ROMLAB_STATE_IO_H
#define SNESTICLE_ROMLAB_STATE_IO_H

#include <stddef.h>
#include <stdint.h>
#include <string>

#include "types.h"
#include "snes.h"
#include "snstate.h"

namespace RomLabStateIO {

struct RomIdentity
{
    uint32_t CRC32;
    uint32_t Bytes;
    uint32_t Flags;
};

uint32_t Hash32(const void *data, size_t bytes);
RomIdentity Identify(const SnesRom &rom);

bool LoadState(const std::string &path, const RomIdentity &rom,
               bool forceRomMismatch, SnesStateT *state,
               std::string *error);
bool SaveRawState(const std::string &path, const SnesStateT &state,
                  std::string *error);
bool SaveStateFile(const std::string &path, const RomIdentity &rom,
                   const SnesStateT &state, uint32_t slot,
                   uint32_t generation, std::string *error);
bool LoadSRAM(const std::string &path, Uint8 *destination,
              size_t destinationBytes, std::string *error);

} // namespace RomLabStateIO

#endif
