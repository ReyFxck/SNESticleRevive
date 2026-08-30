#!/usr/bin/env python3
# Copyright (c) 1997-2004-2022 Icer Addis
# Re-Worked By ReyFxck, Claude Aí, ChatGPT
#
# Description:
#   Tests the snesdiag-v1 text-log analyzer.

import unittest

from analyze import analyze_lines

class AnalyzeTest(unittest.TestCase):
    def test_prefixed_log_and_anomalies(self):
        report = analyze_lines(
            [
                "EE_SIO: [rom-map] final lo=5 hi=2 mapper=LoROM title='TEST GAME'\n",
                "EE_SIO: [snes-diag] schema=snesdiag-v1 level=2 window=120\n",
                "[snes-frame] f=120 host-target=60 slow=3 capacity=57.5 fps\n",
                "[snes-perf] cpu=40% ppu=61% gsu=0% apu=9% mix=8% mdma=2% hdma=3%\n",
                "[snes-raster] ppu queued/applied/full=9/9/2 hdma lines/active/xfer=0/0/0\n",
                "[snes-sync] ppu calls/lines=2/224 dma starts=4 read=0 wrap=1 max=544\n",
                "[snes-audio] samples=64000 avg/frame=533 mix calls/zero=120/1 min/max=533/534\n",
                "[snes-video] rendered/skipped=118/2\n",
                "[snes-sdd1] dma/bytes/remaps/source-fail=5/4096/2/1 seg=0/1/2/3\n",
                "[snes-gs-deep] mismatch stage/copy=1/0\n",
                "[snes-capture] schema=snesdiag-v1 begin f=121 reasons=82 manual/slow/queue/dma/obj/skip/audio/chip=0/1/0/0/0/0/0/1\n",
            ]
        )
        self.assertEqual(report["stats"]["windows"], 1)
        self.assertEqual(report["roms"], {"TEST GAME (LoROM)": 1})
        self.assertEqual(report["stats"]["slow_frames"], 3)
        self.assertEqual(report["stats"]["ppu_queue_full"], 2)
        self.assertEqual(report["stats"]["sdd1_source_fail"], 1)
        self.assertEqual(report["capture_reasons"], {"slow-frame": 1, "chip": 1})
        self.assertEqual(report["hotspots"][0]["name"], "ppu")
        self.assertTrue(any(item["severity"] == "ERROR" for item in report["findings"]))

    def test_missing_window_is_reported(self):
        report = analyze_lines(["ordinary emulator log\n"])
        self.assertEqual(report["findings"][0]["code"], "no-window")

if __name__ == "__main__":
    unittest.main()
