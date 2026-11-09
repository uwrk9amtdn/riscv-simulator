#pragma once

#include "common.h"
#include "mmio_device.h"
#include "hart.h"
#include <atomic>
#include <vector>

class clint : public mmio_device {
public:
    clint(std::vector<hart*> harts);

    bool load(u32 addr, u32 len, u8* data) override;
    bool store(u32 addr, u32 len, const u8* data) override;
    u64 size() const override;
    void tick() override;
    u64 next_deadline_ns() override;
    std::string fdt_node_name(u32 base) const override;
    void fdt_props(fdt_builder& b, u32 base) const override;
    ~clint() override;

    // mtime frequency, also timebase-frequency in the device tree
    static constexpr u32 TIMEBASE_HZ = 10000000;

    // current mtime. Also called by harts for the time CSR, without the device lock
    u64 mtime_now() const;

private:
    std::vector<u32> msip;
    std::vector<u64> mtimecmp;

    u64 time0_ns;         // host time at power on
    std::atomic<u64> mtime_offset{0}; // guest writes to mtime are kept as an offset from the host clock

    u64 timer_deadline_ns(u32 i, u64 now_ns) const;

    std::vector<hart*> harts;
};