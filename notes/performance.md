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
  The default is now `dm`, 64 entries (it was `sa` 64x4), and the default build is devirtualized
  (`RVSIM_TLB_STATIC=DirectMappedTLB`).

## RAM fast path (2026-11-06)

All hart bus accesses (fetch, load/store, AMO, PTE reads and A/D writes) now go through
`hart::bus_load`/`bus_store` ([hart.h](../src/hart/hart.h)). A physical address inside RAM
(`set_ram(base, size, host)`, set in `main.cpp`) is a fixed-size `memcpy` from the host buffer.
Everything else still goes through the `mmio` tree. That removes two virtual calls (`mmio` → `ram`),
the slot decode and the size checks from every RAM access.

| config | MIPS (median, 3 runs) | runs | vs off |
|---|---|---|---|
| dm 64, before | 48.52 | 48.32 48.52 48.97 | 1.68x |
| dm 64 + RAM fast path | 63.64 | 68.32 63.64 61.67 | 2.20x |
| **static dm 64 + RAM fast path (new default build)** | **73.30** | 75.85 73.30 71.05 | **2.53x** |

That's +31%, more than devirtualizing the TLB gave (+17%). The two gains add up: static dm64 with the
fast path gets +15% more. That is the default build now: `RVSIM_TLB_STATIC` in `CMakeLists.txt`
defaults to `DirectMappedTLB`, and a `--tlb` that doesn't match the compiled-in class is an error. The M-mode
reference (67.8) was measured without the fast path, so it is no longer an upper bound.

The D-TLB miss count of dm64 varies up to 10x between runs (0.54M–5.5M). The guess is guest ASLR:
a direct-mapped TLB indexes by virtual address, so random stack and mmap placement changes how
many entries collide. MIPS doesn't follow the miss count (the run with the most misses was the
fastest), so the MIPS spread here is host noise.

## Host-pointer fast TLB (2026-11-06)

A second, tiny TLB sits in front of `resolve_addr` + the bus in `hart::load`/`store`
([hart.h](../src/hart/hart.h)): `fast_i` (fetch) and `fast_d` (load/store), direct-mapped by VPN.
Each entry holds a tag per access type (`tag_x`, or `tag_r`/`tag_w`) and an `addend`, so that
`host address = addend + va`. A tag is only set after that access passed the full path (old TLB or
walker, permission check, A/D update, bus), and only for pages that lie entirely in RAM. A hit is
compare + add + copy: no permission check, no `ppn` arithmetic, no RAM range check. A successful
write also sets `tag_r` (W without R is reserved in Sv32). LR/SC and AMO keep the old path.

Invalidation: the entries are valid only for the context they were filled in. `fast_check_context()`
runs after trap entry, `mret`, `sret` and `mstatus`/`sstatus` writes, and flushes if
(priv, MPRV ? MPP : priv, SUM, MXR) changed. A `satp` change and every `sfence.vma` flush too.
The old I/D TLBs stay as the second level, so their hit-rate counters now count only fast-TLB misses.

| config (default build: static dm64 + RAM fast path) | MIPS (median, 3 runs) | runs | vs off |
|---|---|---|---|
| without fast TLB | 73.30 | 75.85 73.30 71.05 | 2.53x |
| fast TLB, 64 entries | 127.42 | 127.42 126.07 128.45 | 4.41x |
| **fast TLB, 256 entries (default)** | **135.38** | 136.69 135.38 126.79 | **4.68x** |
| fast TLB, 1024 entries | 122.93 | 122.95 126.19 122.93 | 4.25x |

That's +85% over the previous default. Per run: about 674M fetches with 2.09M fetch fills (99.7% fast
hits), and about 311K flushes (mostly U↔S traps and the kernel's SUM toggles). 1024 entries
are slower because each flush clears 32 KiB. 64 entries refill more often. Flushing per context
instead (for example one table per (priv, SUM) key, or a generation counter instead of clearing)
is the next thing to try if flushes show up in a profile.

## Fetch-page cache (2026-11-06)

`hart::fetch()` ([hart.h](../src/hart/hart.h)) keeps the `fast_i` entry of the page the PC is on
(`fetch_vpn`, `fetch_addend`). A fetch on the same page is one compare + copy, with no fast-TLB
index or table load. On another page it fetches through `load(..., x)` and takes the `fast_i`
entry if one was filled. `fast_flush()` clears it, so it is invalidated by every context change,
`satp` change and `sfence.vma`.

| default build | MIPS (3 runs, alternated A/B in one session) | median |
|---|---|---|
| fast TLB only (previous commit) | 131.26 126.09 129.95 | 129.95 |
| **+ fetch-page cache** | 139.32 136.66 138.34 | **138.34** |

About +6.5%. The new build won all three pairs. A separate 3-run measurement of the new build
gave 141.68 (141.68 141.54 141.94). The old build measured 135.38 above but 129.95 here, because
the host speed varies between sessions. The A/B numbers are the ones to compare.

## Profile after the fetch-page cache (2026-11-06)

`perf record` (software task-clock, because WSL2 has no hardware counters) on one boot + workload
run of the default build with `-g` added. That run got 133 MIPS under perf. Reproduce with
[prof_bench.py](prof_bench.py). With LTO almost everything is inlined into `hart::step()` (88%),
so the breakdown is by source line:

| share | what | where |
|---|---|---|
| ~13% | interrupt check on every instruction (the block plus the `mstatus` field reads) | `hart.cpp:19-58`, `hart.h:51` |
| ~14% | `get_part` bit extraction: decoding fields into members, immediates | `common.h:16`, `common.h:26` |
| ~16% | dispatch: opcode `switch` plus the second-level `funct3` switches | `hart.cpp:74`, `op_imm.cpp:6`, `op_load.cpp:5`, `op_op.cpp:5`, ... |
| ~10% | one `step()` call per instruction: main loop, `waiting` test, `try` setup, epilogue, `regs[0] = 0` | `main.cpp:301-302`, `hart.cpp:158` |
| ~6% | `copy_small`'s `switch (len)`: `len` isn't a compile-time constant on the fast path | `hart.h:287-291` |
| ~4% | slow path: `load_slow`, page-table walk, permission check | |
| <0.5% | context flushes (`fast_check_context`), exception unwinding, device ticks | |

Exceptions, flushes and device ticks don't matter. The interpreter loop itself is the cost.

This run also showed the bad data-table case: 12M fast-D fills, against about 2.4M in good runs
(presumably guest ASLR making pages collide in the direct-mapped `fast_d`). The bad runs are about
7–10% slower.

### Next optimizations, cheapest first

1. ~~**Access size as a template parameter**~~ (`load<4>(addr, ...)`). Tried and reverted
   (2026-11-06): 5 alternating A/B pairs gave a median of 136.95 against 137.71 MIPS for the
   baseline, which is within noise. The ~6% on `hart.h:289` is the `case 4: memcpy` line itself,
   so those samples were most likely the guest memory read, not a branch on `len`. LTO
   apparently already folds the constant size.
2. ~~**Run instructions in batches inside the hart**~~. Done, +10.8%, see below.
3. ~~**Check interrupts only when something changes**~~. Done, +12.0%, see below.
4. ~~**Fix the fast-D conflict case**~~. Investigated and dropped, see below.
5. ~~**Decoded-instruction cache**~~. Done, about +14%, see below.

Suggested order: 1 → 2 → 3, measuring after each, then 4, then 5 as its own task.

## Batched run(n) and the interrupt dirty flag (2026-11-06)

- `hart::step()` is split into `check_interrupts()`, `execute()` and `take_trap()`.
  `hart::run(n)` runs up to n steps with the `try` outside the inner loop. A caught trap is taken,
  counted as a step (as before), and the loop goes on. It returns early when WFI parks the hart.
  `main` calls `run(min(1000, max_instr - instret))`. `step()` is `run(1)`.
- `irq_dirty`: the full interrupt check only runs when it is set. It is set by all `set_*ip`
  (including device MMIO stores in the middle of a batch: CLINT `msip`/`mtimecmp`, PLIC
  claim/complete), any CSR instruction (`csr_rw`, conservative), trap entry, `mret` and `sret`,
  and once at the start of every `run()` batch as a safety net. Interrupts are still taken at the
  exact instruction where they become deliverable. Checking only once per batch would also be
  cheap, but it would take interrupts up to 1000 instructions late and miss short
  `local_irq_enable(); local_irq_disable();` windows. For multicore, `irq_dirty` must become
  atomic, because another hart's thread can raise this hart's `msip` (see [multicore.md](multicore.md)).

5 alternating runs per pair, same session:

| step | runs (MIPS) | median | gain |
|---|---|---|---|
| before (fetch-page cache) | 143.00 140.18 138.87 142.16 140.39 | 140.39 | |
| + batched `run(n)` | 155.62 158.66 155.14 158.25 153.67 | 155.62 | +10.8% |
| batched (second session, A/B with the next row) | 158.48 154.54 158.71 153.82 155.75 | 155.75 | |
| **+ `irq_dirty`** | 174.46 173.24 170.16 174.50 177.21 | **174.46** | +12.0% |

Total +24%. All runs were clean, and the M-mode image runs at 160.16 MIPS.

## Fast-D conflicts: investigated, not fixed (2026-11-06)

**Cause, confirmed with a diagnostic build** that counted fills per `fast_d` index and kept the
last two pages per index. Runs 0-13 were normal (2.41-2.51M fills). Run 14 had 5.69M fills:
index 153 took 1.83M fills alternating between pages `0x7ad99`/`0x9d799`, and index 154 took
1.44M fills between `0x7ad9a`/`0x9d79a`. These are two 2-page user buffers a multiple of 1 MiB
apart, so a copy between them evicts the other entry on every access. The placement is random
(guest ASLR). That's also why a hashed index wouldn't help: any index makes two random pages
collide with the same 1/256 probability. Normal runs also have a fixed kernel conflict
(`0xc07c6`/`0xc01c6` and a few similar pairs, ~200K fills each).

**Frequency**: the bad case appeared in about 1 of 15 runs (1 of 23 in this session). It costs
~10% in that run, so under 1% on average.

**Both fixes cost more than they gain** (5 alternating pairs each, all runs clean):

| variant | median MIPS | baseline in the same A/B | median / max fills |
|---|---|---|---|
| 2-way, 128 sets, both ways checked inline | 147.21 | 154.12 | 2.36M / 2.37M (base 2.49M / 4.47M) |
| 2-way, 256 sets, only way 0 inline; `load_slow`/`store_slow` check way 1 and swap it in | 167.83 | 173.58 | 2.34M / 2.35M (base 2.50M / 2.64M) |

Both remove the conflicts (fills are stable and ~6% lower), but they are 3-4.5% slower in every
run. In the second variant the inline hit path is unchanged, so the cost is most likely the layout
(way-0 entries 32 bytes apart, twice the cache lines) and clearing a 2x larger table on each of the
~311K flushes. Even at zero overhead the gain would be about 1%, below what can be measured
reliably here, so the direct-mapped table stays. Revisit if flushes get cheaper (for example a
generation counter in the tags) or if a workload with a persistent conflict shows up.

## Decoded-instruction cache (2026-11-06)

A memo of `decode(raw)` in front of the decoders ([hart.cpp](../src/hart/hart.cpp) `execute()`,
`decode_into()`). `dcache[(pc >> 2) & mask]` holds `{raw, imm, handler}`. After the fetch, a hit is
`entry.raw == inst`. Then `rd`/`rs1`/`rs2` are taken from `inst` with shifts, `imm` from the
entry, and the handler is called through a plain function pointer (`thunk<&hart::inst_x>`, which
inlines the member handler). A miss decodes into the entry.

- The decoded form depends only on the instruction word, so an entry is valid whenever its `raw`
  equals the fetched word. Self-modifying code, `fence.i`, code written by another hart and
  virtual aliases need no invalidation. A changed word misses and is decoded again.
- The `decode_*` functions return the handler instead of calling it, and illegal encodings
  return `inst_illegal`. They stay pure functions of the word. Privilege checks remain in the
  handlers.
- The immediates are decoded once per format in `decode_into` (U, J, I, B, S), with the same
  expressions the handlers used. The handlers read `imm`. CSR handlers still take the CSR
  number from `inst`, which every fetch still sets.
- Empty entries are `raw = 0` → `inst_illegal`. That's correct, because the all-zero word is illegal.

Size: 4 alternating rounds of all builds on an idle host (load average 0.05). The decode counts
are deterministic. Earlier rounds were run while another process on the host used CPU, and gave
misleading numbers (for example +1.1% for 256K × 24 B).

| entries × entry size | memory | decodes | hit rate | median MIPS | vs base |
|---|---|---|---|---|---|
| base (previous commit) | | | | 172.75 | |
| 16K × 24 B (with rd/rs1/rs2) | 384 KiB | 36.5M | 94.6% | 181.39 | +5.0% |
| 64K × 24 B | 1.5 MiB | 11.0M | 98.4% | 188.87 | +9.3% |
| 256K × 24 B | 6 MiB | 1.70M | 99.75% | 189.75 | +9.8% |
| 64K × 16 B | 1 MiB | 10.9M | 98.4% | 193.41 | +12.0% |
| 128K × 16 B | 2 MiB | 3.79M | 99.44% | 197.03 | +14.1% |
| **256K × 16 B (chosen)** | **4 MiB** | **1.70M** | **99.75%** | **196.02** | **+13.5%** |

The hot code (kernel plus busybox) needs more than 64 KiB of coverage. 16-byte entries (`rd`/`rs1`/`rs2`
taken from `raw` instead of stored) beat 24-byte ones by 3-4% at every size, because the table's host
cache footprint matters. 128K and 256K × 16 B are within noise. 256K is chosen because it won 3 of 4 head-to-head rounds and
has the higher mean (196.0 vs 194.8; the median favors 128K by one run). It costs 4 MiB per hart, so
128K is the fallback if memory matters with many harts.

Testing: the new `bench_tlb.py --check` runs a workload with known output (the shell loop count,
the md5 of 8 MiB of zeros, the md5 of `/bin/busybox`, shell arithmetic with negative division). It
fails if a value is missing, so wrong results are caught, not only crashes. Same output before and
after. The M-mode image runs at 179.77 MIPS.

Next step if needed: a per-physical-page decoded array (found through the fetch-page cache) would
have no conflict or capacity misses and no index masking. Or basic blocks, which need
store-side invalidation.

## common.h helpers instead of raw bit operations, and PGO (2026-11-06)

The bit operations added in this work now use the `common.h` helpers (`get_part`, `get_bit`, with
`oright` to place values), the same as the rest of the code. The exception is 4 lines in `execute()`.
Behavior is unchanged: every replacement is an exact identity for the values involved, and
`--check` passes.

**Which helper calls compile differently**: the committed code, plus one replacement at a time,
built with the same flags. Then the machine code is compared per function (addresses and padding
normalized):

| replacement | effect on the generated code |
|---|---|
| `rd`/`rs1`/`rs2` in `execute()` | **about 5 extra instructions per guest instruction**: GCC keeps `get_part`'s `(x << a) >> b` shift pair, and each pair needs a copy of `inst` (`mov`/`shl`/`shr`) |
| decode-cache index in `execute()` | same count (`shl`+`shr` instead of `shr`+`and`), register allocation shifts elsewhere in `main` |
| context key (`fast_check_context`) | +2 instructions, only on traps and CSR writes |
| superpage check in `tlb.h` `invalidate` | +3: `get_part(vpn, 19, 10)` also clears bits above 19, which GCC can't prove are zero. Only on `sfence.vma` |
| G bit in `tlb.h` | swapped compare operands, +1 instruction in `sc.w`, rare paths |
| everything else (fetch, load/store hit paths, `fast_fill`, TLB hit PA, walker, VPN shifts in `tlb.h`) | identical code |

So `execute()` keeps raw shifts for `rd`/`rs1`/`rs2` and the index, with a comment. Measured
(6 rounds): helpers everywhere is 4-5% slower, helpers except `execute()` is the same speed. As a
side note, rewriting `get_part` itself as shift-then-mask (identical results, checked on 3.4M
cases) was not a fix: it made the whole build 3-4.5% slower, through inlining and layout changes
elsewhere.

**PGO** (`-fprofile-generate` → training run → `-fprofile-use`, with LTO, trained on the
`--check` workload rather than the benchmark itself, so the result isn't tuned to the test):

| build | median MIPS (5 rounds) |
|---|---|
| **final (no PGO)** | **200.33** |
| final + PGO | 194.75 |
| helpers everywhere | 188.04 |
| helpers everywhere + PGO | 189.68 |

PGO doesn't help (the final + PGO build lost 3 of 5 pairs, one outlier at 175.77). It also doesn't
remove the helpers-everywhere gap, which fits the extra instructions in `execute()` being real work
rather than layout. Not enabled.

## How to run

The default build is devirtualized (`RVSIM_TLB_STATIC=DirectMappedTLB`, so only `--tlb=dm` is
accepted). The matrix needs a build with runtime TLB selection:

```
cmake -S . -B build_dynamic -DCMAKE_BUILD_TYPE=Release -DRVSIM_TLB_STATIC= && cmake --build build_dynamic -j
python3 notes/bench_tlb.py --rvsim build_dynamic/rvsim --reps 3 --out tlb_results.csv   # full matrix incl. mmode, ~25 min
python3 notes/bench_tlb.py --rvsim build_dynamic/rvsim --only off sa64x4 fa64 --reps 1  # subset
python3 notes/bench_tlb.py --only dm64 --reps 3                                         # default build
python3 notes/bench_tlb.py --check                                                      # known-output correctness run
# another static class:
cmake -S . -B build_static_SetAssocTLB -DCMAKE_BUILD_TYPE=Release -DRVSIM_TLB_STATIC=SetAssocTLB
cmake --build build_static_SetAssocTLB -j
python3 notes/bench_tlb.py --rvsim build_static_SetAssocTLB/rvsim --label static_ --only sa64x4
```

The matrix results below were measured with `-DTLB_STATIC` passed through `CMAKE_CXX_FLAGS`
(Release + `-flto`). That is the same define the cache variable now sets.

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
