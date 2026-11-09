# multicore branch notes

This branch starts from `smode` at `ea006e3` and holds two prototypes that are kept for reference:

1. `c098f01` puts the hart to sleep on WFI so the host can sleep too (from the old `stash@{0}`).
2. `10546df` adds an sv32 translation cache (from `unfinished.patch`). It is known to be broken, and `ai-notes.txt` lists why.

Neither one has been booted on Linux in this form. Both build. The WFI design will be redone, so section 1 explains what the prototype does and which parts are worth keeping. Section 3 covers multicore and cache coherency.

---

## 1. WFI prototype (`c098f01`)

### The problem it solves

Before this commit:

- WFI was a nop, so an idle guest spun in its idle loop and used 100% of a host core.
- Time was counted in instructions. The CLINT added `MTIME_FREQ / tick_freq` to `mtime` on every tick, assuming 100 MIPS. The simulator actually runs at about 40–60 MIPS on the S-mode image, so a guest `sleep 3` took about 5.1 s.
- The UART receive timeout was 100 ticks, which is 100,000 instructions, so the delay depended on emulation speed.

A prototype of the same design (2026-10-09) brought idle host CPU from 100% down to about 2% with no change in busy speed, and guest time became correct. These are the prototype's numbers. The committed code has not been measured.

### Design

The design is single-threaded. WFI never blocks inside `hart::step()`. It only sets a flag. The main loop sees the flag and puts the host to sleep in `ppoll()`, with a timeout set to the earliest moment a device will change state by itself. Input on any device's host fd (`mmio_device::host_fds()`, collected from the whole device tree by `host_io`) ends the sleep early; today that is the UART's input fd.

### Changes by file

**`src/common.h`**
- `host_now_ns()` returns `CLOCK_MONOTONIC` in ns. It is now the only time base of the machine.
- `NO_DEADLINE = UINT64_MAX` means "this device never changes state by itself".

**`src/hart/hart.h`, `src/hart/op_system.cpp`**
- `bool waiting` is public on purpose, because the main loop owns it. `inst_wfi()` advances `pc` first and then sets `waiting` if no wake condition holds. When an interrupt is later taken, `mepc`/`sepc` therefore points after the WFI, which is what the spec requires.
- `wfi_wake_pending()` returns `((mip | seip) & mie) != 0`. It **ignores `mstatus.MIE/SIE` and `mideleg`**. The spec says WFI resumes when any interrupt that is enabled in `mie` becomes pending, whatever the global enable bits say. Linux and OpenSBI both idle with global interrupts off: they run WFI and then take the interrupt once they re-enable them. Earlier testing showed that if you check MIE as well, the M-mode image hangs during boot.
- The `seip` term is needed because SEIP coming from the PLIC is stored apart from `mip`, just as in `step()` and the CSR read paths.

**`src/devices/mmio_device.h`, `src/mmio.{h,cpp}`**
- There is a new virtual `u64 next_deadline_ns()` that defaults to `NO_DEADLINE`. It returns the host time at which the device will change state with no input from anyone, such as a timer firing or a FIFO timeout.
- `mmio::next_deadline_ns()` returns the minimum over all devices in its slots. Because `mmio` is a device too, the nested `prpl` bus works without extra code.

**`src/devices/clint.{h,cpp}`**
- `mtime` is no longer stored. `mtime_now()` computes it as `(host_now_ns() - time0_ns) / 100 + mtime_offset`, which is 10 MHz and matches `timebase-frequency` in `soc.dts`.
- When the guest writes `mtime` (either half, or all 64 bits), the change is stored as `mtime_offset += new - current`, so guest time keeps running from the written value.
- `next_deadline_ns()` returns, for each hart whose `mtimecmp` is still in the future, `now + (mtimecmp - mtime) * 100 ns`. It skips harts that already have an interrupt pending. It also skips deltas above 2^50. That covers `mtimecmp = -1` (meaning "never") and stops the multiplication from overflowing.
- `tick()` reads the clock again and updates MTIP. MTIP is also updated when `mtimecmp` is written.
- The constructor no longer takes `tick_freq`.

**`src/devices/uart.{h,cpp}`**
- The receive (character) timeout is now a deadline: `rx_deadline_ns = now + 1 ms`. It is re-armed when a byte arrives or when the guest reads RBR, and `tick()` sets `rx_timed_out` once the deadline passes. Testing showed key echo latency dropping from about 90 ms to about 2 ms with this change.
- `can_rx()` is true only when the receiver has room: the FIFO is below 16 bytes, or in non-FIFO mode DR is clear. It is false in loopback mode and after EOF.
- `poll_input()` reads bytes from the host fd only while `can_rx()` holds. Anything beyond that stays in the host tty buffer. As a result **LSR.OE (overrun) is never set**. That differs from real hardware, but no data is lost.
- `rx_fd()` returns `-1` while the receiver is full. This matters because a full receiver leaves the fd readable, so `ppoll` would return immediately and the WFI sleep would turn back into a busy loop. `ppoll` ignores negative fds.
- `tick()` checks the host fd at most once per ms (`POLL_PERIOD_NS`) while the hart runs. Before, it made a `poll()` syscall every tick.
- If `read()` returns 0 or an error, the UART sets `rx_eof` and stops reading the fd. This handles piped stdin.

**`src/main.cpp`**
- `instr_per_second` is gone.
- The loop is now:
  - if `waiting`: get `mmap.next_deadline_ns()` and `ppoll(uart fd, deadline - now)`. A null timeout means wait for input only. On input, call `uart0.poll_input()`.
  - otherwise: step up to 1000 instructions, stopping early if the hart enters WFI.
  - `mmap.tick()`
  - if `waiting && wfi_wake_pending()`: clear `waiting`.

### Limitations and things to keep in mind when redoing it

- The main loop only knows about one hart. ~~And one UART fd~~: done (2026-11-06). Devices report their fds through `mmio_device::host_fds()`, and `host_io` collects them from the whole device tree, so a future virtio or network device only has to implement `host_fds()`/`poll_input()`.
- The PLIC has no deadline and the loop does not tick it separately. That is fine for now, because it changes only when a device signals it, and every device signal goes through `tick()` or `poll_input()` before the wake check.
- `tick()` now calls `clock_gettime` once per 1000 instructions (CLINT and UART each make one call). That is a vDSO call of about 20 ns, so it costs very little. Keep the tick interval coarse anyway.
- MTIP and the UART interrupt are still sampled only every 1000 instructions while the hart runs. That is about 20 µs at current speed, which is good enough.
- **Keep these parts in any future design:** real-time `mtime`, deadline-based device timeouts, the `(mip|seip) & mie` wake test, and never waiting on an fd you won't read.

---

## 2. Translation cache prototype (`10546df`)

### What it adds

- `src/hart/cache.h` defines a template `cache<KeyT, DataT>` around `std::unordered_map`, with `get`, `set`, `invalidate` and `flush`.
- `hart` has a member `cache<u32,u32> tlb`.
- `resolve_addr()`: when translation is active (effective privilege S/U and `satp.MODE == 1`), it looks up `va >> 12`. On a hit, it uses the cached PPN with the page offset of `va`. On a miss, it calls `sv32_ptw()` and stores `pa >> 12`.
- `inst_sfence_vma()` flushes the whole cache. It ignores `rs1`/`rs2`.
- `ai-notes.txt` holds the review of this diff.

### Why it is wrong (short version of `ai-notes.txt`)

1. **Hits skip every check.** A hit returns the PA without checking R/W/X, U/SUM, MXR or the privilege level. User code can reach a supervisor page after the kernel touched it, and a read-only page can be written once it has been read. That breaks copy-on-write.
2. **The D bit is lost.** If a read fills the entry, later writes hit and never set D in the PTE, so the kernel can drop modified pages.
3. **ASIDs.** The `satp.ASID` bits are writable ([csr.cpp](../src/hart/csr.cpp), `csr_rw_satp` only clears bits 21:20), so Linux will use ASIDs and stop flushing on context switch. The cache is not keyed by ASID, so processes would see each other's mappings.
4. Smaller issues: the template parameters are ignored (the map is declared `<u32,u32>`), there is no include guard (`#pragma once`), `get` hashes twice, and a hash map lookup on every access is slow.

### Fix direction

- Use a direct-mapped array indexed by the low VPN bits. Each entry holds a tag (VPN plus a generation counter, and later an ASID), the PPN, and the leaf permission bits R/W/X/U/D.
- On a hit, run the same permission check as the walker. Move that check out of `sv32_ptw` into a shared function so the two cannot drift apart.
- Treat a write to an entry with D=0 as a miss, so the walker sets D.
- Flush by bumping the generation counter. Until entries carry the ASID, either flush on `satp` writes or hardwire ASID to 0, in which case Linux flushes on every switch by itself.
- Keep separate entries, or separate permission checks, for fetch and data. MPRV changes the effective privilege for data accesses only.

The TLB matters more for speed than threads do. The M-mode image, which does no page walks, runs at about 160 MIPS, against about 41 MIPS for the S-mode image, and the S-mode image makes 3.5–4 bus calls per instruction. Do this before multicore.

---

## 3. Multicore and cache coherency

### 3.1 Coherency: what actually needs it

The host CPU is already cache-coherent, so a simulator that is meant to be fast **does not need a coherence protocol** like MESI to run SMP Linux correctly. Guest RAM is one host buffer, and every host thread sees the same bytes. Simulated L1/L2 caches with a coherence protocol only make sense if you want a **timing or performance model**, such as counting misses or modeling latency. That is a separate layer that costs a lot of speed. It is best kept optional and off the fast path.

What does need coherence thinking is the **software caches inside the simulator**:

| Simulator cache | Shared between harts? | What keeps it correct |
|---|---|---|
| TLB / translation cache | No, one per hart | `sfence.vma` on the local hart. Linux and OpenSBI do remote flushes by sending an IPI and running `sfence.vma` on each target hart (SBI RFENCE), so no cross-thread invalidation is needed. |
| PTE A/D updates by the walker | They write shared RAM | Must be a CAS on the PTE once harts run in parallel (see the TODO in `sv32_ptw`). |
| A future decoded-instruction / block cache | No, one per hart | Invalidate on local `fence.i`. Remote `fence.i` also comes through an SBI IPI. Stores from the hart itself don't need tracking, because the spec only requires visibility after `fence.i`. |
| A future host-pointer cache for RAM pages | Could be shared | The memory map never changes at runtime, so it never goes stale. |
| LR/SC reservation | Per hart, but invalidated by other harts' stores | See 3.3. |

So "cache coherency" for this project mostly means: keep simulator caches per hart, and invalidate them only through the guest's architectural fence instructions.

### 3.2 Single-hart assumptions in the current code

These are the places that need to change for N harts, in no particular order:

- `main.cpp` hardcodes `hart_0`, one `m_context`/`s_context` pair, `clint0({&hart_0})` and `regs[10] = 0`. **`a0` must be `mhartid` for each hart.** OpenSBI reads it and picks a boot hart with its lottery. The other harts wait in WFI for an SBI HSM start, which makes MSIP wake-up from WFI necessary for SMP to work.
- `soc.dts` has one `cpu@0`. CLINT `interrupts-extended` lists only CPU0 (MSIP, MTIP), and the PLIC lists one M and one S context. You need N cpu nodes, CLINT entries for each hart, and 2N PLIC contexts. That is a good reason to generate the dtb from the machine description, as the TODO in `main.cpp` says.
- The PLIC already takes a context vector and the CLINT already takes a hart vector, so their register layouts should scale. Check that the PLIC address decoding handles context strides past context 1.
- OpenSBI and Linux need to be built for SMP: `CONFIG_SMP`, `NR_CPUS`, and an OpenSBI platform with a large enough hart count. The FDT platform reads it from the dts.
- AMOs are implemented as `load` then `store`, LR/SC keeps a private `reservation_set`, `fence` and `fence.i` are nops, and the walker updates PTEs non-atomically. All of this is correct for one hart only.
- The main loop's WFI handling looks at `hart_0` only.

### 3.3 Step 1: N harts, one host thread (round-robin)

Run each hart for a quantum of instructions (for example 1000), then switch to the next. This is how QEMU's single-threaded TCG mode works.

- Every instruction runs completely before the next hart runs, so **AMOs are atomic for free** and the guest memory model holds automatically (sequential consistency).
- **LR/SC:** a store from another hart to the reserved address must make the SC fail. The simplest correct rule is to **clear a hart's reservation whenever it is switched out**. SC may fail spuriously, and the forward-progress guarantee only covers short constrained LR/SC loops (at most 16 instructions), which will almost never cross a quantum boundary twice in a row.
- WFI: a hart with `waiting` set is skipped. The host sleeps only when **all** harts are waiting. The deadline is the minimum over devices, which already includes every hart's `mtimecmp`. A CLINT MSIP write from one hart wakes another one automatically, because the wake check runs after the step.
- Results are deterministic and easy to debug. Keep this mode later as a command-line option even after threads exist.
- There is no speedup. Total throughput is the same as one hart. It lets you bring up and test SMP Linux before taking on the threading problems.

### 3.4 Step 2: one host thread per hart

This is where the speedup comes from. Earlier measurements showed that the bus has to be designed for this first:

- **Never lock the RAM path.** An uncontended `std::mutex` on every bus access made the busy loop 1.8x slower, because RAM sits on the same `mmio` object as the peripherals. Split the bus: RAM accesses go through a host pointer (`ram_base + (pa - 0x80000000)`) with no virtual call and no lock. Only peripheral accesses, which are about 1 in 40,000 bus calls, take a lock. Do this split first. It also speeds up single-core.
- **Torn accesses.** Aligned 1/2/4-byte guest accesses must be single host accesses. Use `std::atomic_ref<uN>` with `memory_order_relaxed`, not a byte-wise `memcpy`. Racy plain accesses are also undefined behavior in C++.
- **Memory model.** The guest uses RVWMO. On an x86 host (TSO, which is stronger), relaxed atomics plus mapping `fence` to `std::atomic_thread_fence(seq_cst)` is enough. On an ARM host you also have to honour the `.aq`/`.rl` bits on AMOs and LR/SC. Treating them all as `seq_cst` is correct and simple.
- **AMOs** become host atomics on the RAM pointer: `fetch_add`, `exchange`, `fetch_and`, `fetch_or` and `fetch_xor` directly, with min/max/minu/maxu as CAS loops. AMOs to MMIO go through the peripheral lock.
- **LR/SC.** The usual approach, as in QEMU's MTTCG, is: LR records the address and the loaded value, and SC does a `compare_exchange(recorded value → new value)` on the host. This has an ABA problem in theory, but it is accepted in practice and Linux runs fine on it. The exact alternative is a global reservation table that every store checks, which is too slow.
- **PTE A/D updates** in `sv32_ptw` use CAS. If the CAS fails, walk again.
- **Interrupt lines.** `mip` bits are set by devices or other harts and read by the owning hart on each instruction. Make the pending bits `std::atomic<u32>` that writers update with `fetch_or`/`fetch_and`. The hart does one relaxed load per step, which is about free on x86. `seip` belongs in the same word.
- **IPIs and WFI.** A hart in WFI blocks its own thread on a futex or condition variable. Anyone who sets one of its pending bits (CLINT MSIP, PLIC, timer) must wake it. The timer needs a timed wait until that hart's `mtimecmp` deadline. If `mtimecmp` is written while the hart sleeps, wake it so it can recompute the deadline. Earlier testing showed that leaving this out causes lost timer interrupts. *Superseded by `multicore-plan.md`: harts do untimed waits on their `irq_lines` word, and one I/O thread owns all deadlines.*
- **I/O.** Put one host thread, or the peripheral side, in charge of `ppoll` on the device fds and of device deadlines such as the UART timeout. It posts interrupts through the atomic lines above. The WFI prototype's `next_deadline_ns`/`rx_fd` interfaces fit this thread.
- **mtime** already comes from the host clock, so every thread can read it with no locking. Only `mtime_offset` (guest writes, which are rare) needs to be atomic.
- **Per-hart TLB and icache need no locks**, as explained in 3.1.

### 3.5 Suggested order

1. RAM fast path: a host pointer, no virtual call for RAM. Helps single-core now, and threads need it.
2. A correct TLB (section 2). This is the biggest single-core speed gain.
3. N harts round-robin (3.3): dts generation, `a0 = mhartid`, the PLIC/CLINT contexts, reservation clearing on switch, all-harts-waiting WFI. Boot SMP Linux and check `nproc`.
4. A decoded-instruction cache per hart, flushed on `fence.i`, if profiling says it is worth it.
5. Threads (3.4), keeping round-robin as an option. Done: threads are the default, `--round-robin` runs all harts in one thread (not deterministic, mtime follows the host clock, so not called `--deterministic`).
6. Only then, if you want timing numbers, an optional cache and coherence *model*.
