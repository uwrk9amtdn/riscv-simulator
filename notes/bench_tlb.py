#!/usr/bin/env python3
"""TLB benchmark driver for rvsim.

Boots the S-mode image on a pty, types a workload at the shell prompt, waits for its end marker,
then sends SIGUSR1 so rvsim prints `[rvsim] instructions=... cpu_seconds=... mips=...` and TLB stats.

usage: bench_tlb.py [--reps N] [--out results.csv] [--only NAME] [--rvsim PATH]
"""
import argparse, csv, os, pty, re, select, signal, statistics, subprocess, sys, time

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
IMG = os.path.join(ROOT, "images", "s-mode")
IMG_M = os.path.join(ROOT, "images", "m-mode")

# user-space work: a shell loop (fork-free), process creation (fork/exec/exit, page faults, ASIDs),
# and a pipeline that streams 8 MiB through two processes
WORKLOAD = (
    "i=0; while [ $i -lt 20000 ]; do i=$((i+1)); done; "
    "for j in $(seq 150); do ls / > /dev/null; done; "
    "dd if=/dev/zero bs=4096 count=2048 2>/dev/null | md5sum > /dev/null; "
    "echo BENCH_$((40+2))_DONE"
)
MARKER = b"BENCH_42_DONE"

# --check: a workload with known output, to catch wrong results (not only crashes). md5 of 8 MiB of
# zeros is fixed, the busybox md5 is the one of images/s-mode/rootfs.cpio
CHECK_WORKLOAD = (
    "i=0; while [ $i -lt 20000 ]; do i=$((i+1)); done; echo I=$i; "
    "dd if=/dev/zero bs=4096 count=2048 2>/dev/null | md5sum; md5sum /bin/busybox; "
    "echo $((7*191-3)) $((1<<20)) $((-17/5)) $((-17%5)); echo CHECK_$((40+2))_DONE"
)
CHECK_MARKER = b"CHECK_42_DONE"
CHECK_EXPECT = ["I=20000", "96995b58d4cbf6aaa9041b4f00c7f6ae  -",
                "dcfb00dc78aaa8b104be99eb23b6bd56  /bin/busybox", "1334 1048576 -3 -2"]
PROMPT = b"~ # "

CONFIGS = [("off", "off", 0, 0), ("null", "null", 0, 0)]
CONFIGS += [(f"dm{n}", "dm", n, 1) for n in (16, 64, 256, 1024)]
CONFIGS += [(f"sa{n}x4", "sa", n, 4) for n in (16, 64, 256, 1024)]
CONFIGS += [("sa64x2", "sa", 64, 2), ("sa64x8", "sa", 64, 8)]
CONFIGS += [(f"fa{n}", "fa", n, 0) for n in (8, 16, 32, 64, 128)]
CONFIGS += [("mmode", "mmode", 0, 0)]  # reference: different kernel/rootfs, no translation

BAD = [b"Oops", b"Kernel panic", b"Segmentation fault", b"Illegal instruction", b"Unable to handle", b"BUG:"]


def run_once(rvsim, kind, entries, ways, workload=WORKLOAD, marker=MARKER, timeout=300):
    master, slave = pty.openpty()
    if kind == "mmode":  # NOMMU kernel in M-mode, no translation; skip the password login
        cmd = [rvsim, "--kernel", f"{IMG_M}/Image", "--initrd", f"{IMG_M}/rootfs.cpio.gz",
               "--append", "rdinit=/bin/sh"]  # no translation in M-mode, the TLB is unused
    else:
        cmd = [rvsim, "--bios", f"{IMG}/fw_jump.bin", "--kernel", f"{IMG}/Image",
               "--initrd", f"{IMG}/rootfs.cpio",
               f"--tlb={kind}", f"--tlb-entries={max(entries, 1)}", f"--tlb-ways={max(ways, 1)}"]
    p = subprocess.Popen(cmd, stdin=slave, stdout=slave, stderr=subprocess.PIPE)
    os.close(slave)
    out = b""
    sent = False
    deadline = time.time() + timeout
    try:
        while True:
            if time.time() > deadline:
                raise RuntimeError(f"timeout, tail: {out[-500:]!r}")
            r, _, _ = select.select([master], [], [], 0.5)
            if r:
                try:
                    out += os.read(master, 65536)
                except OSError:
                    raise RuntimeError(f"rvsim exited, tail: {out[-500:]!r}")
            if not sent and out.endswith(PROMPT):
                os.write(master, workload.encode() + b"\n")
                sent = True
            if sent and marker in out:
                break
        p.send_signal(signal.SIGUSR1)
        _, err = p.communicate(timeout=30)
        # drain the console
        while select.select([master], [], [], 0.1)[0]:
            try:
                chunk = os.read(master, 65536)
            except OSError:
                break
            if not chunk:
                break
            out += chunk
    finally:
        if p.poll() is None:
            p.kill()
        os.close(master)
    line = [l for l in err.decode().splitlines() if l.startswith("[rvsim]")][-1]
    stats = dict(kv.split("=") for kv in line.split()[1:])
    stats["bad"] = ",".join(b.decode() for b in BAD if b in out)
    return stats, out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--reps", type=int, default=3)
    ap.add_argument("--out", default="tlb_results.csv")
    ap.add_argument("--only", nargs="*")
    ap.add_argument("--rvsim", default=os.path.join(ROOT, "build", "rvsim"))
    ap.add_argument("--label", default="")
    ap.add_argument("--check", action="store_true", help="run the known-output workload once and verify it")
    a = ap.parse_args()

    if a.check:
        st, out = run_once(a.rvsim, "dm", 64, 1, workload=CHECK_WORKLOAD, marker=CHECK_MARKER)
        text = out.decode(errors="replace")
        text = text[text.rindex("echo CHECK_") + 1:]  # only the output, not the echoed command line
        missing = [e for e in CHECK_EXPECT if e not in text]
        print("check:", "FAIL missing " + repr(missing) if missing or st["bad"] else "ok", st["bad"])
        sys.exit(1 if missing or st["bad"] else 0)

    rows = []
    for name, kind, n, w in CONFIGS:
        if a.only and name not in a.only:
            continue
        runs = []
        for _ in range(a.reps):
            st, _ = run_once(a.rvsim, kind, n, w)
            runs.append(st)
            print(name, st, file=sys.stderr, flush=True)
        med = sorted(runs, key=lambda s: float(s["cpu_seconds"]))[len(runs) // 2]
        row = {"config": a.label + name, "kind": kind, "entries": n, "ways": w,
               "instructions": med["instructions"], "cpu_seconds": med["cpu_seconds"],
               "mips": med["mips"],
               "mips_runs": " ".join(r["mips"] for r in runs),
               "itlb_hit_rate": med.get("itlb_hit_rate", ""), "dtlb_hit_rate": med.get("dtlb_hit_rate", ""),
               "bad": " ".join(r["bad"] for r in runs).strip()}
        rows.append(row)
        print(row, flush=True)
    with open(a.out, "w", newline="") as f:
        wr = csv.DictWriter(f, fieldnames=list(rows[0].keys()))
        wr.writeheader()
        wr.writerows(rows)


if __name__ == "__main__":
    main()
