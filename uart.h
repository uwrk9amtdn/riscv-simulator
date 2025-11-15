#pragma once

#include "mmio_device.h"

#include "plic.h"

#include <queue>
#include <termios.h>

class uart : public mmio_device {

public:
    uart(const char* tty, plic* plic, u32 interrupt_id);
    bool load(u32 addr, u32 len, u8* data) override;
    bool store(u32 addr, u32 len, const u8* data) override;
    u32 size() const override;
    void tick() override;
    ~uart() override;

private:

    void update_interrupt();

    u8 rbr;
    u8 thr;
    u8 ier;
    u8 iir;
    u8 fcr;
    u8 lcr;
    u8 mcr;
    u8 lsr;
    u8 msr;
    u8 scr;
    u8 dll;
    u8 dlm;

    std::queue<u8> rx_queue;

    plic* plic_;
    u32 interrupt_id;

    int fd;
    bool custom_tty = false;
    termios old_tios;

    int timeout_tick_count = 0;
};