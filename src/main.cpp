#include "hart.h"
#include "mmio.h"

#include "plic.h"
#include "clint.h"
#include "ram.h"
#include "uart.h"

#include <stdio.h>
#include <stdlib.h>

#include <string>
#include <chrono>
#include <ctime>
#include <csignal>

#include <getopt.h>
#include <poll.h>
#include <libfdt.h>

// TODO: disable ctrl+c

std::vector<u8> load_file(std::string file)
{
    u32 size;
    std::vector<u8> r;

    FILE* fp = fopen(file.c_str(), "r");
    if (!fp) {
        runtime_error("file does not exist");
    }

    fseek(fp, 0, SEEK_END);
    size = ftell(fp);
    fseek(fp, 0, SEEK_SET);

    r.resize(size);
    fread(&r[0], 1, size, fp);
    return r;
}

void patch_fdt(std::vector<u8>& fdt, u32 initrd_start, u32 initrd_end, std::string bootargs)
{
    fdt.resize(fdt.size() + 4096);
    u8* fdt_data;
    fdt_data = &fdt[0];
    int err;
    int chosen;

    err = fdt_open_into(fdt_data, fdt_data, fdt.size());
    if (err) {
        runtime_error(std::string("fdt_open_into failed: ") + fdt_strerror(err));
    }

    chosen = fdt_path_offset(fdt_data, "/chosen");
    if (chosen < 0) {
        chosen = fdt_add_subnode(fdt_data, 0, "chosen");
        if (chosen < 0) {
            runtime_error(std::string("Failed to create /chosen: ") + fdt_strerror(chosen));
        }
    }

    const char* existing = (const char*)fdt_getprop(fdt_data, chosen, "bootargs", nullptr);
    std::string final_bootargs;

    if (existing && *existing) {
        final_bootargs = std::string(existing) + " " + bootargs;
    } else {
        final_bootargs = bootargs;
    }

    err = fdt_setprop_string(fdt_data, chosen, "bootargs", final_bootargs.c_str());
    if (err) {
        runtime_error(std::string("fdt_setprop_string failed: ") + fdt_strerror(err));
    }

    if (initrd_start != 0) {
        err = fdt_setprop_u32(fdt_data, chosen, "linux,initrd-start", initrd_start);
        if (err) {
            runtime_error(std::string("fdt_setprop_u32 failed: ") + fdt_strerror(err));
        }

        err = fdt_setprop_u32(fdt_data, chosen, "linux,initrd-end", initrd_end);
        if (err) {
            runtime_error(std::string("fdt_setprop_u32 failed: ") + fdt_strerror(err));
        }
    }

    err = fdt_pack(fdt_data);
    if (err) {
        runtime_error(std::string("fdt_pack failed: ") + fdt_strerror(err));
    }

    fdt.resize(fdt_totalsize(fdt_data));
}

// SIGUSR1 stops the run and prints the statistics (used by benchmark drivers)
static volatile sig_atomic_t stop_requested = 0;

static void on_sigusr1(int) { stop_requested = 1; }

struct sim_options {
    std::string tlb_kind = "sa";
    u32 tlb_entries = 64;
    u32 tlb_ways = 4;
    u64 max_instr = 0; // 0: run forever
};

void parse_cmdline(int argc, char* const* argv, const char** bios, const char** kernel, const char** initrd, const char** dtb, const char** append, sim_options* sim)
{
    static struct option long_options[] = {
        {"kernel", required_argument, 0, 0},
        {"initrd", required_argument, 0, 1},
        {"dtb", required_argument, 0, 2},
        {"append", required_argument, 0, 3},
        {"bios", required_argument, 0, 4},
        {"tlb", required_argument, 0, 5},         // off, null, dm, sa, fa
        {"tlb-entries", required_argument, 0, 6},
        {"tlb-ways", required_argument, 0, 7},
        {"max-instr", required_argument, 0, 8},   // stop after this many instructions and print MIPS
        {0, 0, 0, 0}
    };

    int opt;
    int opt_index;

    while ((opt = getopt_long(argc, argv, "", long_options, &opt_index)) != -1) {
        switch (opt) {
        case 0:
            *kernel = optarg;
            break;
        case 1:
            *initrd = optarg;
            break;
        case 2:
            *dtb = optarg;
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
        case '?':
            printf("unknown option: %s", argv[optind - 1]);
        default:
            printf("unknown option: %c", optopt);
        }
    }
}

int main(int argc, char** argv)
{
    const char* bios = 0;
    const char* kernel = 0;
    const char* initrd = 0;
    const char* dtb = 0;
    const char* append = 0;

    sim_options sim;

    parse_cmdline(argc, argv, &bios, &kernel, &initrd, &dtb, &append, &sim);

    int instr_per_tick = 1000;

    mmio mmap(28, 4);
    mmio prpl(24, 4);

    hart hart_0(&mmap, 0, 0x80000000);
    hart_0.set_tlb(sim.tlb_kind, sim.tlb_entries, sim.tlb_ways);

    ram ram0(128 * 1024 * 1024);

    m_context m_ctx_0(&hart_0);
    s_context s_ctx_0(&hart_0);

    plic plic0({&m_ctx_0, &s_ctx_0}, 1);

    clint clint0({&hart_0});

    uart uart0(0, &plic0, 1);

    // TODO: add devices to memory map using base addresses. Create mmio tree automatically based on the addresses
    // TODO: generate dtb automatically from mmio

    mmap.add_device(&ram0, 8);  // 0x8000_0000
    mmap.add_device(&prpl, 0);

    prpl.add_device(&uart0, 1);  // 0x0100_0000
    prpl.add_device(&clint0, 2); // 0x0200_0000
    prpl.add_device(&plic0, 3);  // 0x0300_0000

    u32 bios_start = 0x80000000;
    u32 kernel_start = bios ? 0x80400000 : 0x80000000; // without bios, kernel runs directly in m mode
    u32 initrd_start = 0x86000000;
    u32 dtb_start = 0x87000000;
    u32 initrd_end = 0;

    if (bios) {
        auto f = load_file(bios);

        bool ret = mmap.store(bios_start, f.size(), (u8*)&f[0]);
        if (ret == false) {
            runtime_error("could not write bios to ram");
        }
    }

    if (kernel) {
        auto f = load_file(kernel);

        bool ret = mmap.store(kernel_start, f.size(), (u8*)&f[0]);
        if (ret == false) {
            runtime_error("could not write kernel to ram");
        }
    }

    if (initrd) {
        if (!kernel) {
            runtime_error("enabled initrd without kernel");
        }
        auto f = load_file(initrd);

        bool ret = mmap.store(initrd_start, f.size(), (u8*)&f[0]);
        if (ret == false) {
            runtime_error("could not write initrd to ram");
        }
        initrd_end = initrd_start + f.size();
    }

    if (dtb) {
        if (!kernel && !bios) {
            runtime_error("enabled dtb without kernel or bios");
        }
        auto f = load_file(dtb);

        std::string bootargs;
        if (append) {
            bootargs = append;
        }
        if (!initrd) {
            initrd_start = 0;
            initrd_end = 0;
        }
        patch_fdt(f, initrd_start, initrd_end, bootargs);
        bool ret = mmap.store(dtb_start, f.size(), (u8*)&f[0]);

        if (ret == false) {
            runtime_error("could not write dtb to ram");
        }
    }

    hart_0.regs[10] = 0;
    hart_0.regs[11] = dtb_start;
    hart_0.pc = bios ? bios_start : kernel_start;

    signal(SIGUSR1, on_sigusr1);

    u64 instret = 0; // steps, including the ones that trapped
    auto t0 = std::chrono::steady_clock::now();
    std::clock_t c0 = std::clock();

    while ((sim.max_instr == 0 || instret < sim.max_instr) && !stop_requested) {
        if (hart_0.waiting) {
            // the hart is parked in WFI. Sleep in the kernel until input arrives or a device deadline passes
            u64 now = host_now_ns();
            u64 deadline = mmap.next_deadline_ns();

            struct pollfd pfd;
            pfd.fd = uart0.rx_fd(); // poll ignores a negative fd
            pfd.events = POLLIN;
            pfd.revents = 0;

            timespec ts;
            timespec* timeout = nullptr; // no deadline: only input can wake the machine

            if (deadline != NO_DEADLINE) {
                u64 d = deadline > now ? deadline - now : 0;
                ts.tv_sec = d / 1000000000ull;
                ts.tv_nsec = d % 1000000000ull;
                timeout = &ts;
            }

            if (ppoll(&pfd, 1, timeout, nullptr) > 0) {
                uart0.poll_input();
            }
        } else {
            int i;
            for (i = 0; i < instr_per_tick && !hart_0.waiting; i++) {
                hart_0.step();
            }
            instret += i;
        }

        mmap.tick();

        if (hart_0.waiting && hart_0.wfi_wake_pending()) {
            hart_0.waiting = false;
        }
    }

    double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();

    // cpu time leaves out the time spent sleeping in WFI, so it is the right base for MIPS
    double cpu_secs = double(std::clock() - c0) / CLOCKS_PER_SEC;

    fprintf(stderr, "\n[rvsim] instructions=%llu seconds=%.3f cpu_seconds=%.3f mips=%.2f",
            (unsigned long long)instret, secs, cpu_secs, instret / cpu_secs / 1e6);
    for (auto [name, tlb] : {std::pair{"itlb", hart_0.get_itlb()}, std::pair{"dtlb", hart_0.get_dtlb()}}) {
        if (tlb) {
            const TLBStats& st = tlb->stats();
            u64 total = st.hits + st.misses;
            fprintf(stderr, " %s_hits=%llu %s_misses=%llu %s_hit_rate=%.4f", name, (unsigned long long)st.hits,
                    name, (unsigned long long)st.misses, name, total ? (double)st.hits / total : 0.0);
        }
    }
    fprintf(stderr, "\n");

    return 0;
}