#include "plic.h"

#define PRIORITY_BASE 0x00000000
#define PENDING_BASE  0x00001000
#define ENABLE_BASE   0x00002000
#define CONTEXT_BASE  0x00200000
#define END_ADDRESS   0x04000000

#define PRIORITY_BITS 4

plic::plic(std::vector<hart_context*> hart_contexts, u32 num_sources)
{
    num_ids = num_sources + 1;

    u32 num_words = (num_ids + 31) >> 5;

    priority.resize(num_ids, 0);
    interrupt_signal.resize(num_words, 0);
    busy.resize(num_words, 0);
    pending.resize(num_words, 0);

    context.resize(hart_contexts.size());

    for (u32 i = 0; i < context.size(); i++) {
        context[i].enable.resize(num_words, 0);
        context[i].threshold = 0;
        context[i].claim_complete = 0;
        context[i].hcontext = hart_contexts[i];
    }
}

bool plic::load(u32 addr, u32 len, u8* data)
{
    if (len != 4 || (addr & 0x3) != 0 || data == 0) return false;

    u32 val;

    if (addr >= PRIORITY_BASE && addr < PENDING_BASE) {
        u32 id = (addr - PRIORITY_BASE) >> 2;
        if (id < priority.size()) {
            val = priority[id];
        } else {
            val = 0;
        }
        ((u32*)data)[0] = val;
        return true;
    }

    if (addr >= PENDING_BASE && addr < ENABLE_BASE) {
        u32 id = (addr - PENDING_BASE) >> 2;
        if (id < pending.size()) {
            val = pending[id];
        } else {
            val = 0;
        }
        ((u32*)data)[0] = val;
        return true;
    }

    if (addr >= ENABLE_BASE && addr < CONTEXT_BASE) {
        u32 context_id = (addr - ENABLE_BASE) >> 7;
        u32 enable_id = ((addr - ENABLE_BASE) & 0x7f) >> 2;
        if (context_id < context.size()) {
            if (enable_id < context[context_id].enable.size()) {
                val = context[context_id].enable[enable_id];
            } else {
                val = 0;
            }
        } else {
            val = 0;
        }
        ((u32*)data)[0] = val;
        return true;
    }

    if (addr >= CONTEXT_BASE && addr < END_ADDRESS) {
        u32 context_id = (addr - CONTEXT_BASE) >> 12;
        u32 offset = (addr - CONTEXT_BASE) & 0xfff;
        u32 id;
        u32 id_word;
        u32 id_mask;

        if (context_id < context.size()) {
            switch (offset) {
            case 0: // Threshold
                val = context[context_id].threshold;
                break;
            case 4: // Claim/Complete
                val = context[context_id].claim_complete;
                id = context[context_id].claim_complete;
                id_word = id >> 5;
                id_mask = 1 << (id & 0x1f);
                pending[id_word] &= ~id_mask;
                busy[id_word] |= id_mask;
                update_pending();
                update_context();
                break;
            default:
                val = 0;
            }
        } else {
            val = 0;
        }
        ((u32*)data)[0] = val;
        return true;
    }

    return false;
}

bool plic::store(u32 addr, u32 len, const u8* data)
{
    if (len != 4 || (addr & 0x03) != 0 || data == 0) return false;

    u32 val = ((u32*)data)[0];

    if (addr >= PRIORITY_BASE && addr < PENDING_BASE) {
        u32 id = (addr - PRIORITY_BASE) >> 2;
        if (id > 0 && id < num_ids) {
            priority[id] = val & ((1 << PRIORITY_BITS) - 1);
            update_context();
        }

        return true;
    }

    if (addr >= PENDING_BASE && addr < ENABLE_BASE) {
        return true;
    }

    if (addr >= ENABLE_BASE && addr < CONTEXT_BASE) {
        u32 context_id = (addr - ENABLE_BASE) >> 7;
        u32 enable_id = ((addr - ENABLE_BASE) & 0x7f) >> 2;
        if (context_id < context.size()) {
            if (enable_id < context[context_id].enable.size()) {
                context[context_id].enable[enable_id] = val;
                update_context(); // TODO: update only modified context
            }
        }
        return true;
    }

    if (addr >= CONTEXT_BASE && addr < END_ADDRESS) {
        u32 context_id = (addr - CONTEXT_BASE) >> 12;
        u32 offset = (addr - CONTEXT_BASE) & 0xfff;
        u32 id;
        u32 id_word;
        u32 id_mask;
        if (context_id < context.size()) {
            switch (offset) {
            case 0: // Threshold
                context[context_id].threshold = val;
                update_context();
                break;
            case 4: // Claim/Complete
                id = (val);
                id_word = id >> 5;
                id_mask = 1 << (id & 0x1f);
                if (id > 0 && id < num_ids) {
                    busy[id_word] &= ~id_mask;
                    update_pending();
                    update_context();
                }
                break;
            default:
                break;
            }
        }

        return true;
    }

    return false;
}

u32 plic::size() const
{
    return END_ADDRESS;
}

void plic::tick()
{
}

plic::~plic()
{
}

void plic::set_interrupt_signal(u32 id, bool level)
{
    if (id == 0)
        return;

    u32 id_word = id >> 5;
    u32 id_mask = 1 << (id & 0x1f);

    if (id_word < interrupt_signal.size()) {
        interrupt_signal[id_word] = (interrupt_signal[id_word] & ~id_mask) | (level ? id_mask : 0);
        update_pending();
        update_context();
    }
}

void plic::update_pending()
{
    for (u32 i = 0; i < pending.size(); i++) {
        pending[i] = ~busy[i] & interrupt_signal[i];
    }
}

void plic::update_context()
{
    for (auto& c : context) {
        u32 best_id = 0;
        u32 best_pri = 0;
        for (u32 id = 1; id < num_ids; id++) {
            u32 id_word = id >> 5;
            u32 id_mask = 1 << (id & 0x1f);
            if ((c.enable[id_word] & pending[id_word]) & id_mask) {
                if (priority[id] > best_pri) {
                    best_pri = priority[id];
                    best_id = id;
                }
            }
        }

        if (best_id > 0 && best_pri > c.threshold) {
            c.claim_complete = best_id;
            c.hcontext->set_external_interrupt(1);
        } else {
            c.claim_complete = 0;
            c.hcontext->set_external_interrupt(0);
        }
    }
}
