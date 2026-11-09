#include "ram.h"
#include "devicetree.h"
#include <cstring>
#include <sys/mman.h>

ram::ram(u32 size)
{
    this->size_ = size;
    void* p = mmap(nullptr, size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);
    if (p == MAP_FAILED) {
        runtime_error("ram: could not allocate " + std::to_string(size >> 20) + " MiB");
    }
    this->data = (u8*)p;
}

bool ram::load(u32 addr, u32 len, u8* data)
{
    if (addr >= size_ || len > size_ - addr) {
        return false;
    }

    if (data == 0) {
        return false;
    }

    memcpy(data, this->data + addr, len);

    return true;
}

bool ram::store(u32 addr, u32 len, const u8* data)
{
    if (addr >= size_ || len > size_ - addr) {
        return false;
    }

    if (data == 0) {
        return false;
    }

    memcpy(this->data + addr, data, len);

    return true;
}

u64 ram::size() const
{
    return size_;
}

void ram::tick() { }

std::string ram::fdt_node_name(u32 base) const
{
    return fdt_unit_name("memory", base);
}

void ram::fdt_props(fdt_builder& b, u32 base) const
{
    b.prop_string("device_type", "memory");
    b.prop_cells("reg", {base, size_});
}

ram::~ram()
{
    munmap(data, size_);
}