
#include <hart.h>

hart::handler_t hart::decode_amo()
{
    u32 funct5 = get_part(inst, 31, 27);
    // u32 aq = get_part(inst, 26, 26);
    // u32 rl = get_part(inst, 25, 25);

    if (funct3 != FUNCT3_AMO) {
        return &thunk<&hart::inst_illegal>;
    }

    switch (funct5) {
        case FUNCT5_LR_W:       return &thunk<&hart::inst_lr_w>;
        case FUNCT5_SC_W:       return &thunk<&hart::inst_sc_w>;
        case FUNCT5_AMOSWAP_W:  return &thunk<&hart::inst_amoswap_w>;
        case FUNCT5_AMOADD_W:   return &thunk<&hart::inst_amoadd_w>;
        case FUNCT5_AMOXOR_W:   return &thunk<&hart::inst_amoxor_w>;
        case FUNCT5_AMOAND_W:   return &thunk<&hart::inst_amoand_w>;
        case FUNCT5_AMOOR_W:    return &thunk<&hart::inst_amoor_w>;
        case FUNCT5_AMOMIN_W:   return &thunk<&hart::inst_amomin_w>;
        case FUNCT5_AMOMAX_W:   return &thunk<&hart::inst_amomax_w>;
        case FUNCT5_AMOMINU_W:  return &thunk<&hart::inst_amominu_w>;
        case FUNCT5_AMOMAXU_W:  return &thunk<&hart::inst_amomaxu_w>;
    }

    return &thunk<&hart::inst_illegal>;

}

template <typename F>
u32 hart::amo(u32 va, F op)
{
    if (va & 0x03) {
        throw trap{trap_cause_t::store_amo_address_misaligned_exception, va};
    }

    // RAM: the fast-TLB write tag, or a full translation as a write (permission check, A and D bits)
    u8* p = nullptr;
    u32 vpn = get_part(va, 31, 12);
    const fast_d_entry& e = fast_d[get_part(vpn, FAST_TLB_BITS - 1, 0)];
    u32 pa = 0;
    if (e.tag_w == vpn) {
        p = (u8*)(e.addend + va);
    } else {
        pa = resolve_addr(va, access_type_t::w);
        p = ram_ptr(pa, 4);
        if (p) {
            fast_fill(access_type_t::w, va, pa);
        }
    }

    if (p) {
        std::atomic_ref<u32> word(*(u32*)p);
        u32 old = word.load(std::memory_order_relaxed);
        while (!word.compare_exchange_weak(old, op(old), std::memory_order_seq_cst, std::memory_order_relaxed)) {
        }
        return old;
    }

    // MMIO: load and store under the device lock
    std::lock_guard<std::mutex> lock(mmio_->lock);
    u32 old;
    if (!mmio_->load(pa, 4, (u8*)&old)) {
        throw trap{trap_cause_t::store_amo_access_fault_exception, va};
    }
    u32 nv = op(old);
    if (!mmio_->store(pa, 4, (u8*)&nv)) {
        throw trap{trap_cause_t::store_amo_access_fault_exception, va};
    }
    return old;
}

void hart::inst_lr_w() {
    u32 addr;
    u32 ldata;
    u32 pa;

    if (rs2 != 0) {
        throw trap{trap_cause_t::illegal_instruction_exception, inst};
    }

    addr = regs[rs1];

    if (addr & 0x03) {
        throw trap{trap_cause_t::load_address_misaligned_exception, addr};
    }

    pa = resolve_addr(addr, access_type_t::r);

    if (u8* p = ram_ptr(pa, 4)) {
        ldata = std::atomic_ref<u32>(*(u32*)p).load(std::memory_order_seq_cst);
    } else if (!bus_load(pa, 4, (u8*)&ldata)) {
        throw trap{trap_cause_t::load_access_fault_exception, addr};
    }

    reservation_set = true;
    reserved_addr = pa;
    reserved_value = ldata;

    regs[rd] = ldata;

    pc = pc + 4;
}

void hart::inst_sc_w() {
    u32 addr;
    u32 sdata;
    u32 pa;

    addr = regs[rs1];
    sdata = regs[rs2];

    if (addr & 0x03) {
        throw trap{trap_cause_t::store_amo_address_misaligned_exception, addr};
    }

    pa = resolve_addr(addr, access_type_t::w);

    u32 result = 1;
    if (reservation_set && reserved_addr == pa) {
        if (u8* p = ram_ptr(pa, 4)) {
            u32 expected = reserved_value;
            if (std::atomic_ref<u32>(*(u32*)p).compare_exchange_strong(expected, sdata, std::memory_order_seq_cst)) {
                result = 0;
            }
        } else {
            if (!bus_store(pa, 4, (u8*)&sdata)) {
                throw trap{trap_cause_t::store_amo_access_fault_exception, addr};
            }
            result = 0;
        }
    }
    regs[rd] = result;

    reservation_set = false;

    pc = pc + 4;
}

void hart::inst_amoswap_w () {
    u32 v2 = regs[rs2];
    u32 old = amo(regs[rs1], [v2](u32 v) { return v2; });
    regs[rd] = old;
    pc = pc + 4;
}

void hart::inst_amoadd_w () {
    u32 v2 = regs[rs2];
    u32 old = amo(regs[rs1], [v2](u32 v) { return v + v2; });
    regs[rd] = old;
    pc = pc + 4;
}

void hart::inst_amoxor_w () {
    u32 v2 = regs[rs2];
    u32 old = amo(regs[rs1], [v2](u32 v) { return v ^ v2; });
    regs[rd] = old;
    pc = pc + 4;
}

void hart::inst_amoand_w () {
    u32 v2 = regs[rs2];
    u32 old = amo(regs[rs1], [v2](u32 v) { return v & v2; });
    regs[rd] = old;
    pc = pc + 4;
}

void hart::inst_amoor_w () {
    u32 v2 = regs[rs2];
    u32 old = amo(regs[rs1], [v2](u32 v) { return v | v2; });
    regs[rd] = old;
    pc = pc + 4;
}

void hart::inst_amomin_w () {
    u32 v2 = regs[rs2];
    u32 old = amo(regs[rs1], [v2](u32 v) { return (i32)v > (i32)v2 ? v2 : v; });
    regs[rd] = old;
    pc = pc + 4;
}

void hart::inst_amomax_w () {
    u32 v2 = regs[rs2];
    u32 old = amo(regs[rs1], [v2](u32 v) { return (i32)v > (i32)v2 ? v : v2; });
    regs[rd] = old;
    pc = pc + 4;
}

void hart::inst_amominu_w () {
    u32 v2 = regs[rs2];
    u32 old = amo(regs[rs1], [v2](u32 v) { return v > v2 ? v2 : v; });
    regs[rd] = old;
    pc = pc + 4;
}

void hart::inst_amomaxu_w () {
    u32 v2 = regs[rs2];
    u32 old = amo(regs[rs1], [v2](u32 v) { return v > v2 ? v : v2; });
    regs[rd] = old;
    pc = pc + 4;
}
