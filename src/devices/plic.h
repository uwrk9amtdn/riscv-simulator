#pragma once

#include <vector>

#include "mmio_device.h"
#include "hart.h"

class hart_context {
public:
    virtual ~hart_context() = default;
    virtual void set_external_interrupt(bool level) = 0;
};

class machine_context : public hart_context {
public:
    machine_context(hart* h)
    {
        this->h = h;
    }
    void set_external_interrupt(bool level) override
    {
        h->set_meip(level);
    }
    ~machine_context() override { }

private:
    hart* h;
};

class plic : public mmio_device {

public:
    plic(std::vector<hart_context*> hart_contexts, u32 num_sources);

    bool load(u32 addr, u32 len, u8* data) override;
    bool store(u32 addr, u32 len, const u8* data) override;
    u32 size() const override;
    void tick() override;
    ~plic() override;

    void set_interrupt_signal(u32 id, bool level);

private:
    u32 num_ids;

    std::vector<u32> priority;
    std::vector<u32> interrupt_signal;
    std::vector<u32> busy;
    std::vector<u32> pending;

    struct context_t {
        std::vector<u32> enable;
        u32 threshold;
        u32 claim_complete;
        hart_context* hcontext;
    };

    std::vector<context_t> context;

    void update_pending();
    void update_context();
};