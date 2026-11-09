#include "hart.h"
#include "mmio.h"

#include "plic.h"
#include "clint.h"
#include "ram.h"
#include "uart.h"
#include "host_io.h"
#include "devicetree.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <algorithm>
#include <atomic>
#include <memory>
#include <thread>
#include <string>
#include <vector>
#include <chrono>
#include <ctime>
#include <csignal>

#include <getopt.h>

// TODO: disable ctrl+c

std::vector<u8> load_file(std::string file)
{
    u32 size;
    std::vector<u8> r;

    FILE* fp = fopen(file.c_str(), "r");
    if (!fp) {
        runtime_error("can't open " + file);
    }

    fseek(fp, 0, SEEK_END);
    size = ftell(fp);
    fseek(fp, 0, SEEK_SET);

    r.resize(size);
    fread(&r[0], 1, size, fp);
    return r;
}

// SIGUSR1 stops the run and prints the statistics (used by benchmark drivers)
static volatile sig_atomic_t stop_requested = 0;

static void on_sigusr1(int) { stop_requested = 1; }

struct sim_options {
    std::string tlb_kind = "dm";
    u32 tlb_entries = 64;
    u32 tlb_ways = 1;
    u64 max_instr = 0; // 0: run forever
    u32 harts = 1;
    u32 quantum = 1000; // instructions a hart runs before the devices tick and the next hart runs
    bool threads = true; // one host thread per hart plus the I/O thread; false: all harts round-robin in one thread
    u64 mem = 128 << 20; // RAM size, -m
    const char* dump_dtb = nullptr; // write the generated device tree here (blob)
    const char* dump_dts = nullptr; // ... and as dts source
};

// -m: a size in bytes with a K, M or G suffix; a plain number is MiB (as in QEMU). 0 if it doesn't parse
static u64 parse_size(const char* s)
{
    char* end;
    u64 n = strtoull(s, &end, 10);
    u64 unit = 1 << 20;
    if (*end) {
        switch (*end) {
        case 'k': case 'K': unit = 1 << 10; break;
        case 'm': case 'M': unit = 1 << 20; break;
        case 'g': case 'G': unit = 1 << 30; break;
        default: return 0;
        }
        if (end[1] && strcmp(end + 1, "iB") != 0 && strcmp(end + 1, "B") != 0) {
            return 0;
        }
    }
    if (end == s || n > ((u64)1 << 40) / unit) {
        return 0;
    }
    return n * unit;
}

static void print_usage(FILE* out, const char* prog)
{
    fprintf(out,
        "usage: %s [options]\n"
        "\n"
        "machine:\n"
        "  -m SIZE              RAM size at 0x80000000, e.g. 256M or 1G; a plain number is MiB (default 128M,\n"
        "                       16M..2G). OpenSBI fw_jump needs at least 36M (it copies the device tree 34 MiB\n"
        "                       into RAM); a 32-bit Linux uses at most ~1G\n"
        "\n"
        "images:\n"
        "  --bios FILE          M-mode firmware (e.g. OpenSBI fw_jump.bin), loaded at the start of RAM\n"
        "  --kernel FILE        kernel image, 4 MiB into RAM with --bios, else at the start of RAM in M mode\n"
        "  --initrd FILE        initramfs, placed below the device tree (needs --kernel)\n"
        "  --append ARGS        added to the kernel command line (console=ttyS0 earlycon)\n"
        "\n"
        "device tree (generated from the machine; a1 points to it at reset):\n"
        "  --dump-dtb FILE      write it to FILE as a blob\n"
        "  --dump-dts FILE      write it to FILE as dts source\n"
        "                       without --bios and --kernel, rvsim exits after writing them\n"
        "\n"
        "harts:\n"
        "  --harts N            number of harts, 1..64 (default 1; N > 1 needs --bios)\n"
        "  --threads            run each hart in its own host thread, plus an I/O thread (the default)\n"
        "  --round-robin        run all harts in one host thread, one quantum each in turn: slower with more\n"
        "                       than one hart, but uses less host CPU while the guest is idle\n"
        "  --quantum N          instructions per hart per run slice / round-robin turn (default 1000)\n"
        "\n"
        "tlb:\n"
        "  --tlb KIND           off, null, dm, sa or fa (default dm)\n"
        "  --tlb-entries N      number of TLB entries (default 64)\n"
        "  --tlb-ways N         ways for --tlb sa (default 1)\n"
        "\n"
        "other:\n"
        "  --max-instr N        stop after N instructions and print MIPS (default: run forever)\n"
        "  --help               show this help\n"
        "\n"
        "example (4 harts, SMP Linux):\n"
        "  %s --bios fw_jump.bin --kernel Image --initrd rootfs.cpio --harts 4\n",
        prog, prog);
}

void parse_cmdline(int argc, char* const* argv, const char** bios, const char** kernel, const char** initrd, const char** append, sim_options* sim)
{
    static struct option long_options[] = {
        {"kernel", required_argument, 0, 0},
        {"initrd", required_argument, 0, 1},
        {"append", required_argument, 0, 3},
        {"bios", required_argument, 0, 4},
        {"tlb", required_argument, 0, 5},         // off, null, dm, sa, fa
        {"tlb-entries", required_argument, 0, 6},
        {"tlb-ways", required_argument, 0, 7},
        {"max-instr", required_argument, 0, 8},   // stop after this many instructions and print MIPS
        {"harts", required_argument, 0, 9},
        {"dump-dtb", required_argument, 0, 10},
        {"quantum", required_argument, 0, 11},
        {"threads", no_argument, 0, 12},
        {"round-robin", no_argument, 0, 13},
        {"dump-dts", required_argument, 0, 14},
        {"help", no_argument, 0, 'h'},
        {0, 0, 0, 0}
    };

    int opt;
    int opt_index;

    while ((opt = getopt_long(argc, argv, "m:", long_options, &opt_index)) != -1) {
        switch (opt) {
        case 0:
            *kernel = optarg;
            break;
        case 1:
            *initrd = optarg;
            break;
        case 'm':
            sim->mem = parse_size(optarg);
            if (sim->mem == 0) {
                fprintf(stderr, "%s: -m: can't read the size '%s' (e.g. 256M, 1G)\n\n", argv[0], optarg);
                print_usage(stderr, argv[0]);
                exit(1);
            }
            break;
        case 3:
            *append = optarg;
            break;
        case 4:
            *bios = optarg;
            break;
        case 5:
            sim->tlb_kind = optarg;
            break;
        case 6:
            sim->tlb_entries = strtoul(optarg, nullptr, 0);
            break;
        case 7:
            sim->tlb_ways = strtoul(optarg, nullptr, 0);
            break;
        case 8:
            sim->max_instr = strtoull(optarg, nullptr, 0);
            break;
        case 9:
            sim->harts = strtoul(optarg, nullptr, 0);
            break;
        case 10:
            sim->dump_dtb = optarg;
            break;
        case 11:
            sim->quantum = strtoul(optarg, nullptr, 0);
            break;
        case 12:
            sim->threads = true;
            break;
        case 13:
            sim->threads = false;
            break;
        case 14:
            sim->dump_dts = optarg;
            break;
        case 'h':
            print_usage(stdout, argv[0]);
            exit(0);
        default: // '?': getopt_long already printed what was wrong (unknown option or missing argument)
            fprintf(stderr, "\n");
            print_usage(stderr, argv[0]);
            exit(1);
        }
    }

    if (optind < argc) {
        fprintf(stderr, "%s: unexpected argument '%s'", argv[0], argv[optind]);
        // getopt_long moves non-options to the end, so look for --threads anywhere: "--threads 4" is a likely mistake
        bool numeric = strspn(argv[optind], "0123456789") == strlen(argv[optind]);
        bool has_mode = false;
        for (int i = 1; i < optind; i++) {
            has_mode |= strcmp(argv[i], "--threads") == 0 || strcmp(argv[i], "--round-robin") == 0;
        }
        if (numeric && has_mode) {
            fprintf(stderr, " (--threads and --round-robin take no value; use --harts N to set the hart count)");
        }
        fprintf(stderr, "\n\n");
        print_usage(stderr, argv[0]);
        exit(1);
    }
}

static int run(int argc, char** argv)
{
    const char* bios = 0;
    const char* kernel = 0;
    const char* initrd = 0;
    const char* append = 0;

    sim_options sim;

    parse_cmdline(argc, argv, &bios, &kernel, &initrd, &append, &sim);

    if (sim.quantum < 1) {
        runtime_error("--quantum must be at least 1");
    }
    u32 instr_per_tick = sim.quantum;

    // the memory map. RAM size from -m; it has to stay below the top of the 32-bit address space
    constexpr u32 RAM_BASE = 0x80000000;
    constexpr u32 UART_BASE = 0x01000000;
    constexpr u32 CLINT_BASE = 0x02000000;
    constexpr u32 PLIC_BASE = 0x03000000;

    if (sim.mem < (16 << 20) || sim.mem > ((u64)1 << 31) || sim.mem % 4096) {
        runtime_error("-m: RAM size must be 16M..2G, a multiple of 4K");
    }

    mmio bus;

    if (sim.harts < 1 || sim.harts > 64) {
        runtime_error("--harts must be 1..64");
    }
    if (bios && sim.mem < (36 << 20)) {
        fprintf(stderr, "warning: -m below 36M: OpenSBI fw_jump copies the device tree to 34 MiB into RAM (its default "
                "FW_JUMP_FDT_OFFSET) and hangs without output if that is outside RAM\n");
    }
    if (sim.harts > 1 && !bios && kernel) {
        runtime_error("--harts > 1 needs --bios: OpenSBI parks the other harts and starts them for the kernel");
    }

    std::vector<std::unique_ptr<hart>> hart_list;
    std::vector<hart*> harts;
    std::vector<std::unique_ptr<hart_context>> contexts;
    std::vector<hart_context*> plic_contexts; // m0, s0, m1, s1, ...: the order of interrupts-extended in the dtb

    for (u32 i = 0; i < sim.harts; i++) {
        hart_list.push_back(std::make_unique<hart>(&bus, i, RAM_BASE));
        harts.push_back(hart_list.back().get());
        harts.back()->set_tlb(sim.tlb_kind, sim.tlb_entries, sim.tlb_ways);

        contexts.push_back(std::make_unique<m_context>(harts.back()));
        plic_contexts.push_back(contexts.back().get());
        contexts.push_back(std::make_unique<s_context>(harts.back()));
        plic_contexts.push_back(contexts.back().get());
    }

    ram ram0(sim.mem);

    plic plic0(plic_contexts, 1);

    clint clint0(harts);
    for (hart* h : harts) {
        h->set_time_source(&clint0);
    }

    uart uart0(0, &plic0, 1);

    bus.add_device(&ram0, RAM_BASE);
    bus.add_device(&uart0, UART_BASE);
    bus.add_device(&clint0, CLINT_BASE);
    bus.add_device(&plic0, PLIC_BASE);
    for (hart* h : harts) {
        h->set_ram(RAM_BASE, ram0.size(), ram0.host_ptr());
    }

    // where the images go. The firmware at the start of RAM, the kernel 4 MiB above it (OpenSBI fw_jump's
    // FW_JUMP_ADDR) or at the start without firmware. The device tree and the initrd sit at the top of the first
    // 512 MiB, below everything a 32-bit kernel can map directly
    const u32 ram_end = RAM_BASE + (u32)std::min<u64>(sim.mem, 512 << 20);
    const u32 bios_start = RAM_BASE;
    const u32 kernel_start = bios ? RAM_BASE + 0x400000 : RAM_BASE;
    const u32 DTB_MAX = 0x10000; // the generated tree is ~250 bytes per hart, so 64 KiB is plenty
    const u32 dtb_start = ram_end - DTB_MAX;
    u32 used_end = RAM_BASE; // end of what is loaded so far, to catch overlaps

    auto load = [&](const char* what, const std::vector<u8>& f, u32 start, u32 reserve) {
        // reserve: the bytes the image takes once running (a kernel's bss), at least its file size
        u64 end = (u64)start + std::max<u64>(f.size(), reserve);
        if (start < used_end || end > dtb_start) {
            char s[160];
            snprintf(s, sizeof(s), "%s does not fit: 0x%08x..0x%08llx overlaps another image or the device tree at "
                     "0x%08x (use -m for more RAM)", what, start, (unsigned long long)end, dtb_start);
            runtime_error(s);
        }
        if (!bus.store(start, f.size(), f.data())) {
            runtime_error(std::string("could not write ") + what + " to ram");
        }
        used_end = end;
    };

    if (bios) {
        load("--bios", load_file(bios), bios_start, 0);
    }

    if (kernel) {
        auto f = load_file(kernel);
        // a RISC-V Linux Image header has the size the kernel takes in memory, bss included (image_size at 16,
        // magic "RSC\x05" at 0x38). Without it, assume the file size
        u64 image_size = 0;
        if (f.size() >= 0x40 && memcmp(&f[0x38], "RSC\x05", 4) == 0) {
            memcpy(&image_size, &f[16], 8);
        }
        load("--kernel", f, kernel_start, (u32)std::min<u64>(image_size, 1u << 31));
    }

    u32 initrd_start = 0;
    u32 initrd_end = 0;
    if (initrd) {
        if (!kernel) {
            runtime_error("--initrd needs --kernel");
        }
        auto f = load_file(initrd);
        if (f.size() > dtb_start - used_end) {
            runtime_error("--initrd does not fit between the kernel and the device tree (use -m for more RAM)");
        }
        initrd_start = (dtb_start - f.size()) & ~0xfffu; // right below the device tree, page aligned
        load("--initrd", f, initrd_start, 0);
        initrd_end = initrd_start + f.size();
    }

    fdt_chosen chosen;
    chosen.bootargs = "console=ttyS0 earlycon";
    if (append) {
        chosen.bootargs += std::string(" ") + append;
    }
    chosen.stdout_path = "/soc/" + uart0.fdt_node_name(UART_BASE);
    chosen.initrd_start = initrd_start;
    chosen.initrd_end = initrd_end;

    std::vector<u8> dtb = fdt_generate(bus, harts, chosen);
    if (dtb.size() > DTB_MAX) {
        runtime_error("generated device tree is larger than its 64 KiB slot");
    }
    if (sim.dump_dtb) {
        FILE* out = fopen(sim.dump_dtb, "wb");
        if (!out || fwrite(dtb.data(), 1, dtb.size(), out) != dtb.size()) {
            runtime_error("could not write --dump-dtb file");
        }
        fclose(out);
    }
    if (sim.dump_dts) {
        std::string dts = fdt_to_dts(dtb.data());
        FILE* out = fopen(sim.dump_dts, "w");
        if (!out || fwrite(dts.data(), 1, dts.size(), out) != dts.size()) {
            runtime_error("could not write --dump-dts file");
        }
        fclose(out);
    }
    if (!bios && !kernel) {
        if (sim.dump_dtb || sim.dump_dts) {
            return 0; // only the device tree was asked for
        }
        runtime_error("nothing to run: give --bios and/or --kernel (see --help)");
    }
    if (!bus.store(dtb_start, dtb.size(), dtb.data())) {
        runtime_error("could not write the device tree to ram");
    }

    uart0.attach_terminal();

    // every hart starts at the reset pc with a0 = hartid, a1 = dtb. OpenSBI picks the boot hart by lottery and
    // parks the others in WFI until the kernel starts them (SBI HSM, an MSIP IPI)
    for (u32 i = 0; i < harts.size(); i++) {
        harts[i]->regs[10] = i;
        harts[i]->regs[11] = dtb_start;
        harts[i]->pc = bios ? bios_start : kernel_start;
    }

    signal(SIGUSR1, on_sigusr1);

    host_io io(&bus);

    u64 instret = 0; // steps, including the ones that trapped
    auto t0 = std::chrono::steady_clock::now();
    std::clock_t c0 = std::clock();

    std::vector<u64> instret_hart(harts.size(), 0);

    auto wake_ready = [&]() {
        for (hart* h : harts) {
            if (h->waiting && h->wfi_wake_pending()) {
                h->waiting = false;
            }
        }
    };

    if (sim.threads) {
        // one thread per hart. The main thread is the I/O thread: it sleeps in ppoll until host input, a device
        // deadline or a kick (a device change from a hart), then lets devices take input and tick. Harts never
        // tick devices. A parked hart sleeps on its irq_lines word and wakes when a line rises
        std::atomic<bool> stop{false};
        std::atomic<u64> total{0};

        // SIGUSR1 stays blocked everywhere except inside the I/O thread's ppoll, so it can't arrive between
        // checking stop_requested and going to sleep. Threads inherit the blocked mask
        sigset_t usr1, ppoll_mask;
        sigemptyset(&usr1);
        sigaddset(&usr1, SIGUSR1);
        pthread_sigmask(SIG_BLOCK, &usr1, &ppoll_mask); // ppoll_mask = the old mask, with SIGUSR1 unblocked
        io.set_threaded(&ppoll_mask);

        std::vector<std::thread> threads;
        for (u32 i = 0; i < harts.size(); i++) {
            threads.emplace_back([&, i]() {
                hart* h = harts[i];
                u64 mine = 0;
                while (!stop.load(std::memory_order_relaxed)) {
                    u64 done = h->run(instr_per_tick);
                    mine += done;
                    if (sim.max_instr != 0 && total.fetch_add(done, std::memory_order_relaxed) + done >= sim.max_instr) {
                        stop = true;
                        io.kick();
                    }
                    if (h->waiting) {
                        h->wait_for_interrupt();
                    }
                }
                instret_hart[i] = mine; // read after join
            });
        }

        while (!stop.load(std::memory_order_relaxed) && !stop_requested) {
            io.wait_until_event();
            std::lock_guard<std::mutex> lock(bus.lock);
            bus.tick();
        }

        stop = true;
        for (hart* h : harts) {
            h->request_stop();
        }
        for (auto& t : threads) {
            t.join();
        }
        for (u64 n : instret_hart) {
            instret += n;
        }
    } else {
        // round-robin: every hart that isn't parked in WFI runs one quantum, then devices tick and parked harts
        // are woken. Waking after every quantum (not once per round) lets an IPI from one hart start the next
        // one in the same round. The host sleeps only when every hart is parked
        while ((sim.max_instr == 0 || instret < sim.max_instr) && !stop_requested) {
            bool all_waiting = true;

            for (u32 i = 0; i < harts.size(); i++) {
                hart* h = harts[i];
                if (h->waiting) {
                    continue;
                }
                all_waiting = false;

                u64 n = instr_per_tick;
                if (sim.max_instr != 0 && sim.max_instr - instret < n) {
                    n = sim.max_instr - instret;
                }
                u64 done = h->run(n);
                instret += done;
                instret_hart[i] += done;

                if (harts.size() > 1) {
                    h->clear_reservation();
                }

                {
                    std::lock_guard<std::mutex> lock(bus.lock);
                    bus.tick();
                }
                wake_ready();

                if ((sim.max_instr != 0 && instret >= sim.max_instr) || stop_requested) {
                    break;
                }
            }

            if (all_waiting) {
                io.wait_until_event(); // every hart is parked in WFI: sleep until host input or a device deadline
                {
                    std::lock_guard<std::mutex> lock(bus.lock);
                    bus.tick();
                }
                wake_ready();
            }
        }
    }

    double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();

    // cpu time leaves out the time spent sleeping in WFI, so it is the right base for MIPS
    double cpu_secs = double(std::clock() - c0) / CLOCKS_PER_SEC;

    fprintf(stderr, "\n[rvsim] instructions=%llu seconds=%.3f cpu_seconds=%.3f mips=%.2f",
            (unsigned long long)instret, secs, cpu_secs, instret / cpu_secs / 1e6);
    // TLB and cache counters are summed over all harts
    for (int which = 0; which < 2; which++) {
        const char* name = which == 0 ? "itlb" : "dtlb";
        u64 hits = 0, misses = 0;
        bool any = false;
        for (hart* h : harts) {
            if (const hart_tlb_t* tlb = which == 0 ? h->get_itlb() : h->get_dtlb()) {
                hits += tlb->stats().hits;
                misses += tlb->stats().misses;
                any = true;
            }
        }
        if (any) {
            u64 total = hits + misses;
            fprintf(stderr, " %s_hits=%llu %s_misses=%llu %s_hit_rate=%.4f", name, (unsigned long long)hits,
                    name, (unsigned long long)misses, name, total ? (double)hits / total : 0.0);
        }
    }
    u64 ifills = 0, dfills = 0, flushes = 0, decodes = 0;
    for (hart* h : harts) {
        ifills += h->get_fast_tlb_stats().ifills;
        dfills += h->get_fast_tlb_stats().dfills;
        flushes += h->get_fast_tlb_stats().flushes;
        decodes += h->get_decodes();
    }
    fprintf(stderr, " fast_ifills=%llu fast_dfills=%llu fast_flushes=%llu", (unsigned long long)ifills,
            (unsigned long long)dfills, (unsigned long long)flushes);
    fprintf(stderr, " decodes=%llu", (unsigned long long)decodes);
    if (harts.size() > 1) {
        for (u32 i = 0; i < harts.size(); i++) {
            fprintf(stderr, " instructions_hart%u=%llu", i, (unsigned long long)instret_hart[i]);
        }
    }
    fprintf(stderr, "\n");

    return 0;
}

int main(int argc, char** argv)
{
    // setup errors (bad options, images that don't fit, missing files) end here with a message, not an abort.
    // The UART's destructor has restored the terminal by then
    try {
        return run(argc, argv);
    } catch (const std::exception& e) {
        fprintf(stderr, "%s: %s\n", argv[0], e.what());
        return 1;
    }
}
