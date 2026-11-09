#pragma once

#include <vector>

#include "mmio_device.h"
#include "hart.h"

// a PLIC context: one interrupt line of one hart
class hart_context {
public:
    explicit hart_context(hart* h) : h(h) {}
    virtual ~hart_context() = default;
    virtual void set_external_interrupt(bool level) = 0;
    // the line in the hart's interrupt controller, for interrupts-extended in the device tree: MEIP 11, SEIP 9
    virtual u32 line() const = 0;
    hart* get_hart() const { return h; }

protected:
    hart* h;
};

class m_context : public hart_context {
public:
    using hart_context::hart_context;
    void set_external_interrupt(bool level) override { h->set_meip(level); }
    u32 line() const override { return 11; }
};

class s_context : public hart_context {
public:
    using hart_context::hart_context;
    void set_external_interrupt(bool level) override { h->set_seip(level); }
    u32 line() const override { return 9; }
};

class plic : public mmio_device {

public:
    plic(std::vector<hart_context*> hart_contexts, u32 num_sources);

    bool load(u32 addr, u32 len, u8* data) override;
    bool store(u32 addr, u32 len, const u8* data) override;
    u64 size() const override;
    void tick() override;
    std::string fdt_node_name(u32 base) const override;
    void fdt_props(fdt_builder& b, u32 base) const override; // phandle: b.phandle(this)
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