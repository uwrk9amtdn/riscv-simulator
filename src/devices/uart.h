#pragma once

#include "mmio_device.h"

#include "plic.h"

#include <queue>
#include <termios.h>

class uart : public mmio_device {

public:
    uart(const char* tty, plic* plic, u32 interrupt_id);
    // open tty (or put stdin in non-canonical, no-echo mode) for the guest's console; the destructor undoes it.
    // Separate from the constructor so building the machine (e.g. only to write its device tree) needs no terminal
    void attach_terminal();
    bool load(u32 addr, u32 len, u8* data) override;
    bool store(u32 addr, u32 len, const u8* data) override;
    u64 size() const override;
    void tick() override;
    u64 next_deadline_ns() override;

    void host_fds(std::vector<host_fd>& out) override; // the input fd while the receiver can take a byte
    void poll_input() override;                        // move pending input into the receiver, never blocks
    std::string fdt_node_name(u32 base) const override;
    void fdt_props(fdt_builder& b, u32 base) const override;
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

    const char* tty;
    int fd;
    bool custom_tty = false;
    termios old_tios;

    bool rx_timed_out = false; // character timeout indication
    u64 rx_deadline_ns = 0;    // host time at which the character timeout fires
    u64 last_poll_ns = 0;
    bool rx_eof = false;

    bool can_rx() const;

    bool interrupt_pre = false;
};