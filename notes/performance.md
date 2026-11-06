# TLB performance

Plan, review findings and recommendations: [tlb-plan.md](tlb-plan.md).

## Status

- Done: the concrete TLBs in `src/hart/tlb.h` (`NullTLB`, `DirectMappedTLB`, `SetAssocTLB`,
  `FullyAssocTLB`). Integration fixes in `hart.cpp`/`op_system.cpp`: superpage PA, MPRV privilege
  in `sv32_perm_check`, the D bit (the hit path checked bit 2 = W instead of bit 7 = D), the G bit,
  superpages split into 4 KiB entries, insert replaces existing entries, and the sfence ASID mask.
- Done: options `--tlb=off|null|dm|sa|fa --tlb-entries=N --tlb-ways=W --max-instr=N`. SIGUSR1 stops
  the run and prints instructions, CPU time, MIPS and the I/D hit rates to stderr. The default is
  now `dm`, 64 entries (see results).
- Done: `-DTLB_STATIC=<FinalClass>` compile switch (devirtualized TLB).
- Done: benchmark driver [bench_tlb.py](bench_tlb.py). It boots `images/s-mode` on a pty, types a
  userspace workload, waits for the marker, then sends SIGUSR1.
- Done: the full matrix (3 runs each), the devirtualized variants and the M-mode reference (2026-11-06).
  The default is now `dm`, 64 entries (it was `sa` 64x4).

## How to run

```
python3 notes/bench_tlb.py --reps 3 --out tlb_results.csv            # full matrix incl. mmode, ~25 min
python3 notes/bench_tlb.py --only off sa64x4 fa64 --reps 1            # subset
# devirtualized:
cmake -S . -B build_static_DirectMappedTLB -DCMAKE_BUILD_TYPE=Release \
  "-DCMAKE_CXX_FLAGS=-ffunction-sections -fdata-sections -flto -DTLB_STATIC=DirectMappedTLB"
cmake --build build_static_DirectMappedTLB -j
python3 notes/bench_tlb.py --rvsim build_static_DirectMappedTLB/rvsim --label static_ --only dm64 dm256
```

## Results

Host: 12 threads, WSL2, GCC 16.2, Release + LTO, runs one at a time on an idle machine. Workload
(S-mode, `images/s-mode`): boot, then a shell loop of 20000 iterations, 150 runs of `ls /`, and
8 MiB through `dd | md5sum`. That is about 690–710M guest instructions. MIPS = instructions /
process CPU time, the median of 3 runs. Run-to-run spread is about ±2%.

| config | MIPS | vs off | I-TLB hit | D-TLB hit |
|---|---|---|---|---|
| off (no lookup) | 28.92 | 1.00x | – | – |
| null (virtual call, always misses) | 26.07 | 0.90x | 0 | 0 |
| dm 16 | 47.66 | 1.65x | 0.9954 | 0.9187 |
| **dm 64** | **48.52** | **1.68x** | 0.9991 | 0.9966 |
| dm 256 | 48.90 | 1.69x | 0.9998 | 0.9976 |
| dm 1024 | 48.27 | 1.67x | 0.9999 | 0.9997 |
| sa 16 × 4 | 42.40 | 1.47x | 0.9955 | 0.9934 |
| sa 64 × 2 | 46.84 | 1.62x | 0.9994 | 0.9990 |
| sa 64 × 4 | 44.35 | 1.53x | 0.9995 | 0.9991 |
| sa 64 × 8 | 41.99 | 1.45x | 0.9995 | 0.9993 |
| sa 256 × 4 | 44.75 | 1.55x | 0.9999 | 0.9998 |
| sa 1024 × 4 | 45.66 | 1.58x | 1.0000 | 0.9998 |
| fa 8 | 44.86 | 1.55x | 0.9924 | 0.9850 |
| fa 16 | 44.53 | 1.54x | 0.9958 | 0.9947 |
| fa 32 | 44.02 | 1.52x | 0.9979 | 0.9983 |
| fa 64 | 43.43 | 1.50x | 0.9995 | 0.9994 |
| fa 128 | 37.14 | 1.28x | 0.9998 | 0.9996 |
| **static dm 64** (`-DTLB_STATIC=DirectMappedTLB`) | **56.83** | **1.96x** | 0.9991 | 0.9969 |
| static dm 256 | 53.61 | 1.85x | 0.9998 | 0.9987 |
| static sa 64 × 4 (`-DTLB_STATIC=SetAssocTLB`) | 50.00 | 1.73x | 0.9995 | 0.9992 |
| static sa 256 × 4 | 50.84 | 1.76x | 0.9999 | 0.9997 |
| M-mode reference (NOMMU kernel, no translation) | 67.80 | (2.34x) | – | – |

The M-mode run uses a different kernel and rootfs (`images/m-mode`, booted with `rdinit=/bin/sh`)
and about 610M instructions, so it is only an upper bound on what removing translation could give.

Raw data, including every individual run: [results/tlb_results_2026-11-06.csv](results/tlb_results_2026-11-06.csv).

No run showed an oops, panic, segfault or illegal instruction.

### Conclusions

- Any TLB gives a 1.5–1.7x speedup. Above about 99% hits, **the cost of a lookup matters more than
  the hit rate**. dm16 misses 8% of data accesses and still beats every SA and FA config.
  Speed falls as ways are added (sa64: x2 46.8 > x4 44.4 > x8 42.0), and FA falls with size
  (fa128 = 37.1, because its MRU shortcut misses and the linear search takes over).
- Size is not the problem: DM gains nothing beyond 64 entries. The working set of this workload is
  small. Bigger multi-process workloads might favor 256, which costs the same speed.
- **Default changed to `dm`, 64 entries.** It's the fastest dynamic config and the simplest.
- Devirtualizing is worth about +17% for DM (48.5 → 56.8) and +13% for SA. The virtual call plus
  the `std::optional` return is a real cost on a path that runs twice per instruction. The best
  measured config is static dm64: 1.96x over no TLB, 84% of the M-mode ceiling. A follow-up should
  make the TLB concrete in the normal build, or keep the interface only for experiments. The
  softmmu-style host-pointer TLB in the recommendations goes further in the same direction.
- static dm256 has one run at 57.0 and two around 53. It is probably noise, plus some
  host-cache cost from the larger array, so it is no reason to choose 256 over 64.
