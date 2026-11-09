#include "clint.h"
#include <stdexcept>

#define MSIP_BASE     0x0000
#define MTIMECMP_BASE 0x4000
#define MTIME_BASE    0xbff8

#define NS_PER_MTIME 100 // mtime runs at 10 MHz (timebase-frequency in the dts)

clint::clint(std::vector<hart*> harts)
{
    this->harts = harts;

    time0_ns = host_now_ns();
    msip.resize(harts.size(), 0);
    mtimecmp.resize(harts.size(), 0);
}

bool clint::load(u32 addr, u32 len, u8* data)
{

    u32 val32;
    u64 val64;
    u64 d;

    if (data == 0) return false;

    if (addr >= MSIP_BASE && addr < MTIMECMP_BASE) {
        if (len != 4 || (addr & 0x3) != 0) return false;
        u32 hart_id = (addr - MSIP_BASE) >> 2;
        if (hart_id < msip.size()) {
            val32 = msip[hart_id];
        } else {
            val32 = 0;
        }
        ((u32*)data)[0] = val32;
        return true;
    }

    if (addr >= MTIMECMP_BASE && addr < MTIME_BASE) {
        if (len == 8) {
            if ((addr & 0x7) != 0) return false;
        } else if (len == 4) {
            if ((addr & 0x3) != 0) return false;
        } else {
            return false;
        }

        u32 hart_id = (addr - MTIMECMP_BASE) >> 3;
        if (hart_id < mtimecmp.size()) {
            d = mtimecmp[hart_id];
        } else {
            d = 0;
        }

        if (len == 8) {
            val64 = d;
            ((u64*)data)[0] = val64;
        } else {
            if ((addr - MTIMECMP_BASE) & 0x4) {
                val32 = d >> 32;
            } else {
                val32 = d;
            }
            ((u32*)data)[0] = val32;
        }

        return true;
    }

    if (addr >= MTIME_BASE && addr < MTIME_BASE + 8) {

        if (len == 8) {
            if ((addr & 0x7) != 0) return false;
        } else if (len == 4) {
            if ((addr & 0x3) != 0) return false;
        } else {
            return false;
        }

        d = mtime_now();

        if (len == 8) {
            val64 = d;
            ((u64*)data)[0] = val64;
        } else {
            if ((addr - MTIME_BASE) & 0x4) {
                val32 = d >> 32;
            } else {
                val32 = d;
            }
            ((u32*)data)[0] = val32;
        }

        return true;
    }

    return false;
}

bool clint::store(u32 addr, u32 len, const u8* data)
{

    u32 val32;
    u64 val64;

    if (data == 0) return false;

    if (addr >= MSIP_BASE && addr < MTIMECMP_BASE) {
        if (len != 4 || (addr & 0x3) != 0) return false;
        u32 hart_id = (addr - MSIP_BASE) >> 2;
        if (hart_id < msip.size()) {
            msip[hart_id] = ((u32*)data)[0] & 1;
            if (msip[hart_id]) {
                harts[hart_id]->set_msip(1);
            } else {
                harts[hart_id]->set_msip(0);
            }
        }
        return true;
    }

    if (addr >= MTIMECMP_BASE && addr < MTIME_BASE) {

        if (len == 8) {
            if ((addr & 0x7) != 0) return false;
        } else if (len == 4) {
            if ((addr & 0x3) != 0) return false;
        } else {
            return false;
        }

        u32 hart_id = (addr - MTIMECMP_BASE) >> 3;

        if (hart_id < mtimecmp.size()) {
            if (len == 8) {
                val64 = ((u64*)data)[0];
                mtimecmp[hart_id] = val64;
            } else {
                val32 = ((u32*)data)[0];
                if ((addr - MTIMECMP_BASE) & 0x4) {
                    mtimecmp[hart_id] = (mtimecmp[hart_id] & 0x00000000ffffffff) | ((u64)val32 << 32);
                } else {
                    mtimecmp[hart_id] = (mtimecmp[hart_id] & 0xffffffff00000000) | val32;
                }
            }

            if (mtimecmp[hart_id] <= mtime_now()) {
                harts[hart_id]->set_mtip(1);
            } else {
                harts[hart_id]->set_mtip(0);
            }
            notify_host_changed(); // the next timer deadline may be earlier now
        }

        return true;
    }

    if (addr >= MTIME_BASE && addr < MTIME_BASE + 8) {
        if (len == 8) {
            if ((addr & 0x7) != 0) return false;
        } else if (len == 4) {
            if ((addr & 0x3) != 0) return false;
        } else {
            return false;
        }

        u64 cur = mtime_now();
        u64 mtime = cur;

        if (len == 8) {
            val64 = ((u64*)data)[0];
            mtime = val64;
        } else {
            val32 = ((u32*)data)[0];
            if ((addr - MTIME_BASE) & 0x4) {
                mtime = (mtime & 0x00000000ffffffff) | ((u64)val32 << 32);
            } else {
                mtime = (mtime & 0xffffffff00000000) | val32;
            }
        }

        mtime_offset.fetch_add(mtime - cur, std::memory_order_relaxed);
        notify_host_changed();

        for (u32 i = 0; i < harts.size(); i++) {
            if (mtimecmp[i] <= mtime) {
                harts[i]->set_mtip(1);
            } else {
                harts[i]->set_mtip(0);
            }
        }

        return true;
    }

    return false;
}

u64 clint::size() const
{
    return 0xc000;
}

u64 clint::mtime_now() const
{
    return (host_now_ns() - time0_ns) / NS_PER_MTIME + mtime_offset.load(std::memory_order_relaxed);
}

u64 clint::next_deadline_ns()
{
    u64 now_ns = host_now_ns();
    u64 mtime = (now_ns - time0_ns) / NS_PER_MTIME + mtime_offset.load(std::memory_order_relaxed);
    u64 deadline = NO_DEADLINE;

    for (u32 i = 0; i < harts.size(); i++) {
        if (mtimecmp[i] <= mtime) {
            continue; // already pending, nothing changes by itself
        }
        u64 delta = mtimecmp[i] - mtime;
        if (delta > ((u64)1 << 50)) {
            continue; // effectively never (e.g. mtimecmp = -1). Also keeps the multiplication from overflowing
        }
        u64 d = now_ns + delta * NS_PER_MTIME;
        if (d < deadline) {
            deadline = d;
        }
    }

    return deadline;
}

void clint::tick()
{
    u64 mtime = mtime_now();

    for (u32 i = 0; i < harts.size(); i++) {
        if (mtimecmp[i] <= mtime) {
            harts[i]->set_mtip(1);
        } else {
            harts[i]->set_mtip(0);
        }
    }
}

clint::~clint()
{
}
