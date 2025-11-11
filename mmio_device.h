#pragma once

#include "common.h"

class mmio_device {
public:
    virtual bool load(u32 addr, u32 len, u8* data) = 0;
    virtual bool store(u32 addr, u32 len, const u8* data) = 0;
    virtual u32 size() const = 0;
    virtual void tick() = 0;
    virtual ~mmio_device();
};

inline mmio_device::~mmio_device() { }