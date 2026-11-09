#!/usr/bin/env python3
"""SMP test driver for rvsim: boots the SMP S-mode image on a pty with any extra rvsim options.

modes:
  idle   wait at the shell prompt and measure the host CPU time rvsim uses over --seconds
  sleep  run `sleep 1` N times in the guest; compare guest-measured and host wall time (lost wakeups, drift)
  run    type --cmd at the prompt and wait for it to finish; print wall time and the console output

examples:
  smp_test.py idle --harts 4 --threads
  smp_test.py sleep --count 60 --harts 4
  smp_test.py run --cmd 'cat /proc/cpuinfo | grep -c hart' -- --harts 4 --threads
Options after `--` (and unknown ones) are passed to rvsim unchanged.
"""
import argparse, os, pty, re, select, signal, subprocess, sys, time

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
IMG = os.path.join(ROOT, "images", "s-mode-smp")
ROOTFS = os.path.join(ROOT, "images", "s-mode", "rootfs.cpio")
PROMPT = b"~ # "
BAD = [b"Oops", b"Kernel panic", b"Segmentation fault", b"Illegal instruction", b"Unable to handle", b"BUG:",
       b"rcu_sched self-detected stall", b"rcu: INFO"]


class Sim:
    def __init__(self, rvsim, extra):
        self.master, slave = pty.openpty()
        cmd = [rvsim, "--bios", f"{IMG}/fw_jump.bin", "--kernel", f"{IMG}/Image", "--initrd", ROOTFS,
               "--dtb", os.path.join(ROOT, "build", "soc.dtb")] + extra
        self.p = subprocess.Popen(cmd, stdin=slave, stdout=slave, stderr=subprocess.PIPE)
        os.close(slave)
        self.out = b""

    def read(self, t):
        r, _, _ = select.select([self.master], [], [], t)
        if r:
            try:
                self.out += os.read(self.master, 65536)
            except OSError:
                raise RuntimeError(f"rvsim exited, tail: {self.out[-500:]!r}")

    def wait_for(self, pred, timeout):
        deadline = time.time() + timeout
        while not pred():
            if time.time() > deadline:
                raise RuntimeError(f"timeout, tail: {self.out[-500:]!r}")
            self.read(0.2)

    def boot(self, timeout=600):
        self.wait_for(lambda: self.out.endswith(PROMPT), timeout)

    def type(self, line):
        os.write(self.master, line.encode() + b"\n")

    def run(self, cmd, timeout):
        """type cmd, wait for an end marker; return (output after the echoed command, wall seconds)"""
        start = len(self.out)
        t0 = time.time()
        self.type(f"{cmd}; echo END_$((40+2))_MARK")
        self.wait_for(lambda: b"END_42_MARK" in self.out[start:], timeout)
        wall = time.time() - t0
        text = self.out[start:].decode(errors="replace").replace("\r", "")
        text = text[text.index("\n") + 1:] if "\n" in text else text  # drop the echoed command line
        return text[:text.rindex("END_42_MARK")], wall

    def cpu_seconds(self):
        f = open(f"/proc/{self.p.pid}/stat").read().rsplit(")", 1)[1].split()
        return (int(f[11]) + int(f[12])) / os.sysconf("SC_CLK_TCK")  # utime + stime

    def stats(self):
        """SIGUSR1: rvsim prints a [rvsim] stats line on stderr and exits"""
        self.p.send_signal(signal.SIGUSR1)
        _, err = self.p.communicate(timeout=30)
        lines = [l for l in err.decode().splitlines() if l.startswith("[rvsim]")]
        return lines[-1] if lines else err.decode()[-300:]

    def bad(self):
        return [b.decode() for b in BAD if b in self.out]

    def close(self):
        if self.p.poll() is None:
            self.p.kill()
            self.p.wait()
        os.close(self.master)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("mode", choices=["idle", "sleep", "run"])
    ap.add_argument("--rvsim", default=os.path.join(ROOT, "build", "rvsim"))
    ap.add_argument("--seconds", type=float, default=5, help="idle: measuring window")
    ap.add_argument("--settle", type=float, default=3, help="idle: wait after the prompt before measuring")
    ap.add_argument("--count", type=int, default=60, help="sleep: number of `sleep 1`")
    ap.add_argument("--cmd", help="run: shell command")
    ap.add_argument("--timeout", type=float, default=1800)
    a, extra = ap.parse_known_args()
    extra = [x for x in extra if x != "--"]

    s = Sim(a.rvsim, extra)
    try:
        t0 = time.time()
        s.boot()
        print(f"boot: {time.time() - t0:.1f} s wall, {s.cpu_seconds():.1f} s cpu", flush=True)
        if a.mode == "idle":
            s.read(a.settle)
            c0, w0 = s.cpu_seconds(), time.time()
            while time.time() - w0 < a.seconds:
                s.read(0.2)
            c1, w1 = s.cpu_seconds(), time.time()
            print(f"idle: {c1 - c0:.2f} s cpu over {w1 - w0:.2f} s wall ({100 * (c1 - c0) / (w1 - w0):.1f}% of one core)")
            # wake-up check: the shell must still answer promptly
            out, wall = s.run("echo $((6*7))", 30)
            print(f"wake: answered {out.strip()!r} in {wall:.2f} s")
        elif a.mode == "sleep":
            out, wall = s.run(f"a=$(date +%s); for i in $(seq {a.count}); do sleep 1; done; b=$(date +%s); "
                              "echo GUEST=$((b-a))", a.count * 5 + 60)
            m = re.search(r"GUEST=(\d+)", out)
            print(f"sleep x{a.count}: guest {m.group(1) if m else '?'} s, host wall {wall:.1f} s")
        else:
            out, wall = s.run(a.cmd, a.timeout)
            sys.stdout.write(out)
            print(f"run: {wall:.1f} s wall")
        print("bad:", s.bad() or "none")
        print(s.stats())
    finally:
        s.close()


if __name__ == "__main__":
    main()
