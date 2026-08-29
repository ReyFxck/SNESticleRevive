#pragma once

#include "types.h"

#ifndef SNESTICLE_MAX_CATCHUP_FRAMES
#define SNESTICLE_MAX_CATCHUP_FRAMES 3
#endif

/* Host-only recovery scheduler.  It observes completed GS flips and requests
   hidden catch-up frames after missed VBlanks.  The caller executes those
   complete emulated frames with a null render surface, then renders one visible
   frame before the next flip.  Unlike presenting the old texture for another
   VBlank, hidden frames do not wait for the GS, so emulated time can catch up. */
class MainLoopSafeFrameskipScheduler
{
public:
	MainLoopSafeFrameskipScheduler()
	{
		Reset();
	}

	void Reset()
	{
		m_bWasGameplay = FALSE;
		m_uPendingCatchup = 0;
		m_uLastFlip = 0;
		m_uPeriod = 0;
		m_uSampleCount = 0;
		m_uSamplePos = 0;
		m_uSamples[0] = m_uSamples[1] = m_uSamples[2] = 0;
	}

	void CancelRecovery()
	{
		m_uPendingCatchup = 0;
	}

	Uint32 TakeCatchupFrames()
	{
		Uint32 uFrames = m_uPendingCatchup;
		m_uPendingCatchup = 0;
		return uFrames;
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
					    (Uint64)uDelta * 2u >
					    (Uint64)m_uPeriod * 3u)
					{
						/* Completed flips are phase-locked to VBlank, so the
						   nearest period count tells us how many emulated frames
						   must run before the next visible one. */
						Uint32 uPeriods = (uDelta + m_uPeriod / 2u) / m_uPeriod;
						Uint32 uCatchup = uPeriods > 1u ? uPeriods - 1u : 0u;
						if (uCatchup > SNESTICLE_MAX_CATCHUP_FRAMES)
							uCatchup = SNESTICLE_MAX_CATCHUP_FRAMES;
						m_uPendingCatchup = uCatchup;
					}
					else if (bGameplay && m_uPeriod != 0 &&
					         (Uint64)uDelta * 2u <=
					         (Uint64)m_uPeriod * 3u)
					{
						m_uPendingCatchup = 0;
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

	Bool m_bWasGameplay;
	Uint32 m_uPendingCatchup;
	Uint32 m_uLastFlip;
	Uint32 m_uPeriod;
	Uint32 m_uSamples[3];
	Uint32 m_uSampleCount;
	Uint32 m_uSamplePos;
};

Uint32 MainLoopSafeFrameskipTake(Bool bAllowed);
void MainLoopSafeFrameskipAfterFlip(void);
Bool MainLoopSafeFrameskipIsEnabled(void);
void MainLoopSafeFrameskipSetEnabled(Bool bEnabled);
