#include "hart.h"
#include "mmio.h"

#include "plic.h"
#include "clint.h"
#include "ram.h"
#include "uart.h"
#include "host_io.h"

#include <stdio.h>
#include <stdlib.h>

#include <memory>
#include <string>
#include <vector>
#include <chrono>
#include <ctime>
#include <csignal>

#include <getopt.h>
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

static void fdt_check(int err, const char* what)
{
    if (err < 0) {
        runtime_error(std::string(what) + ": " + fdt_strerror(err));
    }
}

struct fdt_prop {
    std::string name;
    std::vector<u8> value;
};

static std::vector<fdt_prop> fdt_read_props(const void* fdt, int node)
{
    std::vector<fdt_prop> props;
    int offset;
    fdt_for_each_property_offset(offset, fdt, node) {
        const char* name;
        int len;
        const u8* value = (const u8*)fdt_getprop_by_offset(fdt, offset, &name, &len);
        props.push_back({name, std::vector<u8>(value, value + len)});
    }
    return props;
}

// /cpus gets cpu@0 .. cpu@N-1, copied from cpu@0 (the template in soc.dts), each with its own interrupt
// controller phandle. The CLINT (MSIP, MTIP) and the PLIC (MEIP, SEIP) get one interrupts-extended pair
// per hart, in hart order, which is also the order of the PLIC contexts (m0, s0, m1, s1, ...)
static void fdt_set_harts(void* fdt, u32 nharts)
{
    int cpus = fdt_path_offset(fdt, "/cpus");
    fdt_check(cpus, "/cpus not found");
    int cpu0 = fdt_subnode_offset(fdt, cpus, "cpu@0");
    fdt_check(cpu0, "/cpus/cpu@0 not found");
    int intc0 = fdt_subnode_offset(fdt, cpu0, "interrupt-controller");
    fdt_check(intc0, "/cpus/cpu@0/interrupt-controller not found");

    std::vector<fdt_prop> cpu_props = fdt_read_props(fdt, cpu0);
    std::vector<fdt_prop> intc_props = fdt_read_props(fdt, intc0);

    std::vector<u32> phandles(nharts);
    phandles[0] = fdt_get_phandle(fdt, intc0);
    if (phandles[0] == 0) {
        runtime_error("cpu@0 interrupt-controller has no phandle");
    }
    u32 max_phandle;
    fdt_check(fdt_find_max_phandle(fdt, &max_phandle), "fdt_find_max_phandle");
    for (u32 i = 1; i < nharts; i++) {
        phandles[i] = max_phandle + i;
    }

    fdt_check(fdt_del_node(fdt, cpu0), "fdt_del_node cpu@0");

    // fdt_add_subnode puts a new node before the existing children, so add them in reverse to get cpu@0 first
    for (int i = nharts - 1; i >= 0; i--) {
        cpus = fdt_path_offset(fdt, "/cpus");
        std::string name = "cpu@" + std::to_string(i);
        int cpu = fdt_add_subnode(fdt, cpus, name.c_str());
        fdt_check(cpu, "fdt_add_subnode cpu");
        for (auto& p : cpu_props) {
            if (p.name == "reg") {
                fdt_check(fdt_setprop_u32(fdt, cpu, "reg", i), "fdt_setprop reg");
            } else if (p.name != "phandle" && p.name != "linux,phandle") {
                fdt_check(fdt_setprop(fdt, cpu, p.name.c_str(), p.value.data(), p.value.size()), "fdt_setprop cpu");
            }
        }
        int intc = fdt_add_subnode(fdt, cpu, "interrupt-controller");
        fdt_check(intc, "fdt_add_subnode interrupt-controller");
        for (auto& p : intc_props) {
            if (p.name == "phandle" || p.name == "linux,phandle") {
                fdt_check(fdt_setprop_u32(fdt, intc, p.name.c_str(), phandles[i]), "fdt_setprop phandle");
            } else {
                fdt_check(fdt_setprop(fdt, intc, p.name.c_str(), p.value.data(), p.value.size()), "fdt_setprop intc");
            }
        }
    }

    auto set_interrupts = [&](const char* compatible, std::initializer_list<u32> irqs) {
        int node = fdt_node_offset_by_compatible(fdt, -1, compatible);
        fdt_check(node, compatible);
        std::vector<fdt32_t> cells;
        for (u32 i = 0; i < nharts; i++) {
            for (u32 irq : irqs) {
                cells.push_back(cpu_to_fdt32(phandles[i]));
                cells.push_back(cpu_to_fdt32(irq));
            }
        }
        fdt_check(fdt_setprop(fdt, node, "interrupts-extended", cells.data(), cells.size() * sizeof(fdt32_t)),
                  "fdt_setprop interrupts-extended");
    };
    set_interrupts("riscv,clint0", {3, 7});  // MSIP, MTIP
    set_interrupts("riscv,plic0", {11, 9});  // MEIP, SEIP
}

void patch_fdt(std::vector<u8>& fdt, u32 initrd_start, u32 initrd_end, std::string bootargs, u32 nharts)
{
    fdt.resize(fdt.size() + 4096 + 1024 * nharts);
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

    if (nharts > 1) {
        fdt_set_harts(fdt_data, nharts);
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
    std::string tlb_kind = "dm";
    u32 tlb_entries = 64;
    u32 tlb_ways = 1;
    u64 max_instr = 0; // 0: run forever
    u32 harts = 1;
    u32 quantum = 1000; // instructions a hart runs before the devices tick and the next hart runs
    const char* dump_dtb = nullptr; // write the final (patched) dtb here
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
        {"harts", required_argument, 0, 9},
        {"dump-dtb", required_argument, 0, 10},
        {"quantum", required_argument, 0, 11},
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
        case 9:
            sim->harts = strtoul(optarg, nullptr, 0);
            break;
        case 10:
            sim->dump_dtb = optarg;
            break;
        case 11:
            sim->quantum = strtoul(optarg, nullptr, 0);
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

    if (sim.quantum < 1) {
        runtime_error("--quantum must be at least 1");
    }
    u32 instr_per_tick = sim.quantum;

    mmio mmap(28, 4);
    mmio prpl(24, 4);

    if (sim.harts < 1 || sim.harts > 64) {
        runtime_error("--harts must be 1..64");
    }
    if (sim.harts > 1 && !bios) {
        runtime_error("--harts > 1 needs --bios: OpenSBI parks the other harts and starts them for the kernel");
    }

    std::vector<std::unique_ptr<hart>> hart_list;
    std::vector<hart*> harts;
    std::vector<std::unique_ptr<hart_context>> contexts;
    std::vector<hart_context*> plic_contexts; // m0, s0, m1, s1, ...: the order of interrupts-extended in the dtb

    for (u32 i = 0; i < sim.harts; i++) {
        hart_list.push_back(std::make_unique<hart>(&mmap, i, 0x80000000));
        harts.push_back(hart_list.back().get());
        harts.back()->set_tlb(sim.tlb_kind, sim.tlb_entries, sim.tlb_ways);

        contexts.push_back(std::make_unique<m_context>(harts.back()));
        plic_contexts.push_back(contexts.back().get());
        contexts.push_back(std::make_unique<s_context>(harts.back()));
        plic_contexts.push_back(contexts.back().get());
    }

    ram ram0(128 * 1024 * 1024);

    plic plic0(plic_contexts, 1);

    clint clint0(harts);

    uart uart0(0, &plic0, 1);

    // TODO: add devices to memory map using base addresses. Create mmio tree automatically based on the addresses
    // TODO: generate dtb automatically from mmio

    mmap.add_device(&ram0, 8);  // 0x8000_0000
    for (hart* h : harts) {
        h->set_ram(0x80000000, ram0.size(), ram0.host_ptr());
    }
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
        patch_fdt(f, initrd_start, initrd_end, bootargs, sim.harts);
        if (sim.dump_dtb) {
            FILE* out = fopen(sim.dump_dtb, "wb");
            if (!out || fwrite(f.data(), 1, f.size(), out) != f.size()) {
                runtime_error("could not write --dump-dtb file");
            }
            fclose(out);
        }
        bool ret = mmap.store(dtb_start, f.size(), (u8*)&f[0]);

        if (ret == false) {
            runtime_error("could not write dtb to ram");
        }
    }

    // every hart starts at the reset pc with a0 = hartid, a1 = dtb. OpenSBI picks the boot hart by lottery and
    // parks the others in WFI until the kernel starts them (SBI HSM, an MSIP IPI)
    for (u32 i = 0; i < harts.size(); i++) {
        harts[i]->regs[10] = i;
        harts[i]->regs[11] = dtb_start;
        harts[i]->pc = bios ? bios_start : kernel_start;
    }

    signal(SIGUSR1, on_sigusr1);

    host_io io(&mmap);

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
                std::lock_guard<std::mutex> lock(mmap.lock);
                mmap.tick();
            }
            wake_ready();

            if ((sim.max_instr != 0 && instret >= sim.max_instr) || stop_requested) {
                break;
            }
        }

        if (all_waiting) {
            io.wait_until_event(); // every hart is parked in WFI: sleep until host input or a device deadline
            {
                std::lock_guard<std::mutex> lock(mmap.lock);
                mmap.tick();
            }
            wake_ready();
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