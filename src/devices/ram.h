#pragma once

#include "mmio_device.h"

class ram : public mmio_device {
    public:
        ram(u32 size);
        bool load(u32 addr, u32 len, u8* data) override;
        bool store(u32 addr, u32 len, const u8* data) override;
        u64 size() const override;
        // backing buffer, so a hart can access RAM directly without the virtual bus call
        u8* host_ptr() { return data; }
        void tick() override;
        std::string fdt_node_name(u32 base) const override;
        void fdt_props(fdt_builder& b, u32 base) const override;
        bool fdt_at_root() const override { return true; }
        ~ram() override;
    private:
        u32 size_;
        u8* data; // anonymous mapping: zero pages, committed only when the guest touches them
};
