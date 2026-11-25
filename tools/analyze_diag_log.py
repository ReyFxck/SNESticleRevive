#!/usr/bin/env python3
"""Summarize complete diagnostic windows (guest EE cycles, not wall FPS)."""
import argparse
import json
import re
import statistics as stats
from pathlib import Path

def analyze(path):
    text = Path(path).read_text(encoding="utf-8", errors="replace")
    fs = [tuple(map(int, x)) for x in re.findall(
        r"\[snes-frame\] f=(\d+).*?host-target=(\d+) budget=(\d+) cycles min/avg/max=(\d+)/(\d+)/(\d+) slow=(\d+)", text)]
    ps = [tuple(map(int, x)) for x in re.findall(
        r"\[ps2-frame\].*?host-hz=(\d+) display-frames=(\d+) count avg loop/prep/submit/flip=(\d+)/(\d+)/(\d+)/(\d+) work min/avg/max=(\d+)/(\d+)/(\d+)", text)]
    if not fs:
        raise ValueError(f"{path}: no complete snes-frame windows")
    sizes = {int(x) for x in re.findall(r"\[snes-diag\].*?window=(\d+)",text)}
    if len(sizes) != 1:
        raise ValueError(f"{path}: mixed or missing window sizes: {sizes}")
    win = sizes.pop()
    # use nominal guest EE frequency from budget * target
    clocks = {f[1]*f[2] for f in fs}
    if len(clocks) != 1:
        raise ValueError(f"{path}: mixed guest clocks: {clocks}")
    hz = clocks.pop()
    avg = stats.mean(f[4] for f in fs)
    cache = {}
    for name,pattern in {
        "bg_chr": r"\[snes-bg-chr-cache\].*?hit/miss/bypass=(\d+)/(\d+)/\d+",
        "bg_line": r"\[snes-bg-line-cache\] hit/miss/bypass=(\d+)/(\d+)/\d+",
        "obj": r"\[snes-obj-cache\] enabled=\d+ hit/miss=(\d+)/(\d+)"
    }.items():
        hits,misses=0,0
        for h,m in re.findall(pattern,text):
            hits+=int(h); misses+=int(m)
        cache[name]={"hits":hits,"misses":misses,
                     "hit_pct":round(100*hits/(hits+misses),2) if hits+misses else None}
    result = {
        "file":str(path), "windows":len(fs),"window_frames":win,
        "frames":len(fs)*win, "guest_clock_hz":hz,
        "core_avg_cycles":round(avg), "core_ms":round(avg*1000/hz,3),
        "core_capacity_fps":round(hz/avg,2),
        "worst_window_capacity_fps":round(hz/max(f[4] for f in fs),2),
        "core_worst_ms":round(max(f[5] for f in fs)*1000/hz,3),
        "windows_over_budget":sum(f[4]>f[2] for f in fs),
        "windows_with_over_budget_frame":sum(f[5]>f[2] for f in fs),
        "slow_frames_reported":sum(f[6] for f in fs),
        "cache":cache, "hostfs_denials":text.count("Denying access to path"),
        "ps2_windows":len(ps),
        "worst_windows": [{"end_frame":f[0],"avg_cycles":f[4],"max_cycles":f[5]}
                          for f in sorted(fs,key=lambda f:f[4],reverse=True)[:5]]
    }
    if ps:
        if len({p[1] for p in ps}) != 1:
            raise ValueError(f"{path}: mixed PS2 window sizes")
        loop=stats.mean(p[2] for p in ps)
        result.update(ps2_loop_cycles=round(loop),ps2_loop_est_fps=round(hz/loop,2),
                      ps2_work_ms=round(stats.mean(p[7] for p in ps)*1000/hz,3),
                      ps2_work_max_ms=round(max(p[8] for p in ps)*1000/hz,3),
                      ps2_prep_ms=round(stats.mean(p[3] for p in ps)*1000/hz,3),
                      ps2_submit_ms=round(stats.mean(p[4] for p in ps)*1000/hz,3),
                      ps2_flip_ms=round(stats.mean(p[5] for p in ps)*1000/hz,3))
    cadence = [tuple(map(int, x)) for x in re.findall(
        r"\[ps2-cadence\] session=(\d+) host-hz=(\d+) intervals=(\d+) avg/max/tail=(\d+)/(\d+)/(\d+) over105/150=(\d+)/(\d+)", text)]
    if cadence:
        count = sum(c[2] for c in cadence)
        avg_interval = sum(c[2] * c[3] for c in cadence) / count
        result.update(cadence_intervals=count,
                      cadence_guest_fps=round(hz/avg_interval, 2),
                      cadence_max_ms=round(max(c[4] for c in cadence)*1000/hz, 3),
                      cadence_tail_ms=round(sum(c[2]*c[5] for c in cadence)*1000/(count*hz), 3),
                      cadence_over105=sum(c[6] for c in cadence),
                      cadence_over150=sum(c[7] for c in cadence))
    epoch = [int(x) for x in re.findall(r"\[snes-bg-line-epoch\] hits=(\d+)", text)]
    if epoch:
        result["bg_line_epoch_hits"] = sum(epoch)
    vr = [tuple(map(int,x)) for x in re.findall(
        r"\[ps2-vblank-irq\] session=(\d+) intervals=(\d+) before/during=(\d+)/(\d+) zero/multiple/invalid=(\d+)/(\d+)/(\d+)",text)]
    if vr:
        result["vblank_irq"] = {"intervals":sum(x[1] for x in vr),
            "before_flip":sum(x[2] for x in vr),"during_flip":sum(x[3] for x in vr),
            "zero":sum(x[4] for x in vr),"multiple":sum(x[5] for x in vr),
            "invalid":sum(x[6] for x in vr),
            "counter_observed":any(x[2] or x[3] for x in vr)}
    return result

if __name__ == "__main__":
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument("logs",nargs="+",type=Path)
    args=p.parse_args()
    print(json.dumps([analyze(path) for path in args.logs],indent=2))
