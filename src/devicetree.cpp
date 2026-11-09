#include "devicetree.h"
#include "mmio.h"
#include "hart.h"
#include "clint.h"

#include <cstdio>
#include <libfdt.h>

static void check(int err, const char* what)
{
    if (err < 0) {
        runtime_error(std::string("fdt: ") + what + ": " + fdt_strerror(err));
    }
}

// the generated tree is a few KiB per hart at most; finish() trims the buffer to the real size
static constexpr size_t FDT_BUFFER = 1 << 20;

fdt_builder::fdt_builder() : buf(FDT_BUFFER)
{
    check(fdt_create(buf.data(), buf.size()), "fdt_create");
    check(fdt_finish_reservemap(buf.data()), "fdt_finish_reservemap");
    check(fdt_begin_node(buf.data(), ""), "root node");
}

void fdt_builder::begin_node(const std::string& name)
{
    check(fdt_begin_node(buf.data(), name.c_str()), name.c_str());
}

void fdt_builder::end_node()
{
    check(fdt_end_node(buf.data()), "fdt_end_node");
}

void fdt_builder::prop_empty(const char* name)
{
    check(fdt_property(buf.data(), name, nullptr, 0), name);
}

void fdt_builder::prop_u32(const char* name, u32 value)
{
    check(fdt_property_u32(buf.data(), name, value), name);
}

void fdt_builder::prop_cells(const char* name, const std::vector<u32>& cells)
{
    std::vector<fdt32_t> be;
    for (u32 c : cells) {
        be.push_back(cpu_to_fdt32(c));
    }
    check(fdt_property(buf.data(), name, be.data(), be.size() * sizeof(fdt32_t)), name);
}

void fdt_builder::prop_string(const char* name, const std::string& value)
{
    check(fdt_property_string(buf.data(), name, value.c_str()), name);
}

void fdt_builder::prop_strings(const char* name, const std::vector<std::string>& values)
{
    std::string list; // "a\0b\0c\0"
    for (auto& v : values) {
        list += v;
        list += '\0';
    }
    check(fdt_property(buf.data(), name, list.data(), list.size()), name);
}

u32 fdt_builder::phandle(const void* key)
{
    auto it = phandles.find(key);
    if (it != phandles.end()) {
        return it->second;
    }
    u32 p = phandles.size() + 1; // phandles start at 1
    phandles[key] = p;
    return p;
}

std::vector<u8> fdt_builder::finish()
{
    check(fdt_end_node(buf.data()), "root node end");
    check(fdt_finish(buf.data()), "fdt_finish");
    buf.resize(fdt_totalsize(buf.data()));
    return std::move(buf);
}

std::string fdt_unit_name(const char* name, u32 base)
{
    char s[64];
    snprintf(s, sizeof(s), "%s@%x", name, base);
    return s;
}

// ---- dts output

// a list of one or more printable, NUL-terminated, non-empty strings
static bool is_string_list(const u8* p, int len)
{
    if (len == 0 || p[len - 1] != 0 || p[0] == 0) {
        return false;
    }
    for (int i = 0; i < len; i++) {
        if (p[i] == 0) {
            if (i + 1 < len && p[i + 1] == 0) {
                return false; // empty string in the middle
            }
        } else if (p[i] < 0x20 || p[i] > 0x7e) {
            return false;
        }
    }
    return true;
}

static void print_value(std::string& out, const u8* p, int len)
{
    char tmp[16];
    if (is_string_list(p, len)) {
        out += '"';
        for (int i = 0; i < len - 1; i++) {
            if (p[i] == 0) {
                out += "\", \"";
            } else {
                if (p[i] == '"' || p[i] == '\\') {
                    out += '\\';
                }
                out += (char)p[i];
            }
        }
        out += '"';
    } else if (len % 4 == 0) {
        out += '<';
        for (int i = 0; i < len; i += 4) {
            snprintf(tmp, sizeof(tmp), "%s0x%02x", i ? " " : "", fdt32_to_cpu(*(const fdt32_t*)(p + i)));
            out += tmp;
        }
        out += '>';
    } else {
        out += '[';
        for (int i = 0; i < len; i++) {
            snprintf(tmp, sizeof(tmp), "%s%02x", i ? " " : "", p[i]);
            out += tmp;
        }
        out += ']';
    }
}

static void print_node(std::string& out, const void* fdt, int node, int depth)
{
    std::string indent(depth, '\t');
    const char* name = depth == 0 ? "/" : fdt_get_name(fdt, node, nullptr);
    out += indent + name + " {\n";

    int prop;
    fdt_for_each_property_offset(prop, fdt, node) {
        const char* pname;
        int len;
        const u8* value = (const u8*)fdt_getprop_by_offset(fdt, prop, &pname, &len);
        out += indent + "\t" + pname;
        if (len > 0) {
            out += " = ";
            print_value(out, value, len);
        }
        out += ";\n";
    }

    int child;
    fdt_for_each_subnode(child, fdt, node) {
        out += "\n";
        print_node(out, fdt, child, depth + 1);
    }

    out += indent + "};\n";
}

std::string fdt_to_dts(const void* fdt)
{
    std::string out = "/dts-v1/;\n\n";
    print_node(out, fdt, 0, 0);
    return out;
}

// ---- the machine

std::vector<u8> fdt_generate(const mmio& bus, const std::vector<hart*>& harts, const fdt_chosen& chosen)
{
    fdt_builder b;

    b.prop_u32("#address-cells", 1);
    b.prop_u32("#size-cells", 1);
    b.prop_string("compatible", "riscv-minimal");
    b.prop_string("model", "hd,riscv-minimal");

    b.begin_node("chosen");
    b.prop_string("bootargs", chosen.bootargs);
    if (!chosen.stdout_path.empty()) {
        b.prop_string("stdout-path", chosen.stdout_path);
    }
    if (chosen.initrd_start) {
        b.prop_u32("linux,initrd-start", chosen.initrd_start);
        b.prop_u32("linux,initrd-end", chosen.initrd_end);
    }
    b.end_node();

    // one cpu node per hart; its interrupt controller is the target of the CLINT's and PLIC's interrupts-extended
    b.begin_node("cpus");
    b.prop_u32("#address-cells", 1);
    b.prop_u32("#size-cells", 0);
    b.prop_u32("timebase-frequency", clint::TIMEBASE_HZ);
    for (hart* h : harts) {
        u32 id = h->mhartid;
        b.begin_node(fdt_unit_name("cpu", id));
        b.prop_string("device_type", "cpu");
        b.prop_u32("reg", id);
        b.prop_string("status", "okay");
        b.prop_string("compatible", "riscv");
        // what the hart implements (misa in hart.cpp: I, M, A, S, U; Zicsr, Zifencei; Sv32)
        b.prop_string("riscv,isa", "rv32ima_zicsr_zifencei");
        b.prop_string("riscv,isa-base", "rv32i");
        b.prop_strings("riscv,isa-extensions", {"i", "m", "a", "zicsr", "zifencei"});
        b.prop_string("mmu-type", "riscv,sv32");
        b.begin_node("interrupt-controller");
        b.prop_u32("#interrupt-cells", 1);
        b.prop_empty("interrupt-controller");
        b.prop_string("compatible", "riscv,cpu-intc");
        b.prop_u32("phandle", b.phandle(h));
        b.end_node();
        b.end_node();
    }
    b.end_node();

    // memory nodes sit at the root, all other devices on the simple-bus /soc
    for (auto& m : bus.mappings()) {
        if (m.dev->fdt_at_root()) {
            std::string name = m.dev->fdt_node_name(m.base);
            if (!name.empty()) {
                b.begin_node(name);
                m.dev->fdt_props(b, m.base);
                b.end_node();
            }
        }
    }

    b.begin_node("soc");
    b.prop_string("compatible", "simple-bus");
    b.prop_u32("#address-cells", 1);
    b.prop_u32("#size-cells", 1);
    b.prop_empty("ranges");
    for (auto& m : bus.mappings()) {
        if (!m.dev->fdt_at_root()) {
            std::string name = m.dev->fdt_node_name(m.base);
            if (!name.empty()) {
                b.begin_node(name);
                m.dev->fdt_props(b, m.base);
                b.end_node();
            }
        }
    }
    b.end_node();

    return b.finish();
}
