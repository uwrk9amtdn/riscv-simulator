#pragma once

#include "common.h"

#include <map>
#include <string>
#include <vector>

class mmio;
class hart;

// writes a flattened device tree with libfdt's sequential-write API: nodes and properties in order, then finish().
// Hands out phandles by object, so a node can refer to another one (a hart's interrupt controller, the PLIC)
// before or after that node is written
class fdt_builder {
public:
    fdt_builder();

    void begin_node(const std::string& name);
    void end_node();

    void prop_empty(const char* name);
    void prop_u32(const char* name, u32 value);
    void prop_cells(const char* name, const std::vector<u32>& cells);
    void prop_string(const char* name, const std::string& value);
    void prop_strings(const char* name, const std::vector<std::string>& values);

    // the phandle of the node that stands for key: a hart (its interrupt controller) or a device. The same key
    // always gets the same phandle; the node writes it with prop_u32("phandle", phandle(this))
    u32 phandle(const void* key);

    std::vector<u8> finish();

private:
    std::vector<u8> buf;
    std::map<const void*, u32> phandles;
};

// "name@base", the usual node name of a device at base
std::string fdt_unit_name(const char* name, u32 base);

// the device tree as dts source, in the form `dtc -I dtb -O dts` prints it (phandles as numbers)
std::string fdt_to_dts(const void* fdt);

struct fdt_chosen {
    std::string bootargs;
    std::string stdout_path; // full path of the console node, e.g. /soc/serial@1000000
    u32 initrd_start = 0;    // 0: no initrd
    u32 initrd_end = 0;
};

// the device tree of the machine: /cpus from the harts, then every device on the bus (RAM at the root, the
// others under /soc) as it describes itself
std::vector<u8> fdt_generate(const mmio& bus, const std::vector<hart*>& harts, const fdt_chosen& chosen);
