#!/usr/bin/env python3
"""Profile one bench_tlb.py workload run: attaches `perf record -g` to the rvsim process by pid
(bench_tlb.py signals the process it started, so perf can't be a wrapper).

usage: prof_bench.py RVSIM PERF_DATA     then: perf report -i PERF_DATA --no-children -g none --sort srcline
Build RVSIM with debug info, e.g.
  cmake -S . -B build_prof -DCMAKE_BUILD_TYPE=Release \
    "-DCMAKE_CXX_FLAGS=-ffunction-sections -fdata-sections -flto -g -fno-omit-frame-pointer"
"""
import importlib.util, os, subprocess, sys

here = os.path.dirname(os.path.abspath(__file__))
spec = importlib.util.spec_from_file_location("bench_tlb", os.path.join(here, "bench_tlb.py"))
bench = importlib.util.module_from_spec(spec)
spec.loader.exec_module(bench)

rvsim, out = sys.argv[1], sys.argv[2]
popen = subprocess.Popen
perf = []


class AttachPerf(popen):
    def __init__(self, *args, **kwargs):
        super().__init__(*args, **kwargs)
        perf.append(popen(["perf", "record", "-F", "1999", "-g", "-p", str(self.pid), "-o", out],
                          stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL))


bench.subprocess.Popen = AttachPerf
stats, _ = bench.run_once(rvsim, "dm", 64, 1)
perf[0].wait(timeout=60)
print(stats)
