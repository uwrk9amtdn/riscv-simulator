#pragma once

#include "mmio_device.h"

#include <poll.h>
#include <vector>

// Host side of the WFI sleep. While every hart is parked, the machine only changes when a device
// fd becomes readable (host input) or a device deadline passes (timer, UART character timeout).
// Both come from the device tree under root, so a new device only has to implement host_fds(),
// poll_input() and next_deadline_ns()
class host_io {
public:
    explicit host_io(mmio_device* root);

    // sleep in the kernel until a device fd is readable or the earliest device deadline passes, then
    // let the devices with readable fds take their input. Returns early when a signal arrives
    void wait_until_event();

private:
    mmio_device* root;
    std::vector<host_fd> fds;  // reused between calls
    std::vector<pollfd> pfds;
};
