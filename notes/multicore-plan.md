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

## Phase 1: interrupt lines in one atomic word (single hart, no behavior change)

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

## Phase 3: N harts, round-robin in one thread

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

## Phase 4: one host thread per hart

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
