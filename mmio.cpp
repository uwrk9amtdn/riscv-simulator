#include "mmio.h"
#include <stdexcept>

mmio::mmio(int slot_bits)
{
    this->slot_bits = slot_bits;
    devices.resize(1 << slot_bits, 0);
}

void mmio::add_device(mmio_device* dev, u32 slot)
{
    if (slot >= devices.size()) {
        throw std::runtime_error("mmio: invalid slot");
    }

    if (dev->size() > (1 << (32 - slot_bits))) {
        throw std::runtime_error("mmio: device size exceeds slot size");
    }

    devices[slot] = dev;
}

bool mmio::load(u32 addr, u32 len, u8* data)
{
    u32 slot = addr >> (32 - slot_bits);
    addr = (addr << slot_bits) >> slot_bits;

    mmio_device* dev = devices[slot];
    if (dev) {
        return dev->load(addr, len, data);
    } else {
        return false;
    }
}

bool mmio::store(u32 addr, u32 len, const u8* data)
{
    u32 slot = addr >> (32 - slot_bits);
    addr = (addr << slot_bits) >> slot_bits;

    mmio_device* dev = devices[slot];
    if (dev) {
        return dev->store(addr, len, data);
    } else {
        return false;
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