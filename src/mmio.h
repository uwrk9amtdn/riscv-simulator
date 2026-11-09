#pragma once

#include "common.h"
#include "mmio_device.h"
#include <mutex>
#include <vector>

// the physical address space: devices mapped at a base address, each covering [base, base + size()).
// Harts reach RAM through their own host pointer and only come here for MMIO (and the loader for the images),
// so a short list is fast enough
class mmio : public mmio_device {
public:
    struct mapping {
        u32 base;
        u64 size;
        mmio_device* dev;
    };

    // map dev at base; it must not overlap another device or wrap the 32-bit address space
    void add_device(mmio_device* dev, u32 base);
    const std::vector<mapping>& mappings() const { return devices; } // sorted by base

    // the root bus's lock serializes all device access: a hart's MMIO load/store, host_io's input and
    // deadline handling, and tick(). Devices call each other (UART -> PLIC -> hart lines), so one lock for
    // all of them avoids lock-order problems. MMIO is rare, so it is cheap. Not taken inside mmio itself
    std::mutex lock;

public:
    bool load(u32 addr, u32 len, u8* data) override;
    bool store(u32 addr, u32 len, const u8* data) override;
    u64 size() const override;
    void tick() override;
    u64 next_deadline_ns() override;
    void host_fds(std::vector<host_fd>& out) override;

private:
    // the device that holds all of [addr, addr + len), nullptr if none (an access across two devices fails)
    const mapping* find(u32 addr, u32 len) const;

    std::vector<mapping> devices;
};
