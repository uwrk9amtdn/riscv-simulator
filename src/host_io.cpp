#include "host_io.h"

host_io::host_io(mmio_device* root)
{
    this->root = root;
}

void host_io::wait_until_event()
{
    fds.clear();
    root->host_fds(fds);

    pfds.resize(fds.size());
    for (size_t i = 0; i < fds.size(); i++) {
        pfds[i].fd = fds[i].fd;
        pfds[i].events = POLLIN;
        pfds[i].revents = 0;
    }

    u64 now = host_now_ns();
    u64 deadline = root->next_deadline_ns();

    timespec ts;
    timespec* timeout = nullptr; // no deadline: only input can wake the machine

    if (deadline != NO_DEADLINE) {
        u64 d = deadline > now ? deadline - now : 0;
        ts.tv_sec = d / 1000000000ull;
        ts.tv_nsec = d % 1000000000ull;
        timeout = &ts;
    }

    if (ppoll(pfds.data(), pfds.size(), timeout, nullptr) > 0) {
        for (size_t i = 0; i < fds.size(); i++) {
            if (pfds[i].revents) { // POLLIN, or POLLHUP/POLLERR: the device sees the end of input on read
                fds[i].device->poll_input();
            }
        }
    }
}
