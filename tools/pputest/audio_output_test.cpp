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
	capturedLeft.assign(left, left + count);
	capturedRight.assign(right, right + count);
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
	std::puts("audio_output_test: PASS (all int16 values, both channels/queues)");
	return 0;
}
