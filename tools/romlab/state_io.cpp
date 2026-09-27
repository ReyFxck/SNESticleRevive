#include "state_io.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <vector>

#include <zlib.h>

namespace {

static const uint8_t kStateMagic[8] =
    { 'S', 'N', 'R', 'S', 'T', 'A', 'T', 'E' };

enum
{
    STATE_FORMAT_VERSION = 1,
    STATE_PAYLOAD_RAW = 0,
    STATE_PAYLOAD_DEFLATE = 1,
    STATE_SYSTEM_SNES = 0
};

struct StateFileHeader
{
    uint8_t Magic[8];
    uint32_t Version;
    uint32_t HeaderBytes;
    uint32_t PayloadBytes;
    uint32_t PayloadCRC;
    uint32_t RomCRC;
    uint32_t RomBytes;
    uint32_t RomFlags;
    uint32_t Slot;
    uint32_t Generation;
    uint32_t Reserved[5];
};

static_assert(sizeof(StateFileHeader) == 64,
              "SNESticle state header must remain 64 bytes");

static bool ReadFile(const std::string &path, std::vector<uint8_t> *data,
                     std::string *error)
{
    FILE *file = fopen(path.c_str(), "rb");
    if (!file)
    {
        if (error) *error = "cannot open '" + path + "': " + strerror(errno);
        return false;
    }
    if (fseek(file, 0, SEEK_END) || ftell(file) < 0)
    {
        if (error) *error = "cannot determine file size: " + path;
        fclose(file);
        return false;
    }
    long size = ftell(file);
    rewind(file);
    data->resize((size_t)size);
    bool ok = !size || fread(data->data(), 1, (size_t)size, file) == (size_t)size;
    fclose(file);
    if (!ok)
    {
        if (error) *error = "short read from: " + path;
        return false;
    }
    return true;
}

} // namespace

namespace RomLabStateIO {

uint32_t Hash32(const void *data, size_t bytes)
{
    return (uint32_t)crc32(0L, (const Bytef *)data, (uInt)bytes);
}

RomIdentity Identify(const SnesRom &rom)
{
    RomIdentity identity;
    SnesRom &mutableRom = const_cast<SnesRom &>(rom);
    identity.Bytes = mutableRom.GetBytes();
    identity.Flags = mutableRom.m_Flags;
    identity.CRC32 = Hash32(mutableRom.GetData(), identity.Bytes);
    return identity;
}

bool LoadState(const std::string &path, const RomIdentity &rom,
               bool forceRomMismatch, SnesStateT *state,
               std::string *error)
{
    std::vector<uint8_t> file;
    if (!ReadFile(path, &file, error))
        return false;

    if (file.size() == sizeof(*state))
    {
        memcpy(state, file.data(), sizeof(*state));
        if (memcmp(state->Tag, "SNS", 4))
        {
            if (error) *error = "raw state has no SNS tag";
            return false;
        }
        return true;
    }

    if (file.size() < sizeof(StateFileHeader))
    {
        if (error) *error = "file is too small to be a SNESticle state";
        return false;
    }

    StateFileHeader header;
    memcpy(&header, file.data(), sizeof(header));
    if (memcmp(header.Magic, kStateMagic, sizeof(kStateMagic)) ||
        header.Version != STATE_FORMAT_VERSION ||
        header.HeaderBytes != sizeof(header) ||
        header.Reserved[2] != STATE_SYSTEM_SNES)
    {
        if (error)
            *error = "unsupported state format (Mesen/bsnes states are not byte-compatible)";
        return false;
    }
    if ((uint64_t)header.HeaderBytes + header.PayloadBytes != file.size())
    {
        if (error) *error = "state payload length does not match its header";
        return false;
    }
    if (!forceRomMismatch &&
        (header.RomCRC != rom.CRC32 || header.RomBytes != rom.Bytes ||
         header.RomFlags != rom.Flags))
    {
        if (error)
        {
            char message[256];
            snprintf(message, sizeof(message),
                "state belongs to another ROM (state %08X/%u/%08X, ROM %08X/%u/%08X)",
                header.RomCRC, header.RomBytes, header.RomFlags,
                rom.CRC32, rom.Bytes, rom.Flags);
            *error = message;
        }
        return false;
    }

    const uint8_t *stored = file.data() + header.HeaderBytes;
    if (header.Reserved[0] == STATE_PAYLOAD_RAW)
    {
        if (header.PayloadBytes != sizeof(*state))
        {
            if (error) *error = "raw state ABI size differs from this ROM Lab build";
            return false;
        }
        memcpy(state, stored, sizeof(*state));
    }
    else if (header.Reserved[0] == STATE_PAYLOAD_DEFLATE)
    {
        if (header.Reserved[1] &&
            Hash32(stored, header.PayloadBytes) != header.Reserved[1])
        {
            if (error) *error = "compressed state CRC is invalid";
            return false;
        }
        uLongf outputBytes = sizeof(*state);
        int result = uncompress((Bytef *)state, &outputBytes,
                                (const Bytef *)stored,
                                (uLong)header.PayloadBytes);
        if (result != Z_OK || outputBytes != sizeof(*state))
        {
            if (error) *error = "cannot decompress state or state ABI size differs";
            return false;
        }
    }
    else
    {
        if (error) *error = "unknown state payload encoding";
        return false;
    }

    if (Hash32(state, sizeof(*state)) != header.PayloadCRC)
    {
        if (error) *error = "decoded state CRC is invalid";
        return false;
    }
    if (memcmp(state->Tag, "SNS", 4))
    {
        if (error) *error = "decoded state has no SNS tag";
        return false;
    }
    return true;
}

bool SaveRawState(const std::string &path, const SnesStateT &state,
                  std::string *error)
{
    FILE *file = fopen(path.c_str(), "wb");
    if (!file)
    {
        if (error) *error = "cannot create '" + path + "': " + strerror(errno);
        return false;
    }
    bool ok = fwrite(&state, 1, sizeof(state), file) == sizeof(state);
    if (fflush(file))
        ok = false;
    if (fclose(file))
        ok = false;
    if (!ok)
    {
        if (error) *error = "cannot write state: " + path;
        return false;
    }
    return true;
}

bool SaveStateFile(const std::string &path, const RomIdentity &rom,
                   const SnesStateT &state, uint32_t slot,
                   uint32_t generation, std::string *error)
{
    std::vector<uint8_t> compressed((size_t)compressBound(sizeof(state)));
    uLongf compressedBytes = (uLongf)compressed.size();
    const uint8_t *payload = (const uint8_t *)&state;
    size_t payloadBytes = sizeof(state);
    uint32_t encoding = STATE_PAYLOAD_RAW;
    uint32_t storedCRC = 0;

    if (compress2((Bytef *)compressed.data(), &compressedBytes,
                  (const Bytef *)&state, sizeof(state), Z_BEST_SPEED) == Z_OK &&
        compressedBytes < sizeof(state))
    {
        payload = compressed.data();
        payloadBytes = (size_t)compressedBytes;
        encoding = STATE_PAYLOAD_DEFLATE;
        storedCRC = Hash32(payload, payloadBytes);
    }

    StateFileHeader header;
    memset(&header, 0, sizeof(header));
    memcpy(header.Magic, kStateMagic, sizeof(header.Magic));
    header.Version = STATE_FORMAT_VERSION;
    header.HeaderBytes = sizeof(header);
    header.PayloadBytes = (uint32_t)payloadBytes;
    header.PayloadCRC = Hash32(&state, sizeof(state));
    header.RomCRC = rom.CRC32;
    header.RomBytes = rom.Bytes;
    header.RomFlags = rom.Flags;
    header.Slot = slot;
    header.Generation = generation ? generation : 1;
    header.Reserved[0] = encoding;
    header.Reserved[1] = storedCRC;
    header.Reserved[2] = STATE_SYSTEM_SNES;

    FILE *file = fopen(path.c_str(), "wb");
    if (!file)
    {
        if (error) *error = "cannot create '" + path + "': " + strerror(errno);
        return false;
    }
    bool ok = fwrite(&header, 1, sizeof(header), file) == sizeof(header) &&
              fwrite(payload, 1, payloadBytes, file) == payloadBytes;
    if (fflush(file)) ok = false;
    if (fclose(file)) ok = false;
    if (!ok)
    {
        if (error) *error = "cannot write state container: " + path;
        return false;
    }
    return true;
}

bool LoadSRAM(const std::string &path, Uint8 *destination,
              size_t destinationBytes, std::string *error)
{
    std::vector<uint8_t> data;
    if (!ReadFile(path, &data, error))
        return false;
    if (data.size() > destinationBytes)
    {
        if (error) *error = "SRAM file is larger than the cartridge SRAM";
        return false;
    }
    memset(destination, 0, destinationBytes);
    if (!data.empty())
        memcpy(destination, data.data(), data.size());
    return true;
}

} // namespace RomLabStateIO
