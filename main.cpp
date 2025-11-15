#include "hart.h"
#include "mmio.h"

#include "plic.h"
#include "clint.h"
#include "ram.h"
#include "uart.h"

#include <stdio.h>
#include <stdlib.h>

#include <string>
#include <stdexcept>

#include <getopt.h>
#include <libfdt.h>

std::vector<u8> load_file(std::string file)
{
    u32 size;
    std::vector<u8> r;

    FILE* fp = fopen(file.c_str(), "r");
    if (!fp) {
        throw std::runtime_error("file does not exist");
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
        throw std::runtime_error(std::string("fdt_open_into failed: ") + fdt_strerror(err));
    }

    chosen = fdt_path_offset(fdt_data, "/chosen");
    if (chosen < 0) {
        chosen = fdt_add_subnode(fdt_data, 0, "chosen");
        if (chosen < 0) {
            throw std::runtime_error(std::string("Failed to create /chosen: ") + fdt_strerror(chosen));
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
        throw std::runtime_error(std::string("fdt_setprop_string failed: ") + fdt_strerror(err));
    }

    if (initrd_start != 0) {
        err = fdt_setprop_u32(fdt_data, chosen, "linux,initrd-start", initrd_start);
        if (err) {
            throw std::runtime_error(std::string("fdt_setprop_u32 failed: ") + fdt_strerror(err));
        }

        err = fdt_setprop_u32(fdt_data, chosen, "linux,initrd-end", initrd_end);
        if (err) {
            throw std::runtime_error(std::string("fdt_setprop_u32 failed: ") + fdt_strerror(err));
        }
    }

    err = fdt_pack(fdt_data);
    if (err) {
        throw std::runtime_error(std::string("fdt_pack failed: ") + fdt_strerror(err));
    }

    fdt.resize(fdt_totalsize(fdt_data));
}

int main(int argc, char** argv)
{

    const char* kernel = 0;
    const char* initrd = 0;
    const char* dtb = 0;
    const char* append = 0;

    static struct option long_options[] = {
        {"kernel", required_argument, 0, 0},
        {"initrd", required_argument, 0, 1},
        {"dtb", required_argument, 0, 2},
        {"append", required_argument, 0, 3},
        {0, 0, 0, 0}
    };

    int opt;
    int opt_index;

    while ((opt = getopt_long(argc, argv, "", long_options, &opt_index)) != -1) {
        switch (opt) {
        case 0:
            kernel = optarg;
            break;
        case 1:
            initrd = optarg;
            break;
        case 2:
            dtb = optarg;
            break;
        case 3:
            append = optarg;
            break;
        default:
            return 1;
        }
    }

    mmio mmio0(4);
    hart hart0(&mmio0, 0, 0x80000000);

    ram ram0(128 * 1024 * 1024);

    machine_context hart0_machine_context(&hart0);
    plic plic0({&hart0_machine_context}, 1);

    clint clint0({&hart0}, 10000000);

    uart uart0(0, &plic0, 1);

    mmio0.add_device(&ram0, 8);
    mmio0.add_device(&plic0, 3);
    mmio0.add_device(&clint0, 2);
    mmio0.add_device(&uart0, 1);

    u32 kernel_start = 0x80000000;
    u32 initrd_start = 0x86000000;
    u32 dtb_start = 0x87000000;
    u32 initrd_end = 0;

    if (kernel) {
        auto f = load_file(kernel);

        bool ret = mmio0.store(kernel_start, f.size(), (u8*)&f[0]);
        if (ret == false) {
            throw std::runtime_error("could not write kernel to ram");
        }
    }

    if (initrd) {
        if (!kernel) {
            throw std::runtime_error("enabled initrd without kernel");
        }
        auto f = load_file(initrd);

        bool ret = mmio0.store(initrd_start, f.size(), (u8*)&f[0]);
        if (ret == false) {
            throw std::runtime_error("could not write initrd to ram");
        }
        initrd_end = initrd_start + f.size();
    }

    if (dtb) {
        if (!kernel) {
            throw std::runtime_error("enabled dtb without kernel");
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
        bool ret = mmio0.store(dtb_start, f.size(), (u8*)&f[0]);

        if (ret == false) {
            throw std::runtime_error("could not write dtb to ram");
        }
    }

    hart0.regs[10] = 0;
    hart0.regs[11] = dtb_start;
    hart0.pc = kernel_start;

    while (1) {
        hart0.step();
        mmio0.tick();
    }
}