# Multicore plan: WFI, interrupt lines and the two schedulers

This plan builds on `multicore.md`. It turns sections 1 (WFI) and 3 (multicore) into concrete steps, and it covers two ways of running N harts:

- **Round-robin**: one host thread. Each hart runs `n` instructions, then the next hart runs. This is the default mode, and it is deterministic.
- **Threaded**: one host thread per hart, plus one I/O thread. Turned on with `--threads`.

Both modes share the same interrupt-line, WFI and device interfaces. The only difference is who runs `step()` and who sleeps. The WFI part of this plan **replaces** the "IPIs and WFI" bullet in `multicore.md` 3.4: harts never do timed waits, and the I/O thread owns all deadlines.

Before phase 4 (threads), do items 1 and 2 of `multicore.md` 3.5 (the RAM fast path and a correct TLB). Phases 1–3 don't depend on them.

Items 1 and 2 are done, along with a fast TLB, a fetch-page cache, batched `run(n)`, an interrupt dirty flag and a decode cache. They change some of phase 1 and phase 4. See "Status and next steps" in [performance.md](performance.md).

---

## Design summary

| Piece | Owner | Mechanism |
|---|---|---|
| `mip` software bits (SSIP, STIP, software SEIP), `mie`, `mideleg` | owning hart | plain fields, as today |
| MEIP, SEIP (PLIC), MTIP, MSIP | any thread | `std::atomic<u32> irq_lines`, `fetch_or` / `fetch_and`, `notify_one` on a 0→1 edge |
| Effective pending set | owning hart | `effective_mip() = mip \| irq_lines` |
| WFI sleep | round-robin: the scheduler; threaded: the owning hart | skip the hart / `irq_lines.wait(old)` in a recheck loop |
| `mtime` | everyone | host clock, lock-free read (only `mtime_offset` is atomic) |
| Device deadlines, host fds | `host_io` | `ppoll` on device fds plus a kick eventfd, timeout from `mmap.next_deadline_ns()` |
| Device state (CLINT, PLIC, UART) | whoever does MMIO | per-device mutex; compute a line's level and publish it under that mutex |

### Wake rule

A hart in WFI wakes when `(effective_mip() & mie) != 0`, whatever `mstatus.MIE/SIE` and `mideleg` say (see `multicore.md` 1). The spec allows waking for no reason, so an early wake is always safe. A late wake can hang the guest, so every design decision below prefers waking too often over missing a wake.

### Why harts don't do timed waits

If each hart computed its own wake deadline, hart code would have to know about the CLINT. The UART timeout and any future fds would still need a home anyway. With one `host_io` that owns every deadline, a hart only waits on its `irq_lines` word, and a timer interrupt costs one extra futex wake (a few µs).

---

## Phase 1: interrupt lines in one atomic word (single hart, no behavior change) — done 2026-11-06

Done as described below, with one addition: `set_line()` also sets `irq_dirty` (the interrupt check
only runs when it is set, see performance.md). That's still a plain `bool`. With threads, a line
raised by another thread has to reach the running hart, so phase 4 must make it atomic, or have
`run()` also compare `irq_lines` with the value it last checked (one relaxed load per instruction,
a plain `mov` on x86). There's a `TODO(multicore)` at `set_line()`. Steps 6 (MEIP/MTIP/MSIP not
writable in `mip`) and 5 (SSIP/STIP stay in `mip`) were already true. Check: `--check`, the idle /
wake / 3000-byte paste test and the M-mode image pass, and speed is the same within noise
(124.91 vs 126.61 MIPS, alternated A/B).


Goal: replace `bool seip` with a general word for external lines. The machine should behave exactly as it does today.

1. In `hart.h`, add `std::atomic<u32> irq_lines{0};` and remove `seip`.
2. Change `effective_mip()` to `mip | irq_lines.load(std::memory_order_acquire)`. Every reader already goes through it (`step()`, `csr_rw_mip`, `csr_rw_sip`, `wfi_wake_pending()`).
3. Add `set_line(u32 bit, bool level)`:
   ```cpp
   void set_line(u32 bit, bool level) {
       if (level) {
           u32 old = irq_lines.fetch_or(bit, std::memory_order_release);
           if (!(old & bit)) irq_lines.notify_one();   // a syscall only on a 0->1 edge
       } else {
           irq_lines.fetch_and(~bit, std::memory_order_release); // clearing never wakes anyone
       }
   }
   ```
4. `set_meip`, `set_seip`, `set_mtip` and `set_msip` call `set_line()`, and stop writing `mip`.
5. Leave SSIP and STIP in `mip`, because the hart writes them itself (STIP through M-mode writes, which OpenSBI uses for timer emulation). If Sstc is added later, STIP becomes a line driven by the CLINT side and moves into `irq_lines`.
6. Remove MEIP, MTIP and MSIP from the `mip` write mask if any path still allows writing them. They are read-only in `mip`.
7. Reserve bit 31 of `irq_lines` as `LINE_KICK`. No CSR ever exposes it, so mask it out in `effective_mip()`, and keep it out of the WFI test. It is used later to stop a sleeping hart (exit, debugger halt).

Check: the M-mode and S-mode images boot as before, and idle host CPU stays low.

---

## Phase 2: `host_io` (single hart, no behavior change) — done 2026-11-06

Goal: move the `ppoll` branch out of `main()` into an object that both schedulers can use, and stop
hardcoding the UART's fd.

What was built:

1. `mmio_device` has two more virtual functions with empty defaults:
   - `host_fds(std::vector<host_fd>& out)` appends `{fd, device}` for every host fd the device
     currently waits on. The rule from the UART stays: leave an fd out while the device can't take
     input, because it would stay readable and turn the WFI sleep into a busy loop.
   - `poll_input()` is called when one of the device's fds is readable. It must not block.
2. `mmio` forwards `host_fds()` to its children, the same way it already forwards `tick()` and
   `next_deadline_ns()`. Any device anywhere in the tree is found automatically, with no
   registration list. A device can have several fds.
3. `src/host_io.{h,cpp}`: `host_io(mmio_device* root)` and `wait_until_event()`. That collects the
   fds and the earliest deadline from the root, sleeps in `ppoll`, then calls `poll_input()` on the
   devices with readable fds (any `revents`, so a hang-up reaches the device as end of input).
4. The UART's `rx_fd()` became the `host_fds()` override. `poll_input()` is now an override.
5. `main()`'s waiting branch is `io.wait_until_event()`. `mmap.tick()` and the wake check stay after
   it, as before, so the order of events didn't change.

Not done yet, because single-threaded mode doesn't need it: `kick()` (an eventfd in the poll set)
and `deadline_changed()`. With one thread the host only sleeps while every hart is parked, so
nothing can move a deadline during the sleep, and the deadline computed just before it is always
current. Phase 4 adds both: the I/O thread sleeps while harts run, so a `mtimecmp` write, a UART RBR
read (re-arms the character timeout) or an `mtime` write must kick it.

Check (done): `--check` passes. Idle CPU over 5 s at the shell prompt is 0.12-0.14 s, the same as
before (0.12-0.17 s). Wake latency is 3-7 ms in both. A 3000-byte paste into `wc -c` (fills the
16-byte RX FIFO ~190 times) arrives complete with 0.02 s of host CPU in both.

---

## Phase 3: N harts, round-robin in one thread — done 2026-11-06

**Status.** SMP Linux boots and runs on 2 and 4 harts (`smp: Brought up 1 node, 4 CPUs`). Parallel work
gives correct results, and every hart runs guest code. What differs from the steps below:

- **Device tree: generated, not hand-written.** `patch_fdt()` (main.cpp, `fdt_set_harts()`) rebuilds `/cpus`
  from the `cpu@0` template in `soc.dts`: `reg = i`, a new phandle for each hart's interrupt controller
  (after the existing maximum). It also rewrites `interrupts-extended` of the CLINT (`<&intcN 3 &intcN 7>`) and the
  PLIC (`<&intcN 11 &intcN 9>`) for every hart. libfdt inserts new nodes before the existing children, so
  the cpu nodes are added in reverse to come out as `cpu@0 .. cpu@N-1`. With one hart the dtb is not
  touched. `--dump-dtb FILE` writes the final dtb for inspection (`dtc -I dtb -O dts FILE`).
- **Options:** `--harts N` (1..64; N > 1 needs `--bios`, because OpenSBI parks and starts the other harts)
  and `--quantum n` (default 1000).
- **Scheduler** as in 3.2: one quantum per non-waiting hart, `clear_reservation()` after it when N > 1,
  `mmap.tick()` and a wake check after every quantum. The host sleeps (`host_io`) only when every
  hart is parked. With one hart the order is the same as before (run, tick, wake check).
- **Stats** are summed over the harts. With N > 1, SIGUSR1 also prints `instructions_hartN`.
- **The PLIC needed no change** (context decoding was already generic). The CLINT already took a hart
  vector.

**Guest images** (`images/s-mode-smp/`; the uniprocessor `images/s-mode/` stays the default, so the
benchmark baselines don't change):

- `Image`: Linux 7.2 from `~/Desktop/riscv/sw/linux`, the same minimal config plus `CONFIG_SMP=y`,
  `CONFIG_NR_CPUS=8` (saved as `configs/linux-smp.config`; the previous config is kept there as
  `.config.up`). Everything else the diff changed is SMP-related (tree RCU, spinning locks, IPI
  support). Built in place with `make ARCH=riscv CROSS_COMPILE=riscv32-unknown-linux-musl- -j12 Image`.
- `fw_jump.bin`: OpenSBI 1.9 from `~/Desktop/riscv/sw/opensbi`, with the existing minimal config plus
  `CONFIG_SBI_ECALL_RFENCE`, `CONFIG_SBI_ECALL_IPI`, `CONFIG_SBI_ECALL_HSM` (the old config is kept as
  `build/platform/generic/kconfig/.config.minimal`). The old firmware only offered `time,base`. An SMP kernel then
  hangs right after `Freeing initrd memory` even on one hart, because it needs RFENCE for remote TLB flushes.
  Build: `make PLATFORM=generic PLATFORM_RISCV_XLEN=32 PLATFORM_RISCV_ISA=rv32ima_zicsr_zifencei
  PLATFORM_RISCV_ABI=ilp32 CROSS_COMPILE=riscv32-unknown-linux-musl-`. **Gotcha:** after a config change, run
  `make ... clean` first. The generated extension list (`build/lib/sbi/sbi_ecall_exts.carray.c`) is not
  regenerated otherwise, and the firmware comes out byte-identical.
- The rootfs is unchanged (`images/s-mode/rootfs.cpio`): SMP is a kernel-only change. A buildroot
  rebuild with the host toolchain would need `rsync` on the host and a local option for GCC 16 (this
  buildroot's external-toolchain choice stops at 15, and it checks the major version exactly).

**Checks (done):**

- 1 hart: `--check` passes, the idle / wake / 3000-byte paste test is the same as before, and speed is
  within noise (188.73 vs 192.19 MIPS).
- 2 harts with the uniprocessor kernel and the old firmware: OpenSBI reports 2 harts, parks hart 1, and
  the kernel boots on one.
- SMP kernel on 2 and 4 harts: `/proc/cpuinfo` and `/sys/devices/system/cpu/online` show all CPUs. One
  busy loop per CPU spreads evenly (each hart 532-562M instructions).
- Stress on 4 harts: the shell loop count, the shell arithmetic, 8 parallel `dd | md5sum` (all equal
  the md5 of 4 MiB of zeros) and 8 parallel `md5sum /bin/busybox` (all equal the known value). No
  oops, panic, RCU stall, hung task or segfault.
- `--quantum 100` and `10000` on 2 harts: same correct results. 10000 is faster (262.6 vs 197.6
  aggregate MIPS in one run), because devices tick and harts switch less often.
- Idle with 4 SMP harts: the host sleeps, but uses 0.40 s CPU per 5 s (1 hart: 0.12-0.14 s). Each CPU
  takes its own 250 Hz timer tick. `CONFIG_NO_HZ_IDLE` in the kernel would remove most of these wakeups.

**Run:**

```
build/rvsim --bios images/s-mode-smp/fw_jump.bin --kernel images/s-mode-smp/Image \
    --initrd images/s-mode/rootfs.cpio --dtb build/soc.dtb --harts 4
```


Goal: boot SMP Linux with `--harts N` and no host threads. This mode stays the default, and it is the reference for debugging.

### 3.1 Machine setup

1. Add command-line options: `--harts N` (default 1) and `--quantum n` (default 1000).
2. Create harts in a `std::vector<std::unique_ptr<hart>>` with `mhartid = i`. Set `regs[10] = i` (a0 = hartid) and `regs[11] = dtb_start` on every hart. Start every hart at the same reset `pc`. OpenSBI's lottery picks the boot hart, and the others park in WFI until an SBI HSM start (an MSIP IPI) wakes them.
3. Create one `m_context` and one `s_context` per hart, in the order `m0, s0, m1, s1, ...`. Pass all of them to the PLIC, and pass all harts to the CLINT.
4. Check the PLIC decoding for contexts past 1: enable bits at `0x2000 + 0x80 * ctx`, and threshold/claim at `0x200000 + 0x1000 * ctx`.
5. Device tree: first, ship a hand-written `soc-2.dts` with two `cpu@N` nodes, a CLINT `interrupts-extended` with `<&cpuN_intc 3 &cpuN_intc 7>` for each hart, and a PLIC with `<&cpuN_intc 11 &cpuN_intc 9>` for each hart. Generating the dtb from the machine description (the TODO in `main.cpp`) can come later and replace it.
6. Rebuild OpenSBI and Linux with SMP enabled (`CONFIG_SMP=y`, `CONFIG_NR_CPUS >= N`). OpenSBI's generic platform reads the hart count from the dts.

### 3.2 Scheduler

```cpp
void run_round_robin(std::vector<hart*>& harts, mmio& mmap, host_io& io, int quantum) {
    for (;;) {
        bool all_waiting = true;
        for (hart* h : harts) {
            if (h->waiting) continue;
            all_waiting = false;
            for (int i = 0; i < quantum && !h->waiting; i++) h->step();
            if (harts.size() > 1) h->clear_reservation(); // another hart may store to the reserved address
            mmap.tick();                                  // keep device latency per executed instruction the same as today
            wake_ready(harts);
        }
        if (all_waiting) {
            io.wait_until_event();                        // ticks the devices itself
            wake_ready(harts);
        }
    }
}

void wake_ready(std::vector<hart*>& harts) {
    for (hart* h : harts)
        if (h->waiting && h->wfi_wake_pending()) h->waiting = false;
}
```

Notes:

- `wake_ready()` runs after every quantum, not once per round. An IPI from hart 0 to hart 1 (a CLINT MSIP store during hart 0's quantum) then wakes hart 1 before the round ends.
- The host sleeps only when **every** hart is waiting. Nobody can write `mtimecmp` while all harts sleep, so `kick()` stays a no-op in this mode, and the deadline computed before `ppoll` is always current.
- **LR/SC:** clearing the reservation on switch-out is the simple correct rule (`multicore.md` 3.3). It needs a `hart::clear_reservation()` that resets `reservation_set`.
- AMOs, `fence` and PTE A/D updates need no changes in this mode, because a quantum never interleaves with another hart's instructions.
- Guest time is host time, so with N busy harts each one gets about 1/N of the host core. The guest sees N slower CPUs. That is correct, just slow.
- A smaller `--quantum` lowers IPI and spinlock latency between harts (a hart spinning on a lock held by a switched-out hart burns its whole quantum). A larger one costs fewer `tick()` calls. Start with 1000 and measure boot time with 100 and 10000.

Check:
- `--harts 1` behaves exactly as after phase 2.
- `--harts 2` and `--harts 4`: Linux prints `smp: Brought up 1 node, N CPUs`, `nproc` prints N, and `/proc/interrupts` shows IPI counts going up on every CPU.
- Host CPU at an idle shell stays low. This checks that all-harts-waiting sleeps.
- Two runs with the same input give the same output. This mode is deterministic apart from host-time-driven timer interrupts.

---

## Phase 4: one host thread per hart — IN PROGRESS (stopped 2026-11-06)

**Where it stands.** `--threads` works: SMP Linux boots on 1, 2, 4 and 8 threaded harts and gives
correct results under parallel load. ThreadSanitizer reports no races. Two checks from the list below
are still open (see "Next steps").

**Done (commit 6b131d6, thread-safety, still round-robin):**

- Guest RAM accesses (fast-TLB hits, fetch, `bus_load`/`bus_store`) are relaxed `std::atomic_ref`
  accesses (plain moves on x86). All accesses are aligned: misaligned ones trap, and `mepc`/`sepc`
  and the trap vectors keep `pc` 4-aligned.
- AMOs: `hart::amo()` is one seq_cst compare-exchange loop on the host word (MMIO: load + store
  under the device lock). LR records the physical address and the value, SC compare-exchanges from that
  value. `fence` is `atomic_thread_fence(seq_cst)`. The walker's A/D update is a compare-exchange,
  walking again if it loses.
- `irq_dirty` is atomic. `set_line()` publishes the line, then sets the flag (release). `run()` claims
  it with an `exchange(false, acq_rel)` before the check, so a line raised during the check is never lost.
- **One device lock** (`mmio::lock` on the root bus) instead of one mutex per device: devices call each
  other (UART -> PLIC -> hart lines), so a single lock avoids lock-order problems. It's taken for MMIO in
  `bus_load`/`bus_store` and `amo()`, in `host_io`, and around `tick()`. Not inside `mmio` itself (the root
  bus calls the nested ones).
- `host_io`: an eventfd `kick()` in the poll set, the `mmio_device::notify_host_changed()` hook (the CLINT
  on `mtimecmp`/`mtime` writes, the UART on RBR reads and IER/FCR/MCR writes), and a `ppoll` signal mask.
- Checked in round-robin mode: `--check`, the idle/paste test, the 4-hart SMP stress, and speed (185.64 vs
  184.61 MIPS).

**Done (uncommitted at the time of writing, committed as WIP afterwards):**

- `--threads` (main.cpp): one `std::thread` per hart running `run(quantum)`, and
  `hart::wait_for_interrupt()` (sleeps on `irq_lines.wait()`, returns on a wake condition or `LINE_KICK`)
  when parked. The main thread is the I/O thread: `io.wait_until_event()`, then `tick()` under the lock.
  Harts never tick. SIGUSR1 is blocked in every thread and only unblocked inside the I/O thread's `ppoll`
  (no race between the stop check and the sleep). Shutdown: `stop`, `request_stop()` (sets `LINE_KICK`)
  on every hart, join. `--max-instr` works through a shared atomic total. CMake links `Threads::Threads`.
- Note: `mips=` in the SIGUSR1 line divides by the process CPU time, which with threads is the sum over
  all threads. So it's MIPS per busy core, not throughput. Use wall time for scaling.

**Results so far (`--threads`):**

- Boots: 1 hart (UP kernel), 2 and 4 harts (SMP kernel): all CPUs up, one busy loop per CPU spread
  evenly over the harts.
- 4-hart stress workload twice: all correct (loop count, shell arithmetic, 8 x md5 of 4 MiB of zeros,
  8 x md5 of busybox). No oops, panic, stall or segfault.
- ThreadSanitizer (`-fsanitize=thread -g -O1`, no LTO): boot of 4 threaded harts plus a smaller stress
  run (4 pipelines, 4 md5sum) gives no reports. The run is about 100x slower, ~2 MIPS. TSan was checked to
  work in this environment with a deliberately racy test program.
- Scaling, one 400000-iteration shell loop per guest CPU (host: i5-1235U, a 15 W hybrid laptop CPU, 2 P-cores
  with HT + 8 E-cores, 12 MiB L3):

  | harts | round-robin wall | `--threads` wall | host cores used | threads vs RR |
  |---|---|---|---|---|
  | 1 | 39.4 s | 39.3 s | 1.01 | 1.0x |
  | 2 | 86.9 s | 54.1 s | 1.98 | 1.6x |
  | 4 | 183.5 s | 83.0 s | 3.75 | 2.2x |
  | 8 | 358.3 s | 124.8 s | 6.72 | 2.9x |

  Each thread is slower than one hart alone (4 threads: 311 s CPU for ~157 s of single-hart work). That is
  mostly the host: the power-limited clock drops with more busy cores, and threads land on E-cores. A
  4-thread A/B with a 128K-entry decode cache (2 MiB/hart, fits L3) against 256K (4 MiB/hart) showed no
  difference (20.98 vs 20.79 s median, spread 15.9-21.8 s from the thermal state). So cache size is not the
  limit. Round-robin also scales a bit worse than N x (4 harts 183 s, not 157 s): switching between the
  harts' caches.

**Idle CPU with threads (done):** 4 SMP harts at the shell prompt, host CPU over 10 s (`notes/smp_test.py idle`).
At first `--threads` used 33% of a core against round-robin's ~10%. Counting wakeups and traps found three causes:

1. Kicks: every 32-bit `mtimecmp` store kicked the I/O thread (an eventfd write), ~9 per guest timer
   interrupt, 3600/s. Now `mmio_device::notify_deadline()` kicks only if the new deadline is earlier than the
   one the I/O thread sleeps until (`armed_deadline_ns`, published under the device lock, 0 while awake). A due
   `mtimecmp` doesn't kick at all (`set_mtip` wakes the hart). The I/O thread sleeps at most 1 s, so the
   transient values of a split RV32 write (~429 s away, the other half still old) are never earlier than the
   armed deadline.
2. The guest's per-CPU ticks are aligned to the same jiffy but programmed a few us apart. A 100 us slack
   (`DEADLINE_SLACK_NS`, in `clint::next_deadline_ns` and the kick test) fires them in one I/O wake-up. With
   both: ~1 kick and ~2 I/O wake-ups per 10 ms tick period, down from ~9 and ~12.
3. Not threads-specific: `time`/`timeh` weren't implemented, so each Linux clocksource read trapped to OpenSBI
   (a C++ exception, the firmware's emulation, an MMIO load of mtime under the device lock), ~17 per hart tick,
   ~7000/s idle; the unwinder alone was 15% of idle CPU. The CSRs now read the CLINT's mtime
   (`hart::set_time_source`), gated by `mcounteren.TM` / `scounteren.TM`.

Result, 4 harts, 10 s idle: round-robin 0.86-0.92 s CPU, `--threads` 1.67-1.90 s (was 3.3 s). The rest is
inherent to threads: each guest CPU's 100 Hz tick wakes its own host thread (400 futex wake/sleep cycles/s, each
~0.1 ms of host kernel time in this WSL2 VM, plus user time on a cold core); round-robin handles all four in one
wake-up. Each hart tick still throws 4 traps (MTIP, STIP, 2 ecalls) as C++ exceptions.

Checked after these changes: `bench_tlb.py --check` ok, M-mode NOMMU boot ok, 4-hart stress (4 parallel jobs of
loop + md5 of 8 MiB zeros + md5 of busybox) correct in both modes, 4.8 s with threads vs 14.5 s round-robin.

**Timer drift / lost wakeups (done, 60 s):** 60 x `sleep 1` in the guest, 4 harts: guest 60 s, host wall 60.7 s
(round-robin) and 60.8 s (`--threads`), no hang.

**1-hart round-robin A/B (done):** `bench_tlb.py --only dm64`, 5 interleaved runs each, HEAD (be1f36d) against
6b131d6: 658.7M vs 671.6M instructions (OpenSBI's time emulation is gone), median 3.291 vs 3.568 s CPU, 200.2 vs
188.3 MIPS. No regression from the `main()` restructuring; ~8% less CPU for the same workload.

**ThreadSanitizer after the kick/time changes (done):** TSan build (`-fsanitize=thread -g -O1`, no LTO), 4 threaded
harts: boot, 3 s idle (timer kicks and the armed deadline), 4 parallel jobs (loop + md5 of 1 MiB of zeros, all
correct), 3 x `sleep 1`. No reports; TSan and its `log_path` were checked with a racy test program.

**Next steps (where to continue):**

1. Repeat the IPI-heavy workloads (fork-heavy scripts, TLB shootdowns via SBI RFENCE) in both modes and
   compare. The stress workload above covers part of this.
2. Longer timer drift run (several minutes).
3. Then: update `multicore.md` 3.5 and the README, and decide whether threads become the default (phase 5).
4. Optional, both modes: interrupts and ecalls are still delivered as C++ exceptions (~4 per hart per guest
   tick); taking interrupts without a throw would cut idle CPU further.

Test driver: `notes/smp_test.py` (`idle`, `sleep`, `run --cmd`; extra options go to rvsim).


Goal: `--threads` runs each hart in its own thread for real parallel speed. Requires items 1 and 2 of `multicore.md` 3.5.

### 4.1 Memory and atomics (from `multicore.md` 3.4)

1. Guest RAM accesses go through the host pointer fast path, with no lock.
2. Aligned 1/2/4-byte RAM accesses use `std::atomic_ref<uN>` with `memory_order_relaxed`.
3. AMOs become host atomics on the RAM pointer (`fetch_add`, `exchange`, `fetch_and/or/xor`; CAS loops for min/max). AMOs to MMIO take the device lock.
4. LR records the address and the loaded value. SC does `compare_exchange(recorded value -> new value)`. The switch-out reservation clearing from phase 3 isn't needed in this mode.
5. `fence` becomes `std::atomic_thread_fence(std::memory_order_seq_cst)`. Treat `.aq`/`.rl` as `seq_cst` (needed on ARM hosts, harmless on x86).
6. Update PTE A/D bits in `sv32_ptw` with a CAS. If the CAS fails, walk again.

### 4.2 Devices

1. Give the CLINT, PLIC and UART each a `std::mutex`, taken in `load()`, `store()`, `tick()`, `poll_input()` and `next_deadline_ns()`. MMIO is about 1 in 40,000 bus calls, so the lock costs little.
2. Compute a line's level and publish it under the same lock:
   ```cpp
   void clint::update_mtip(int i) {   // with clint's mutex held
       harts[i]->set_mtip(mtime_now() >= mtimecmp[i]);
   }
   ```
   Otherwise the I/O thread can decide that the *old* `mtimecmp` expired, then a hart writes a future `mtimecmp` and clears MTIP, and then the I/O thread sets MTIP. The guest would get a stale timer interrupt.
3. The PLIC claim/complete path must be atomic per source, because several harts can claim the same interrupt. The mutex already covers this, and a source is given to only one claimer.
4. `mtime_offset` becomes `std::atomic<u64>`. `time0_ns` is constant.

### 4.3 Threads

```cpp
void hart::wait_for_interrupt() {
    for (;;) {
        u32 lines = irq_lines.load(std::memory_order_acquire);
        if (lines & LINE_KICK) return;                          // stop request
        if (((mip | (lines & ~LINE_KICK)) & mie) != 0) break;   // same test as wfi_wake_pending()
        irq_lines.wait(lines, std::memory_order_acquire);       // blocks only while the word is still `lines`
    }
    waiting = false;
}

void hart::run(int quantum) {
    while (!(irq_lines.load(std::memory_order_relaxed) & LINE_KICK)) {
        for (int i = 0; i < quantum && !waiting; i++) step();
        if (waiting) wait_for_interrupt();
    }
}

void io_thread(host_io& io, std::atomic<bool>& stop) {
    io.set_threaded(true);                                      // kick() now writes the eventfd
    while (!stop) io.wait_until_event();
}
```

- No wakeup can be lost. `atomic::wait(old)` returns as soon as the word differs from `old`, so a line raised between the check and the sleep ends the sleep at once. This is the host-side version of the race Linux avoids by idling with interrupts disabled.
- `mie` can be read without a lock, because only the owning hart writes it, and that hart is awake whenever it does.
- A running hart sees new lines on the next `step()`, through `effective_mip()`.
- Hart threads don't call `mmap.tick()`. Only the I/O thread drives device time, and it sleeps in `ppoll` instead of polling every 1000 instructions. The `POLL_PERIOD_NS` throttle in `uart::tick()` isn't needed in this mode.
- A hart that writes `mtimecmp` or reads RBR calls `deadline_changed()`, which kicks the I/O thread so it recomputes its timeout. Without this, timer interrupts get lost (`multicore.md` 3.4).
- `kick()`, `set_threaded()` and `deadline_changed()` don't exist yet (phase 2 left them out, see there). Add them here: `host_io` gets an eventfd and adds it to the poll set built from `host_fds()`, and `mmio_device` gets `deadline_changed()`, which forwards to `host_io::kick()` through a pointer set when the device is added to the bus.
- To shut down: set `stop`, kick the I/O thread, call `set_line(LINE_KICK, 1)` on every hart, then join all threads.

Check:
- Everything from phase 3, run with `--threads`.
- Build with `-fsanitize=thread` and boot to a shell. There should be no reports outside known-benign spots (document them if there are any).
- N busy loops in the guest (`while :; do :; done &` N times) use N host cores.
- With every guest CPU idle, host CPU use is about zero.
- `sleep 1` in a loop for several minutes shows no drift and no hangs. This catches lost timer wakes.
- Repeat the IPI-heavy workloads (fork-heavy shell scripts, `sfence.vma` shootdowns through SBI RFENCE) under `--threads` and in round-robin mode, and compare the results.

---

## Phase 5: cleanup

- Generate the dtb from the machine description, so `--harts N` doesn't need a dts for each N.
- Keep round-robin as the default, or as `--deterministic` if threads become the default. Run CI or regression boots in both modes.
- Update `multicore.md` 3.5 once phases 3 and 4 have landed.

---

## Open questions

- Should the round-robin quantum adapt? For example, a hart that hits WFI or a long LR/SC retry streak could yield early. Measure before adding it.
- Should `mmap.tick()` in round-robin mode run once per round rather than once per quantum when N is large? `tick()` makes about two `clock_gettime` calls (about 40 ns), so this probably doesn't matter below N = 8.
- With Sstc, STIP moves into `irq_lines`, and the CLINT side needs a per-hart `stimecmp` deadline in `next_deadline_ns()`.
