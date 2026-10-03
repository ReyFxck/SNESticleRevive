/* Preserve complete mixer PCM and voice feedback across PMON changes.
   Golden hashes come from main 6c6eb4a, using synthetic BRR data only. */
#include <cstdio>
#include <cstdint>
#include <cstring>

#include "types.h"
#include "mixbuffer.h"
#include "snspcdsp.h"
#include "snspcmix.h"
#include "snspcdefs.h"

class Capture : public CMixBuffer
{
public:
	Uint32 rate;
	Int32 samples = 0;
	Uint32 produced = 0;
	uint64_t hash = UINT64_C(1469598103934665603);
	void Add(Uint16 value)
	{
		hash ^= value;
		hash *= UINT64_C(1099511628211);
	}
	void GetFormat(Uint32 *r, Uint32 *b, Uint32 *c) override
	{
		*r = rate; *b = 16; *c = 2;
	}
	Int32 GetOutputSamples() override { return samples; }
	void OutputSamplesStereo(Int16 *left, Int16 *right, Int32 count) override
	{
		produced += count;
		for (Int32 i = 0; i < count; i++)
		{
			Add((Uint16)left[i]);
			Add((Uint16)right[i]);
		}
	}
};

static uint64_t Exercise(Uint32 rate)
{
	Capture capture;
	capture.rate = rate;
	static const Int32 lengths[] = { 0, 1, 2, 67, 68, 69, 137, 533 };
	for (Uint32 mask = 0; mask < 256; mask++)
	{
		Uint8 ram[65536] = {};
		SNSpcDsp dsp{};
		SNSpcDspMixFull mixer{};
		dsp.SetMem(ram);
		dsp.SetMixer(0, &mixer);
		dsp.SetMixer(1, NULL);
		mixer.SetDsp(&dsp);
		dsp.Reset();
		mixer.Reset();
		dsp.Write8(SNSPCDSP_REG_DIR, 0x10);
		dsp.Write8(SNSPCDSP_REG_MVOLL, 91);
		dsp.Write8(SNSPCDSP_REG_MVOLR, (Uint8)-73);
		dsp.Write8(SNSPCDSP_REG_EVOLL, 47);
		dsp.Write8(SNSPCDSP_REG_EVOLR, (Uint8)-39);
		dsp.Write8(SNSPCDSP_REG_EON, (Uint8)(mask ^ 0xA5));
		dsp.Write8(SNSPCDSP_REG_EFB, 31);
		dsp.Write8(SNSPCDSP_REG_EDL, 1);
		dsp.Write8(SNSPCDSP_REG_ECHOFIR0, 127);
		dsp.Write8(SNSPCDSP_REG_FLG, 7);
		dsp.Write8(SNSPCDSP_REG_PMON, (Uint8)mask);
		dsp.Write8(SNSPCDSP_REG_NOV, (Uint8)(mask & 0x55));
		for (Uint32 voice = 0; voice < 8; voice++)
		{
			Uint32 address = 0x2000 + voice * 18;
			ram[0x1000 + voice * 4 + 0] = (Uint8)address;
			ram[0x1000 + voice * 4 + 1] = (Uint8)(address >> 8);
			ram[0x1000 + voice * 4 + 2] = (Uint8)address;
			ram[0x1000 + voice * 4 + 3] = (Uint8)(address >> 8);
			for (Uint32 block = 0; block < 2; block++)
			{
				ram[address + block * 9] = (Uint8)(0x80 |
					((voice & 3) << 2) | (block ? 3 : 0));
				for (Uint32 byte = 1; byte < 9; byte++)
					ram[address + block * 9 + byte] =
						(Uint8)(mask * 17 + voice * 31 + byte * 43);
			}
			dsp.Write8(voice * 16 + SNSPCDSP_REG_VOLL, (Uint8)(21 + voice * 11));
			dsp.Write8(voice * 16 + SNSPCDSP_REG_VOLR, (Uint8)(-97 + (int)voice * 13));
			dsp.Write8(voice * 16 + SNSPCDSP_REG_PITCHLO, (Uint8)(31 + voice * 17));
			dsp.Write8(voice * 16 + SNSPCDSP_REG_PITCHHI, (Uint8)(8 + voice * 7));
			dsp.Write8(voice * 16 + SNSPCDSP_REG_SRCN, (Uint8)voice);
			dsp.Write8(voice * 16 + SNSPCDSP_REG_GAIN, (Uint8)(64 + voice * 7));
		}
		/* Alternating active/silent predecessors catch stale PMON feedback. */
		dsp.Write8(SNSPCDSP_REG_KON, (Uint8)(mask | 0xAA));
		for (Uint32 step = 0; step < sizeof(lengths) / sizeof(lengths[0]); step++)
		{
			capture.samples = lengths[step];
			capture.produced = 0;
			if (step == 5) dsp.Write8(SNSPCDSP_REG_KOFF, 0x55);
			if (step == 6) dsp.Write8(SNSPCDSP_REG_KON, 0xFF);
			/* PMON changes inside a multi-block call must remain block-local. */
			dsp.EnqueueWrite(0, SNSPCDSP_REG_PMON, (Uint8)(mask ^ (step * 29)));
			dsp.EnqueueWrite(68 * 32 * SNSPC_CYCLE, SNSPCDSP_REG_PMON,
				(Uint8)(mask ^ 0xFE));
			mixer.Mix(&capture);
			if (capture.produced != (Uint32)capture.samples)
			{
				std::fprintf(stderr, "incorrect mixer sample count\n");
				return 0;
			}
			dsp.Sync();
			dsp.UpdateFlags(&mixer);
			for (Uint32 voice = 0; voice < 8; voice++)
			{
				capture.Add(dsp.Read8(voice * 16 + SNSPCDSP_REG_ENVX));
				capture.Add(dsp.Read8(voice * 16 + SNSPCDSP_REG_OUTX));
			}
			capture.Add(dsp.Read8(SNSPCDSP_REG_ENDX));
		}
	}
	return capture.hash;
}

int main()
{
	static const Uint32 rates[] = { 32000, 44100, 48000 };
	static const uint64_t expected[] = {
		UINT64_C(0x62FA954A37C83057), UINT64_C(0x0D19ED458CD96149),
		UINT64_C(0xCD9D6A629D9DBC95)
	};
	int failures = 0;
	for (Uint32 i = 0; i < 3; i++)
	{
		uint64_t result = Exercise(rates[i]);
		std::printf("mixer_feedback_test: rate=%u hash=%016llX\n",
			(unsigned)rates[i], (unsigned long long)result);
		if (result != expected[i]) failures++;
	}
	return failures ? 1 : 0;
}
