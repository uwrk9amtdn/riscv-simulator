#pragma once

#include "common.h"
#include "mmio_device.h"
#include <vector>

class mmio : public mmio_device {
public:
    mmio(int address_bits, int slot_bits);

    void add_device(mmio_device* dev, u32 slot);

public:
    bool load(u32 addr, u32 len, u8* data) override;
    bool store(u32 addr, u32 len, const u8* data) override;
    u64 size() const override;
    void tick() override;

private:
    std::vector<mmio_device*> devices;
    int slot_bits;
    int address_bits;
};