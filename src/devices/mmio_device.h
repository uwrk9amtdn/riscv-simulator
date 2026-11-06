#pragma once

#include "common.h"

#include <vector>

class mmio_device;

// a host file descriptor a device wants to be woken by, and the device that reads it
struct host_fd {
    int fd;
    mmio_device* device;
};

class mmio_device {
public:
    virtual bool load(u32 addr, u32 len, u8* data) = 0;
    virtual bool store(u32 addr, u32 len, const u8* data) = 0;
    virtual u64 size() const = 0;
    virtual void tick() = 0;
    // host time (ns) at which the device changes state on its own, NO_DEADLINE if it never does.
    // Lets the main loop sleep exactly that long while the hart is parked in WFI
    virtual u64 next_deadline_ns() { return NO_DEADLINE; }
    // host fds the device currently waits on (input from the host side). Leave out an fd while the
    // device can't take input: it would stay readable, and the WFI sleep would turn into a busy loop
    virtual void host_fds(std::vector<host_fd>& out) { (void)out; }
    // one of the device's fds is readable: take what it can, never block
    virtual void poll_input() {}

    // a device calls this when its next_deadline_ns() may have moved earlier or its host_fds() changed
    // (e.g. a CLINT mtimecmp write, a UART RBR read). With threads, the I/O thread may be asleep with an old
    // timeout or fd set and must recompute them. host_io sets the hook; without threads it stays empty
    static inline void (*host_changed_hook)(void*) = nullptr;
    static inline void* host_changed_ctx = nullptr;
    static void notify_host_changed()
    {
        if (host_changed_hook) {
            host_changed_hook(host_changed_ctx);
        }
    }
    virtual ~mmio_device() {};
};