#pragma once

#include "mmio_device.h"
#include <memory>

class ram : public mmio_device {
    public:
        ram(u32 size);
        bool load(u32 addr, u32 len, u8* data) override;
        bool store(u32 addr, u32 len, const u8* data) override;
        u32 size() const override;
        void tick() override;
        ~ram() override;
    private:
        u32 size_;
        std::unique_ptr<u8[]> data;
};