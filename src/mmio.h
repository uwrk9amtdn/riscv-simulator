#pragma once

#include "common.h"
#include "mmio_device.h"
#include <mutex>
#include <vector>

class mmio : public mmio_device {
public:
    mmio(int address_bits, int slot_bits);

    void add_device(mmio_device* dev, u32 slot);

    // the root bus's lock serializes all device access: a hart's MMIO load/store, host_io's input and
    // deadline handling, and tick(). Devices call each other (UART -> PLIC -> hart lines), so one lock for
    // all of them avoids lock-order problems. MMIO is rare, so it is cheap. Not taken inside mmio itself,
    // because the root bus calls the nested buses
    std::mutex lock;

public:
    bool load(u32 addr, u32 len, u8* data) override;
    bool store(u32 addr, u32 len, const u8* data) override;
    u64 size() const override;
    void tick() override;
    u64 next_deadline_ns() override;
    void host_fds(std::vector<host_fd>& out) override;

private:
    std::vector<mmio_device*> devices;
    int slot_bits;
    int address_bits;
};