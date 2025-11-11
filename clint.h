#pragma once

#include "common.h"
#include "mmio_device.h"
#include "hart.h"
#include <vector>

class clint : public mmio_device {
public:
    clint(std::vector<hart*> harts, u32 tick_freq = 1000000);

    bool load(u32 addr, u32 len, u8* data) override;
    bool store(u32 addr, u32 len, const u8* data) override;
    u32 size() const override;
    void tick() override;
    ~clint() override;

private:
    std::vector<u32> msip;
    std::vector<u64> mtimecmp;

    u64 mtime;

    std::vector<hart*> harts;

    u32 tick_freq;
    u64 tick_counter = 0;
};