/* Exercise the real output frontend, with a captured audio-device boundary. */
#include <cstdio>
#include <vector>
#include "types.h"
#include "audmixbuffer.h"
extern "C" {
#include "audio.h"
}

static std::vector<Int16> capturedLeft, capturedRight;
extern "C" int Aud_IsInitialized(void) { return 1; }
extern "C" void Aud_Enqueue(short *left, short *right, int count, int)
{
	capturedLeft.insert(capturedLeft.end(), left, left + count);
	capturedRight.insert(capturedRight.end(), right, right + count);
}
extern "C" void Aud_EnqueueAsync(short *left, short *right, int count)
{
	Aud_Enqueue(left, right, count, 0);
}

static bool CheckVolume(int volume, bool async)
{
	AudMixGameSetVolume(volume);
	const int effective = volume < 0 ? 0 : volume > 100 ? 100 : volume;
	AudMixBuffer output(48000, async ? TRUE : FALSE);
	Int16 left[800], right[800];
	for (Uint32 start = 0; start < 65536; start += 800)
	{
		const Uint32 count = 65536 - start < 800 ? 65536 - start : 800;
		capturedLeft.clear(); capturedRight.clear();
		for (Uint32 i = 0; i < count; i++)
		{
			left[i] = (Int16)((Int32)(start + i) - 32768);
			right[i] = (Int16)(32767 - (Int32)(start + i));
		}
		output.OutputSamplesStereo(left, right, count);
		output.Flush();
		if (capturedLeft.size() != count || capturedRight.size() != count)
			return false;
		for (Uint32 i = 0; i < count; i++)
			if (capturedLeft[i] != (Int32)left[i] * effective / 100 ||
			    capturedRight[i] != (Int32)right[i] * effective / 100)
				return false;
	}
	return AudMixGameGetVolume() == effective;
}

static Int16 RoundedCubic(Int32 value)
{
	value = (value >= 0 ? value + 40 : value - 40) / 81;
	return value > 32767 ? 32767 : value < -32768 ? -32768 : (Int16)value;
}

static std::vector<Int16> Reference(const std::vector<Int16> &source, int rate)
{
	if (rate != 32000) return source;
	std::vector<Int16> result;
	for (size_t i = 0; i + 3 < source.size(); i += 2)
	{
		Int32 prev = i ? source[i - 1] : 0;
		result.push_back(source[i]);
		result.push_back(RoundedCubic(-4 * prev + 30 * source[i] +
			60 * source[i + 1] - 5 * source[i + 2]));
		result.push_back(RoundedCubic(-5 * source[i] + 60 * source[i + 1] +
			30 * source[i + 2] - 4 * source[i + 3]));
	}
	return result;
}

static bool CheckChunks(int rate, bool async, int length, int chunk, bool flush)
{
	std::vector<Int16> left(length), right(length);
	Uint32 random = 0x918361;
	for (int i = 0; i < length; i++)
	{
		random = random * 1664525u + 1013904223u; left[i] = (Int16)(random >> 16);
		random = random * 1664525u + 1013904223u; right[i] = (Int16)(random >> 16);
	}
	AudMixGameSetVolume(100);
	AudMixBuffer output(rate, async ? TRUE : FALSE);
	/* Reset must also discard the previous resampler's lookahead. */
	output.OutputSamplesStereo(left.data(), right.data(), length > 3 ? 3 : length);
	output.Reset();
	capturedLeft.clear(); capturedRight.clear();
	for (int i = 0; i < length; i += chunk)
	{
		int count = length - i < chunk ? length - i : chunk;
		output.OutputSamplesStereo(left.data() + i, right.data() + i, count);
		if (flush) output.Flush();
	}
	output.Flush();
	return capturedLeft == Reference(left, rate) && capturedRight == Reference(right, rate);
}

int main()
{
	static const int volumes[] = { 100, 50, 0, 1, 99, -1, 101 };
	for (int volume : volumes)
		for (int async = 0; async < 2; async++)
			if (!CheckVolume(volume, async != 0))
			{
				std::printf("audio_output_test: FAIL volume=%d async=%d\n", volume, async);
				return 1;
			}
	for (int rate : { 32000, 48000 })
		for (int async = 0; async < 2; async++)
			for (int length : { 1, 2, 3, 4, 5, 17, 532, 3999, 4000, 4001, 8013, 16001 })
				for (int chunk : { 1, 2, 3, 7, 512, 532, 533, 4001, 16001 })
					for (int flush = 0; flush < 2; flush++)
						if (!CheckChunks(rate, async != 0, length, chunk, flush != 0))
						{
							std::printf("audio_output_test: FAIL rate=%d async=%d length=%d chunk=%d flush=%d\n",
								rate, async, length, chunk, flush);
							return 1;
						}
	std::puts("audio_output_test: PASS (int16 gain, odd/large buffers, chunk-independent cubic PCM)");
	return 0;
}
