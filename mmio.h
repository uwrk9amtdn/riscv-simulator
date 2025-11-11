#pragma once

#include "common.h"
#include "mmio_device.h"
#include <vector>
#include <memory>

class mmio {
public:
    mmio(int slot_bits);

    void add_device(mmio_device* dev, u32 slot);

    bool load(u32 addr, u32 len, u8* data);
    bool store(u32 addr, u32 len, const u8* data);
    void tick();

private:
    std::vector<mmio_device*> devices;
    int slot_bits;
};