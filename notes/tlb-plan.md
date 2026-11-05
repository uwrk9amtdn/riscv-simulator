# TLB: review, implementation, benchmark, speed recommendations

## Context
The user wrote an abstract `TLB` interface ([src/hart/tlb.h](src/hart/tlb.h)) and wired it into `hart`
(`resolve_addr`, `sv32_ptw`, `inst_sfence_vma`). They want: (1) a check of that integration,
(2) concrete fully-associative / set-associative / direct-mapped implementations, (3) MIPS measured
with no TLB vs. each kind at several sizes, (4) advice on making the simulator as fast as possible,
with multicore (one host thread per hart) as the end goal. Build is already `Release` + `-flto`.

## Review findings (to report and fix)
1. **Doesn't compile**: `TLB itlb; TLB dtlb;` in [hart.h](src/hart/hart.h) instantiate an abstract class.
2. **Superpage PA is wrong**: hit path does `e->ppn << 22`, but `ppn` holds the full 22-bit PPN
   (pte[31:10]). It must be `(e->ppn >> 10) << 22 | va[21:0]`.
3. **MPRV is broken**: `sv32_perm_check` is a member function and reads the member `priv`. The old
   inline code in `sv32_ptw` read the *parameter* `priv` (the effective privilege after MPRV),
   which shadowed the member. Fix: pass the effective priv in as an argument (walker and hit path).
4. **G bit is ignored** in the interface's contract. Lookup must match `G || asid == e.asid`.
   `invalidate(asid)` must keep global entries.
5. **Superpages vs indexed TLBs**: DM/SA index by low VPN bits, so a 4 MiB entry has no single
   index. Fix: on insert, split the superpage into a 4 KiB entry for the accessed page
   (ppn = ppn[1]<<10 | vpn[0]) and keep `super=true` on it. `invalidate(va)` then scans all entries
   and also drops super entries whose vpn[1] matches (sfence is rare, so a full scan costs nothing).
6. Minor: `tlb.h` has no `#pragma once`. Insert must *replace* an existing (vpn, asid) entry (a D-bit
   refill would otherwise make duplicates in FA/SA). Mask `regs[rs2]` to 9 ASID bits in sfence. The
   `while(1)`/break hack can be a plain `if`. The A-bit check on a hit is dead code (entries are only
   inserted after A is set) but harmless. `sv32_perm_check` computes an unused access-fault cause.
7. What's right: permissions, MXR/SUM and U are re-checked on every hit. A write hitting a D=0
   entry falls back to the walker. The I/D split is correct. The sfence rs1/rs2 cases are right.

On speed: a virtual call that returns a `std::optional<TLBEntry>` by value, twice per instruction
(fetch and data), plus the permission check and the `mmio` virtual dispatch after it. That should
still beat 2–3 PTE bus reads per access, but the abstraction itself costs something. The
benchmark measures that cost with a devirtualized variant.

## Implementation
- **[src/hart/tlb.h](src/hart/tlb.h)**: add `#pragma once`. Add a `TLBStats {hits, misses}` that
  every implementation counts. Add three `final` classes sharing one compact entry
  (`valid`, `vpn`, `ppn`, `asid`, `flags`, `super`):
  - `DirectMappedTLB(n)`: index = vpn & (n-1).
  - `SetAssocTLB(sets, ways)`: index = vpn & (sets-1), linear search over the ways, round-robin
    victim per set.
  - `FullyAssocTLB(n)`: linear search, round-robin victim, plus a one-entry MRU shortcut.
  - `NullTLB`: always misses (the "no TLB" baseline uses the same code path; a second baseline
    skips the lookup completely).
  Sizes must be powers of two. `flush()` = clear valid bits.
- **[src/hart/hart.h](src/hart/hart.h)**: `std::unique_ptr<TLB> itlb, dtlb`, plus a
  `set_tlb(kind, entries, ways)` used by main.
- **[src/hart/hart.cpp](src/hart/hart.cpp)**: fixes 2–5 above. `sv32_perm_check(va, u, xwr, access_type, priv)`.
  Splitting superpages happens in the insert block of `sv32_ptw`.
- **[src/hart/op_system.cpp](src/hart/op_system.cpp)**: mask the ASID. Otherwise unchanged.
- **[src/main.cpp](src/main.cpp)**: new options `--tlb=none|off|dm|sa|fa`, `--tlb-entries=N`,
  `--tlb-ways=W`, `--max-instr=N`. When `--max-instr` is reached, print the instruction count,
  wall time, MIPS and the I/D TLB hit rates to stderr, then exit. Count instructions in the run loop.
- **Devirtualized data point**: a `-DTLB_STATIC=<class>` compile-time switch that makes
  `itlb`/`dtlb` concrete `final` members, so lookups inline. It's only built for the best config,
  to measure the virtual-call overhead.

## Benchmark
- Workloads (S-mode images in `images/s-mode`: `fw_jump.bin` + `Image` + `rootfs.cpio`):
  - **A: Linux boot**: a fixed instruction budget (pick N so that boot is nearly done, about
    10–30 s per run, after a calibration run).
  - **B: userspace churn**: pipe a shell script into stdin (fork/exec loop, e.g.
    `for i in $(seq 300); do ls / >/dev/null; done`, plus an arithmetic loop), with a larger budget.
  - The M-mode image (no translation) as a reference point.
- Configurations: no TLB (skip lookup) / NullTLB. DM: 16, 64, 256, 1024. SA 4-way: 16, 64, 256, 1024,
  plus 2-way and 8-way at 64. FA: 8, 16, 32, 64, 128. The best config devirtualized.
- Each config runs 3 times. Report the median MIPS, the speedup over no TLB, and the I/D hit rates.
  Driven by a script in the scratchpad that writes a CSV and a markdown table.
- Check that the console output (boot log) matches the no-TLB run, so the TLB doesn't change behavior.

## Recommendations: record in the repo, don't implement yet
Write them, together with the benchmark results table and the review findings, to a new
**[notes/performance.md](notes/performance.md)** (linked from `notes/multicore.md` 3.5), so the
next steps are written down in the project. Also summarize them in the final report.
In order of expected gain:
1. A RAM fast path (host pointer, no virtual `mmio` call). Threads need this anyway.
2. A softmmu-style TLB: cache VPN → host pointer, with separate read/write/exec tags already
   permission-checked for the current (priv, SUM, MXR, ASID). Flush when those change. A hit then
   becomes index + compare + add, with no permission check and no bus.
3. A fetch-page cache: keep the host pointer of the current PC's page, and re-translate only when
   PC leaves the page or on a flush.
4. Check interrupts only when the pending/enable state changes (an atomic "dirty" flag), not on
   every step.
5. A decoded-instruction / basic-block cache, flushed on `fence.i`.
6. Multicore: per-hart TLBs need no locks (sfence is local, and SBI RFENCE does remote flushes).
   PTE A/D updates need CAS. Don't put locks on the RAM path. This matches `notes/multicore.md` 3.4.

## Verification
- `cmake --build build` builds cleanly, and boots S-mode Linux to the shell with each TLB kind
  (the console log matches the no-TLB run).
- The busybox userspace workload runs without segfaults or oopses. This catches permission, D-bit,
  ASID and superpage bugs.
- `notes/performance.md` contains the review, the results table (hardware, image and budget noted)
  and the recommendations. The benchmark script is also committed (`notes/bench_tlb.sh`) so the
  results can be reproduced.
