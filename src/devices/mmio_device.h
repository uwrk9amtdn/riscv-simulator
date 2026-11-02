#pragma once

#include "common.h"

class mmio_device {
public:
    virtual bool load(u32 addr, u32 len, u8* data) = 0;
    virtual bool store(u32 addr, u32 len, const u8* data) = 0;
    virtual u64 size() const = 0;
    virtual void tick() = 0;
    // host time (ns) at which the device changes state on its own, NO_DEADLINE if it never does.
    // Lets the main loop sleep exactly that long while the hart is parked in WFI
    virtual u64 next_deadline_ns() { return NO_DEADLINE; }
    virtual ~mmio_device() {};
};