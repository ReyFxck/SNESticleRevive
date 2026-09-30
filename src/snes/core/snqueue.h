/*
 * Copyright (c) 1997-2004-2022 Icer Addis
 * Re-Worked By ReyFxck, Claude Aí, ChatGPT
 *
 * Description:
 *   Declares the snqueue interface for the SNES emulation core.
 */

#ifndef _SNQUEUE_H
#define _SNQUEUE_H

#define SNQUEUE_SIZE (512)
#define SNPPU_QUEUE_SIZE (4096)

struct SNQueueElementT
{
	Uint32	uCycle;
	Uint16	uAddr;
	Uint8	uData;
	Uint8	uPad;
};

template <Int32 t_nSize>
class SNQueueT
{
public:
    SNQueueT()
    {
        Reset();
    }

	inline void Reset()
	{
		m_iHead = m_iTail = m_nCount = 0;
	}

	inline Bool IsEmpty()
	{
		return m_nCount == 0;
	}

	inline Bool Enqueue(Uint32 uCycle, Uint32 uAddr, Uint8 uData)
	{
		if (m_nCount < t_nSize)
		{
			SNQueueElementT *pElement = &m_Elements[m_iTail];
			if (++m_iTail == t_nSize)
				m_iTail = 0;
			m_nCount++;

			// enqueue write
			pElement->uCycle = uCycle;
			pElement->uAddr  = uAddr;
			pElement->uData = uData;
			pElement->uPad = 0;
			return TRUE;
		} else
		{
			// write cannot be enqueued, buffer full
			return FALSE;
		}
	}

	inline const SNQueueElementT *Peek() const
	{
		return m_nCount > 0 ? &m_Elements[m_iHead] : NULL;
	}

	inline Int32 CopyPending(SNQueueElementT *pOut, Int32 nMax) const
	{
		Int32 nCopy = m_nCount < nMax ? m_nCount : nMax;
		Int32 iIndex = m_iHead;
		for (Int32 i = 0; i < nCopy; ++i)
		{
			pOut[i] = m_Elements[iIndex];
			if (++iIndex == t_nSize) iIndex = 0;
		}
		return nCopy;
	}

	inline void RestorePending(const SNQueueElementT *pIn, Int32 nCount)
	{
		Reset();
		if (nCount < 0) nCount = 0;
		if (nCount > t_nSize) nCount = t_nSize;
		for (Int32 i = 0; i < nCount; ++i)
			Enqueue(pIn[i].uCycle, pIn[i].uAddr, pIn[i].uData);
	}

	inline SNQueueElementT	*Dequeue(Uint32 uCycle)
	{
		// dequeue element only if it is earlier than cycle time given
		if (m_nCount > 0 && (uCycle > m_Elements[m_iHead].uCycle))
		{
			SNQueueElementT *pElement = &m_Elements[m_iHead];
			if (++m_iHead == t_nSize)
				m_iHead = 0;
			m_nCount--;
			return pElement;
		}
		return NULL;
	}

	inline SNQueueElementT *DequeueAtOrBefore(Uint32 uCycle)
	{
		if (m_nCount > 0 && (uCycle >= m_Elements[m_iHead].uCycle))
		{
			SNQueueElementT *pElement = &m_Elements[m_iHead];
			if (++m_iHead == t_nSize)
				m_iHead = 0;
			m_nCount--;
			return pElement;
		}
		return NULL;
	}

	inline SNQueueElementT	*Dequeue()
	{
		if (m_nCount > 0)
		{
			SNQueueElementT *pElement = &m_Elements[m_iHead];
			if (++m_iHead == t_nSize)
				m_iHead = 0;
			m_nCount--;
			return pElement;
		}
		return NULL;
	}

private:
    Int32			m_iHead;	// current read position within write queue
    Int32			m_iTail;	// current write position within write queue
	Int32           m_nCount;
	SNQueueElementT	m_Elements[t_nSize];

};

/* SPC queues retain the original footprint.  Raster-heavy games can issue
   more than two thousand PPU writes in one frame, so the PPU gets a separate
   ring large enough to avoid forced mid-frame flushes. */
typedef SNQueueT<SNQUEUE_SIZE> SNQueue;
typedef SNQueueT<SNPPU_QUEUE_SIZE> SNPPUQueue;

#endif
