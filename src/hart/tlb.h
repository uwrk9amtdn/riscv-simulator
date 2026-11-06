#pragma once

#include <cstdint>
#include <optional>
#include <vector>
#include <common.h>

struct TLBEntry {
    u32  vpn;   // 4 KiB virtual page number (va[31:12]). Superpages are stored split into 4 KiB pieces
    u32  ppn;   // 4 KiB physical page number of this piece
    u32  asid;
    u32  flags; // daguxwrv (pte[7:0])
    bool super; // the piece comes from a 4 MiB superpage, sfence by va must drop all pieces of it
};

struct TLBStats {
    u64 hits   = 0;
    u64 misses = 0;
};

class TLB {
public:
    virtual ~TLB() = default;

    virtual std::optional<TLBEntry> lookup(u32 va, u32 asid) const = 0;

    // replaces an existing entry for the same page and address space
    virtual void insert(const TLBEntry& entry) = 0;

    // SFENCE.VMA: nullopt represents x0.
    virtual void invalidate(
        std::optional<u32> va = {},
        std::optional<u32> asid = {}
    ) = 0;

    virtual void flush() = 0;

    const TLBStats& stats() const { return stats_; }

protected:
    mutable TLBStats stats_;
};

namespace tlb_detail {

constexpr u32 FLAG_G = 1u << 5;

struct slot {
    TLBEntry e;
    bool valid = false;
};

inline bool is_pow2(u32 n) { return n != 0 && (n & (n - 1)) == 0; }

inline bool matches(const slot& s, u32 vpn, u32 asid) {
    return s.valid && s.e.vpn == vpn && ((s.e.flags & FLAG_G) || s.e.asid == asid);
}

// same page and same address space (used to replace instead of duplicate on insert)
inline bool same_mapping(const slot& s, const TLBEntry& e) {
    return s.valid && s.e.vpn == e.vpn && (((s.e.flags | e.flags) & FLAG_G) || s.e.asid == e.asid);
}

// storage + the SFENCE logic shared by every organisation. SFENCE is rare, so invalidation always scans
class array_tlb : public TLB {
public:
    void invalidate(std::optional<u32> va, std::optional<u32> asid) override {
        for (auto& s : slots) {
            if (!s.valid) {
                continue;
            }
            if (asid && ((s.e.flags & FLAG_G) || s.e.asid != *asid)) {
                continue; // an ASID-specific fence keeps global mappings and other address spaces
            }
            if (va) {
                u32 vpn = *va >> 12;
                bool hit = s.e.super ? (s.e.vpn >> 10) == (vpn >> 10) : s.e.vpn == vpn;
                if (!hit) {
                    continue;
                }
            }
            s.valid = false;
        }
    }

    void flush() override {
        for (auto& s : slots) {
            s.valid = false;
        }
    }

protected:
    explicit array_tlb(u32 n) : slots(n) {
        if (!is_pow2(n)) {
            runtime_error("tlb: size must be a power of two");
        }
    }

    std::optional<TLBEntry> hit(const slot& s) const {
        stats_.hits++;
        return s.e;
    }

    std::optional<TLBEntry> miss() const {
        stats_.misses++;
        return std::nullopt;
    }

    std::vector<slot> slots;
};

} // namespace tlb_detail

// every constructor takes (entries, ways) so the hart can build any of them the same way

class NullTLB final : public TLB {
public:
    static constexpr const char* kind = "null"; // --tlb name
    NullTLB(u32 = 0, u32 = 0) {}
    std::optional<TLBEntry> lookup(u32, u32) const override { stats_.misses++; return std::nullopt; }
    void insert(const TLBEntry&) override {}
    void invalidate(std::optional<u32>, std::optional<u32>) override {}
    void flush() override {}
};

class DirectMappedTLB final : public tlb_detail::array_tlb {
public:
    static constexpr const char* kind = "dm"; // --tlb name
    DirectMappedTLB(u32 entries, u32 = 1) : array_tlb(entries), mask(entries - 1) {}

    std::optional<TLBEntry> lookup(u32 va, u32 asid) const override {
        u32 vpn = va >> 12;
        const auto& s = slots[vpn & mask];
        return tlb_detail::matches(s, vpn, asid) ? hit(s) : miss();
    }

    void insert(const TLBEntry& e) override {
        slots[e.vpn & mask] = {e, true};
    }

private:
    u32 mask;
};

class SetAssocTLB final : public tlb_detail::array_tlb {
public:
    static constexpr const char* kind = "sa"; // --tlb name
    SetAssocTLB(u32 entries, u32 ways)
        : array_tlb(entries), ways(ways), set_mask(entries / ways - 1), next(entries / ways, 0) {
        if (!tlb_detail::is_pow2(ways) || ways > entries) {
            runtime_error("tlb: ways must be a power of two and not more than the entries");
        }
    }

    std::optional<TLBEntry> lookup(u32 va, u32 asid) const override {
        u32 vpn = va >> 12;
        const tlb_detail::slot* set = &slots[(vpn & set_mask) * ways];
        for (u32 w = 0; w < ways; w++) {
            if (tlb_detail::matches(set[w], vpn, asid)) {
                return hit(set[w]);
            }
        }
        return miss();
    }

    void insert(const TLBEntry& e) override {
        u32 index = e.vpn & set_mask;
        tlb_detail::slot* set = &slots[index * ways];

        for (u32 w = 0; w < ways; w++) {
            if (tlb_detail::same_mapping(set[w], e)) {
                set[w] = {e, true};
                return;
            }
        }
        for (u32 w = 0; w < ways; w++) {
            if (!set[w].valid) {
                set[w] = {e, true};
                return;
            }
        }
        set[next[index]] = {e, true};
        next[index] = (next[index] + 1) & (ways - 1); // round-robin victim
    }

private:
    u32 ways;
    u32 set_mask;
    std::vector<u32> next;
};

class FullyAssocTLB final : public tlb_detail::array_tlb {
public:
    static constexpr const char* kind = "fa"; // --tlb name
    FullyAssocTLB(u32 entries, u32 = 0) : array_tlb(entries) {}

    std::optional<TLBEntry> lookup(u32 va, u32 asid) const override {
        u32 vpn = va >> 12;
        if (tlb_detail::matches(slots[mru], vpn, asid)) {
            return hit(slots[mru]);
        }
        for (u32 i = 0; i < slots.size(); i++) {
            if (tlb_detail::matches(slots[i], vpn, asid)) {
                mru = i;
                return hit(slots[i]);
            }
        }
        return miss();
    }

    void insert(const TLBEntry& e) override {
        for (u32 i = 0; i < slots.size(); i++) {
            if (tlb_detail::same_mapping(slots[i], e)) {
                slots[i] = {e, true};
                mru = i;
                return;
            }
        }
        // round-robin victim. After a flush every slot is invalid, so it fills them in order anyway
        slots[next] = {e, true};
        mru = next;
        next = (next + 1) & (slots.size() - 1);
    }

private:
    mutable u32 mru = 0;
    u32 next = 0;
};
