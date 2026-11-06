#pragma once

#include "mmio.h"

#include <csignal>
#include <poll.h>
#include <vector>

// Host side of the WFI sleep. While every hart is parked, the machine only changes when a device
// fd becomes readable (host input) or a device deadline passes (timer, UART character timeout).
// Both come from the device tree under root, so a new device only has to implement host_fds(),
// poll_input() and next_deadline_ns().
//
// With threads, an I/O thread calls wait_until_event() in a loop while harts run. A device whose
// deadline or fds change calls mmio_device::notify_host_changed(), which kicks the I/O thread out of
// ppoll so it recomputes them. All device access happens under root->lock
class host_io {
public:
    explicit host_io(mmio* root);
    ~host_io();

    // sleep in the kernel until a device fd is readable, the earliest device deadline passes or kick()
    // is called, then let the devices with readable fds take their input. Returns early on a signal
    void wait_until_event();

    // wake wait_until_event() from another thread
    void kick();

    // threaded mode: device changes kick the I/O thread. ppoll_mask (if not null) is the signal mask while
    // sleeping, so a signal that is blocked otherwise (SIGUSR1) can only arrive in ppoll and end it, with no
    // race between checking a stop flag and going to sleep
    void set_threaded(const sigset_t* ppoll_mask);

private:
    mmio* root;
    int kick_fd;
    const sigset_t* ppoll_mask = nullptr;
    std::vector<host_fd> fds;  // reused between calls
    std::vector<pollfd> pfds;
};
