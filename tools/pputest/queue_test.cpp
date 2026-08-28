#include <cstdio>

#include "types.h"
#include "snqueue.h"

static int g_Failures;

static void Check(const char *pName, int nGot, int nExpected)
{
	if (nGot != nExpected)
	{
		std::printf("FAIL %s: %d != %d\n", pName, nGot, nExpected);
		g_Failures++;
	}
}

int main()
{
	SNQueueT<8> queue;
	SNQueueElementT *pElement;
	int i;

	for (i = 0; i < 8; ++i)
		Check("initial fill", queue.Enqueue((Uint32)i, 0x2100, (Uint8)i), TRUE);
	Check("genuine full queue", queue.Enqueue(900, 0x2100, 0), FALSE);

	for (i = 0; i < 5; ++i)
	{
		pElement = queue.Dequeue();
		Check("dequeue before wrap", pElement ? (int)pElement->uCycle : -1, i);
	}

	for (i = 8; i < 13; ++i)
		Check("enqueue wraps into consumed prefix",
		      queue.Enqueue((Uint32)i, 0x2122, (Uint8)i), TRUE);
	Check("wrapped queue is full", queue.Enqueue(999, 0x2122, 0x5A), FALSE);
	for (i = 5; i < 13; ++i)
	{
		pElement = queue.Dequeue();
		Check("FIFO survives wrap", pElement ? (int)pElement->uCycle : -1, i);
	}
	Check("queue empty after drain", queue.IsEmpty(), TRUE);

	queue.Enqueue(10, 0x2100, 0);
	Check("timed dequeue excludes equal cycle", queue.Dequeue(10) == NULL, TRUE);
	pElement = queue.Dequeue(11);
	Check("timed dequeue accepts later cycle", pElement ? (int)pElement->uCycle : -1, 10);

	std::printf(g_Failures ? "FAIL (%d)\n" : "PASS\n", g_Failures);
	return g_Failures ? 1 : 0;
}
