#pragma once

#include "types.h"

/* Host-only recovery scheduler.  It observes completed GS flips and requests
   one video-only skip after a missed VBlank.  CPU, SPC, input and every PPU
   register/DMA still advance; the caller merely passes a null render surface.
   A mandatory rendered frame after every skip prevents consecutive drops. */
class MainLoopSafeFrameskipScheduler
{
public:
	MainLoopSafeFrameskipScheduler()
	{
		Reset();
	}

	void Reset()
	{
		m_bPending = FALSE;
		m_bMustRender = FALSE;
		m_bWasGameplay = FALSE;
		m_uLastFlip = 0;
		m_uPeriod = 0;
		m_uSampleCount = 0;
		m_uSamplePos = 0;
		m_uSamples[0] = m_uSamples[1] = m_uSamples[2] = 0;
	}

	void CancelRecovery()
	{
		m_bPending = FALSE;
		m_bMustRender = FALSE;
	}

	Bool Take()
	{
		if (m_bMustRender)
		{
			m_bMustRender = FALSE;
			return FALSE;
		}
		if (!m_bPending)
			return FALSE;

		m_bPending = FALSE;
		m_bMustRender = TRUE;
		return TRUE;
	}

	void AfterFlip(Uint32 uNow, Bool bGameplay)
	{
		if (m_uLastFlip != 0)
		{
			Uint32 uDelta = uNow - m_uLastFlip;
			if (uDelta != 0)
			{
				Bool bDiscontinuity =
					(m_uPeriod != 0 &&
					 (Uint64)uDelta > (Uint64)m_uPeriod * 4u)
					? TRUE : FALSE;

				if (bDiscontinuity)
				{
					CancelRecovery();
				}
				else
				{
					Learn(uDelta, bGameplay);

					if (bGameplay && m_bWasGameplay &&
					    m_uSampleCount >= 3u && m_uPeriod != 0 &&
					    (Uint64)uDelta * 1000u >
					    (Uint64)m_uPeriod * 1500u)
					{
						m_bPending = TRUE;
					}
					else if (bGameplay && m_bPending && m_uPeriod != 0 &&
					         (Uint64)uDelta * 1000u <=
					         (Uint64)m_uPeriod * 1500u)
					{
						m_bPending = FALSE;
					}
				}
			}
		}

		m_uLastFlip = uNow;
		m_bWasGameplay = bGameplay;
		if (!bGameplay)
			CancelRecovery();
	}

	Uint32 GetPeriod() const { return m_uPeriod; }

private:
	static Uint32 Median3(Uint32 a, Uint32 b, Uint32 c)
	{
		if (a > b) { Uint32 x = a; a = b; b = x; }
		if (b > c) { Uint32 x = b; b = c; c = x; }
		if (a > b) { Uint32 x = a; a = b; b = x; }
		return b;
	}

	void Learn(Uint32 uDelta, Bool bGameplay)
	{
		if (m_uPeriod != 0)
		{
			if (bGameplay)
			{
				/* Gameplay refines only healthy single-VBlank samples. */
				if ((Uint64)uDelta * 4u < (Uint64)m_uPeriod * 3u ||
				    (Uint64)uDelta * 4u > (Uint64)m_uPeriod * 5u)
					return;
			}
			else
			{
				/* Menus may follow PAL/NTSC drift but not half/double noise. */
				if ((Uint64)uDelta * 3u < (Uint64)m_uPeriod * 2u ||
				    (Uint64)uDelta * 2u > (Uint64)m_uPeriod * 3u)
					return;
			}
		}

		m_uSamples[m_uSamplePos] = uDelta;
		m_uSamplePos = (m_uSamplePos + 1u) % 3u;
		if (m_uSampleCount < 3u)
			m_uSampleCount++;

		if (m_uSampleCount == 1u)
			m_uPeriod = m_uSamples[0];
		else if (m_uSampleCount == 2u)
			m_uPeriod = (Uint32)(((Uint64)m_uSamples[0] +
			                            (Uint64)m_uSamples[1]) / 2u);
		else
			m_uPeriod = Median3(m_uSamples[0], m_uSamples[1],
			                    m_uSamples[2]);
	}

	Bool m_bPending;
	Bool m_bMustRender;
	Bool m_bWasGameplay;
	Uint32 m_uLastFlip;
	Uint32 m_uPeriod;
	Uint32 m_uSamples[3];
	Uint32 m_uSampleCount;
	Uint32 m_uSamplePos;
};

Bool MainLoopSafeFrameskipTake(Bool bAllowed);
void MainLoopSafeFrameskipAfterFlip(void);
