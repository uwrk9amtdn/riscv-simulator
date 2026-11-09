#include "mmio.h"
#include <algorithm>
#include <cstdio>

void mmio::add_device(mmio_device* dev, u32 base)
{
    u64 size = dev->size();
    if (size == 0 || base + size > ((u64)1 << 32)) {
        runtime_error("mmio: device does not fit in the address space");
    }
    for (auto& m : devices) {
        if (base < m.base + m.size && m.base < base + size) {
            char s[96];
            snprintf(s, sizeof(s), "mmio: device at 0x%08x overlaps the one at 0x%08x", base, m.base);
            runtime_error(s);
        }
    }
    devices.push_back({base, size, dev});
    std::sort(devices.begin(), devices.end(), [](const mapping& a, const mapping& b) { return a.base < b.base; });
}

const mmio::mapping* mmio::find(u32 addr, u32 len) const
{
    for (auto& m : devices) {
        u64 off = (u64)addr - m.base; // huge for addr < base
        if (off < m.size && len <= m.size - off) {
            return &m;
        }
    }
    return nullptr;
}

bool mmio::load(u32 addr, u32 len, u8* data)
{
    const mapping* m = find(addr, len);
    return m ? m->dev->load(addr - m->base, len, data) : false;
}

bool mmio::store(u32 addr, u32 len, const u8* data)
{
    const mapping* m = find(addr, len);
    return m ? m->dev->store(addr - m->base, len, data) : false;
}

u64 mmio::size() const {
    return (u64)1 << 32;
}

u64 mmio::next_deadline_ns()
{
    u64 deadline = NO_DEADLINE;
    for (auto& m : devices) {
        u64 d = m.dev->next_deadline_ns();
        if (d < deadline) {
            deadline = d;
        }
    }
    return deadline;
}

void mmio::host_fds(std::vector<host_fd>& out)
{
    for (auto& m : devices) {
        m.dev->host_fds(out);
    }
}

void mmio::tick()
{
    for (auto& m : devices) {
        m.dev->tick();
    }
}
