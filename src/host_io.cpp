#include "host_io.h"

#include <algorithm>

#include <sys/eventfd.h>
#include <unistd.h>

host_io::host_io(mmio* root)
{
    this->root = root;
    kick_fd = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
    if (kick_fd < 0) {
        runtime_error("eventfd failed");
    }
}

host_io::~host_io()
{
    if (mmio_device::host_changed_ctx == this) {
        mmio_device::host_changed_hook = nullptr;
        mmio_device::host_changed_ctx = nullptr;
    }
    close(kick_fd);
}

void host_io::set_threaded(const sigset_t* ppoll_mask)
{
    this->ppoll_mask = ppoll_mask;
    mmio_device::host_changed_ctx = this;
    mmio_device::host_changed_hook = [](void* io) { ((host_io*)io)->kick(); };
}

void host_io::kick()
{
    u64 one = 1;
    [[maybe_unused]] ssize_t r = write(kick_fd, &one, sizeof(one)); // only fails if the counter is full, then a wake is pending anyway
}

static constexpr u64 MAX_SLEEP_NS = 1000000000ull; // see wait_until_event

void host_io::wait_until_event()
{
    u64 deadline;
    {
        std::lock_guard<std::mutex> lock(root->lock);
        fds.clear();
        root->host_fds(fds);
        // sleep at most MAX_SLEEP_NS: the armed deadline is then never far away, so the transient far values of a
        // 32-bit split mtimecmp write (hi or lo still old, ~429 s away) don't kick. Costs one wake per second idle
        deadline = std::min(root->next_deadline_ns(), host_now_ns() + MAX_SLEEP_NS);
        mmio_device::armed_deadline_ns.store(deadline, std::memory_order_relaxed); // devices kick only if earlier
    }

    pfds.resize(fds.size() + 1);
    for (size_t i = 0; i < fds.size(); i++) {
        pfds[i].fd = fds[i].fd;
        pfds[i].events = POLLIN;
        pfds[i].revents = 0;
    }
    pfds[fds.size()] = {kick_fd, POLLIN, 0};

    u64 now = host_now_ns();

    timespec ts;
    timespec* timeout = nullptr; // no deadline: only input or a kick can wake the machine

    if (deadline != NO_DEADLINE) {
        u64 d = deadline > now ? deadline - now : 0;
        ts.tv_sec = d / 1000000000ull;
        ts.tv_nsec = d % 1000000000ull;
        timeout = &ts;
    }

    int n = ppoll(pfds.data(), pfds.size(), timeout, ppoll_mask);
    // awake: the next call recomputes the deadline under the lock and sees every change made from now on
    mmio_device::armed_deadline_ns.store(0, std::memory_order_relaxed);
    if (n > 0) {
        if (pfds[fds.size()].revents) {
            u64 count;
            [[maybe_unused]] ssize_t r = read(kick_fd, &count, sizeof(count));
        }
        std::lock_guard<std::mutex> lock(root->lock);
        for (size_t i = 0; i < fds.size(); i++) {
            if (pfds[i].revents) { // POLLIN, or POLLHUP/POLLERR: the device sees the end of input on read
                fds[i].device->poll_input();
            }
        }
    }
}
