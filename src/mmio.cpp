#include "mmio.h"
#include <stdexcept>

mmio::mmio(int address_bits, int slot_bits)
{
    this->address_bits = address_bits;
    this->slot_bits = slot_bits;
    devices.resize(1 << slot_bits, 0);
}

void mmio::add_device(mmio_device* dev, u32 slot)
{
    if (slot >= devices.size()) {
        runtime_error("mmio: invalid slot");
    }

    if (dev->size() > ((u64)1 << address_bits)) {
        runtime_error("mmio: device size exceeds slot size");
    }

    devices[slot] = dev;
}

bool mmio::load(u32 addr, u32 len, u8* data)
{
    u32 slot = get_part(addr, address_bits + slot_bits - 1, address_bits);
    addr = get_part(addr, address_bits - 1, 0);

    mmio_device* dev = devices[slot];
    if (dev) {
        return dev->load(addr, len, data);
    } else {
        return false;
    }
}

bool mmio::store(u32 addr, u32 len, const u8* data)
{
    u32 slot = get_part(addr, address_bits + slot_bits - 1, address_bits);
    addr = get_part(addr, address_bits - 1, 0);

    mmio_device* dev = devices[slot];
    if (dev) {
        return dev->store(addr, len, data);
    } else {
        return false;
    }
}

u64 mmio::size() const {
    return (u64)1 << (address_bits + slot_bits);
}

u64 mmio::next_deadline_ns()
{
    u64 deadline = NO_DEADLINE;
    for (auto& device : devices) {
        if (device) {
            u64 d = device->next_deadline_ns();
            if (d < deadline) {
                deadline = d;
            }
        }
    }
    return deadline;
}

void mmio::host_fds(std::vector<host_fd>& out)
{
    for (auto& device : devices) {
        if (device) {
            device->host_fds(out);
        }
    }
}

void mmio::tick()
{
    for (auto& device : devices) {
        if (device) {
            device->tick();
        }
    }
}