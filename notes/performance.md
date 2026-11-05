# TLB performance (work in progress)

Plan, review findings and recommendations: [tlb-plan.md](tlb-plan.md).

## Status

- Done: the concrete TLBs in `src/hart/tlb.h` (`NullTLB`, `DirectMappedTLB`, `SetAssocTLB`,
  `FullyAssocTLB`). Integration fixes in `hart.cpp`/`op_system.cpp`: superpage PA, MPRV privilege
  in `sv32_perm_check`, the D bit (the hit path checked bit 2 = W instead of bit 7 = D), the G bit,
  superpages split into 4 KiB entries, insert replaces existing entries, and the sfence ASID mask.
- Done: options `--tlb=off|null|dm|sa|fa --tlb-entries=N --tlb-ways=W --max-instr=N`. SIGUSR1 stops
  the run and prints instructions, CPU time, MIPS and the I/D hit rates to stderr. The default is
  currently `sa`, 64 entries, 4 ways.
- Done: `-DTLB_STATIC=<FinalClass>` compile switch (devirtualized TLB).
- Done: benchmark driver [bench_tlb.py](bench_tlb.py). It boots `images/s-mode` on a pty, types a
  userspace workload, waits for the marker, then sends SIGUSR1.
- **TODO**: run the full matrix (it was interrupted after the baselines), the devirtualized
  variants and the M-mode reference. Then fill in the table and pick the default TLB.

## How to run

```
python3 notes/bench_tlb.py --reps 3 --out tlb_results.csv            # full matrix, ~25 min
python3 notes/bench_tlb.py --only off sa64x4 fa64 --reps 1            # subset
# devirtualized:
cmake -S . -B build_static -DCMAKE_BUILD_TYPE=Release \
  "-DCMAKE_CXX_FLAGS=-ffunction-sections -fdata-sections -flto -DTLB_STATIC=SetAssocTLB"
cmake --build build_static -j && python3 notes/bench_tlb.py --rvsim build_static/rvsim --label static_ --only sa64x4
```

## Results so far (partial)

Host: 12 threads, WSL2, GCC 16.2, Release + LTO. Workload: a shell loop of 20000 iterations,
150 runs of `ls /`, and 8 MiB through `dd | md5sum`. That is about 700M guest instructions,
including the boot. MIPS = instructions / process CPU time.

| config | runs | MIPS (median) | vs off | I-TLB hit | D-TLB hit |
|---|---|---|---|---|---|
| off (no lookup) | 3 | 28.30 | 1.00x | – | – |
| null (virtual call, always misses) | 3 | 26.09 | 0.92x | 0 | 0 |
| sa 64 × 4-way | 1 (probe) | 44.80 | 1.58x | 0.9995 | 0.9992 |
| fa 64 | 1 (probe) | 41.27 | 1.46x | 0.9995 | 0.9994 |

Notes so far: the virtual call plus the `optional` return alone costs about 8% (null vs off), so
a devirtualized TLB is worth measuring. FA 64 is slower than SA 64x4 at the same hit rate because
of the linear search on an MRU miss. None of the runs showed an oops, panic or segfault.
