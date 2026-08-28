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
	SNQueue queue;
	SNQueueElementT *pElement;
	int i;

	for (i = 0; i < SNQUEUE_SIZE; ++i)
		Check("initial fill", queue.Enqueue((Uint32)i, 0x2100, (Uint8)i), TRUE);
	Check("genuine full queue", queue.Enqueue(900, 0x2100, 0), FALSE);

	for (i = 0; i < 300; ++i)
	{
		pElement = queue.Dequeue();
		Check("dequeue before compact", pElement ? (int)pElement->uCycle : -1, i);
	}

	Check("enqueue compacts consumed prefix",
	      queue.Enqueue(999, 0x2122, 0x5A), TRUE);
	for (i = 300; i < SNQUEUE_SIZE; ++i)
	{
		pElement = queue.Dequeue();
		Check("FIFO survives compact", pElement ? (int)pElement->uCycle : -1, i);
	}
	pElement = queue.Dequeue();
	Check("new element remains last", pElement ? (int)pElement->uCycle : -1, 999);
	Check("queue empty after drain", queue.IsEmpty(), TRUE);

	queue.Enqueue(10, 0x2100, 0);
	Check("timed dequeue excludes equal cycle", queue.Dequeue(10) == NULL, TRUE);
	pElement = queue.Dequeue(11);
	Check("timed dequeue accepts later cycle", pElement ? (int)pElement->uCycle : -1, 10);

	std::printf(g_Failures ? "FAIL (%d)\n" : "PASS\n", g_Failures);
	return g_Failures ? 1 : 0;
}
