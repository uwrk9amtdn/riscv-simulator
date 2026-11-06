#include "hart.h"

hart::hart(::mmio* mmio, u32 mhartid, u32 pc)
{
    this->mmio_ = mmio;
    this->mhartid = mhartid;
    this->pc = pc;

    mstatus = 0;
    mstatus.MPP() = 0b11;

    misa = 0x40141101; // MXL = 32, extensions: A, I, M, S, U

    fast_flush();
    fast_stats = {};

    // raw 0 is an illegal instruction, so the initial entries are already correct
    dcache = std::make_unique<decoded[]>(1u << DCACHE_BITS);
    for (u32 i = 0; i < (1u << DCACHE_BITS); i++) {
        dcache[i] = {0, 0, &thunk<&hart::inst_illegal>};
    }
}

void hart::check_interrupts()
{
    {
        u32 mip = effective_mip();
        u32 mp = mip & mie & ~mideleg;
        u32 sp = mip & mie & mideleg;

        switch(priv) {
            case 0b11: {
                if (!mstatus.MIE()) {
                    mp = 0;
                }
                sp = 0;
                break;
            }
            case 0b01: {
                if (!mstatus.SIE()) {
                    sp = 0;
                }
                break;
            }
            case 0b00: {
                break;
            }
        }

        if (mp | sp) {

            u32 order [] = {11, 3, 7, 9, 1, 5};
            u32 c;

            for (int i = 0; i < 6; i++) {
                if (get_bit(mp | sp, order[i])) {
                    c = order[i];
                    break;
                }
            }

            c = set_bit(c, 31);

            throw trap{static_cast<trap_cause_t>(c), 0};
        }
    }
}

void hart::execute()
{
    fetch();

    decoded& d = dcache[(pc >> 2) & DCACHE_MASK];
    if (d.raw != inst) [[unlikely]] {
        decode_into(d);
    }

    rd  = (inst >> 7) & 31;
    rs1 = (inst >> 15) & 31;
    rs2 = (inst >> 20) & 31;
    imm = d.imm;

    d.handler(*this);
}

void hart::decode_into(decoded& d)
{
    opcode = get_part(inst,  6,  0);
    rs1    = get_part(inst, 19, 15);
    rs2    = get_part(inst, 24, 20);
    rd     = get_part(inst, 11,  7);
    funct3 = get_part(inst, 14, 12);
    funct7 = get_part(inst, 31, 25);

    // clang-format off
    i32 im = 0;
    switch (opcode) {
        case OP_LUI:
        case OP_AUIPC:  im = get_part_signed(inst, 31, 12, 12); break;                // U
        case OP_JAL:    im = sign_extend(get_part(inst, 31, 31, 20) | get_part(inst, 19, 12, 12) |
                                         get_part(inst, 20, 20, 11) | get_part(inst, 30, 21, 1), 20); break; // J
        case OP_JALR:
        case OP_LOAD:
        case OP_IMM:    im = get_part_signed(inst, 31, 20); break;                    // I
        case OP_BRANCH: im = sign_extend(get_part(inst, 31, 31, 12) | get_part(inst, 7, 7, 11) |
                                         get_part(inst, 30, 25, 5) | get_part(inst, 11, 8, 1), 12); break; // B
        case OP_STORE:  im = get_part_signed(inst, 31, 25, 5) + get_part(inst, 11, 7); break; // S
    }

    handler_t h;
    switch (opcode) {
        case OP_LUI:      h = decode_lui();      break;
        case OP_AUIPC:    h = decode_auipc();    break;
        case OP_JAL:      h = decode_jal();      break;
        case OP_JALR:     h = decode_jalr();     break;
        case OP_BRANCH:   h = decode_branch();   break;
        case OP_LOAD:     h = decode_load();     break;
        case OP_STORE:    h = decode_store();    break;
        case OP_IMM:      h = decode_imm();      break;
        case OP_OP:       h = decode_op();       break;
        case OP_MISC_MEM: h = decode_misc_mem(); break;
        case OP_SYSTEM:   h = decode_system();   break;
        case OP_AMO:      h = decode_amo();      break;
        default:          h = &thunk<&hart::inst_illegal>; break;
    }
    // clang-format on

    d = {inst, im, h};
    decodes++;
}

void hart::inst_illegal()
{
    throw trap{trap_cause_t::illegal_instruction_exception, inst};
}

void hart::take_trap(const trap& t)
{

    u32 cause;
    bool intr;

    cause = static_cast<u32>(t.cause);
    intr = get_bit(cause, 31);
    cause = get_part(cause, 4, 0);

    bool delegate = false;

    if (priv != 0b11) {
        if (intr) {
            if (get_bit(mideleg, cause)) {
                delegate = true;
            }
        } else {
            if (get_bit(medeleg, cause)) {
                delegate = true;
            }
        }
    }

    if (delegate) {
        scause = static_cast<u32>(t.cause);
        stval = t.value;

        mstatus.SPIE() = mstatus.SIE();
        mstatus.SIE() = 0b0;
        mstatus.SPP() = priv;

        priv = 0b01;

        sepc = pc;

        if (stvec.MODE() == 0b01 && intr) {
            pc = (stvec.BASE() + cause) * 4;
        } else {
            pc = stvec.BASE() * 4;
        }

    } else {
        mcause = static_cast<u32>(t.cause);
        mtval = t.value;

        mstatus.MPIE() = mstatus.MIE();
        mstatus.MIE() = 0b0;
        mstatus.MPP() = priv;

        priv = 0b11;

        mepc = pc;

        if (mtvec.MODE() == 0b01 && intr) {
            pc = (mtvec.BASE() + cause) * 4;
        } else {
            pc = mtvec.BASE() * 4;
        }
    }

    fast_check_context();
    irq_dirty = true; // priv and MIE/SIE changed
}

// runs up to n steps (a step that traps counts too) or until WFI parks the hart. The try block is
// outside the inner loop, so it is set up once per trap instead of once per instruction
u32 hart::run(u32 n)
{
    u32 i = 0;
    irq_dirty = true; // devices ticked between batches, check once even if nothing marked it
    while (i < n && !waiting) {
        try {
            for (; i < n && !waiting; i++) {
                if (irq_dirty) [[unlikely]] {
                    irq_dirty = false;
                    check_interrupts();
                }
                execute();
                regs[0] = 0;
            }
        } catch (const trap& t) {
            take_trap(t);
            regs[0] = 0;
            i++;
        }
    }
    return i;
}

void hart::step()
{
    run(1);
}

void hart::set_meip(bool level)
{
    mip.MEIP() = level;
    irq_dirty = true;
}

void hart::set_seip(bool level)
{
    seip = level;
    irq_dirty = true;
}

void hart::set_mtip(bool level)
{
    mip.MTIP() = level;
    irq_dirty = true;
}

void hart::set_stip(bool level)
{
    mip.STIP() = level;
    irq_dirty = true;
}

void hart::set_msip(bool level)
{
    mip.MSIP() = level;
    irq_dirty = true;
}

void hart::set_ssip(bool level)
{
    mip.SSIP() = level;
    irq_dirty = true;
}

hart::trap_cause_t hart::access_type_to_access_fault_exception(access_type_t access_type)
{
    switch (access_type) {
        case access_type_t::r: return trap_cause_t::load_access_fault_exception;
        case access_type_t::w: return trap_cause_t::store_amo_access_fault_exception;
        case access_type_t::x: return trap_cause_t::instruction_access_fault_exception;
    }
    return trap_cause_t{};
}

hart::trap_cause_t hart::access_type_to_page_fault_exception(access_type_t access_type)
{
    switch (access_type) {
        case access_type_t::r: return trap_cause_t::load_page_fault_exception;
        case access_type_t::w: return trap_cause_t::store_amo_page_fault_exception;
        case access_type_t::x: return trap_cause_t::instruction_page_fault_exception;
    }
    return trap_cause_t{};
}

void hart::set_tlb(const std::string& kind, u32 entries, u32 ways)
{
#ifdef TLB_STATIC
    if (kind != hart_tlb_t::kind) {
        runtime_error("--tlb=" + kind + ": this build has a fixed --tlb=" + hart_tlb_t::kind +
                      " TLB, reconfigure with -DRVSIM_TLB_STATIC= for runtime selection");
    }
    itlb = std::make_unique<hart_tlb_t>(entries, ways);
    dtlb = std::make_unique<hart_tlb_t>(entries, ways);
#else
    auto make = [&]() -> std::unique_ptr<TLB> {
        if (kind == "off")  return nullptr;
        if (kind == "null") return std::make_unique<NullTLB>();
        if (kind == "dm")   return std::make_unique<DirectMappedTLB>(entries);
        if (kind == "sa")   return std::make_unique<SetAssocTLB>(entries, ways);
        if (kind == "fa")   return std::make_unique<FullyAssocTLB>(entries);
        runtime_error("unknown tlb kind: " + kind);
        return nullptr;
    };
    itlb = make();
    dtlb = make();
#endif
}

// priv is the effective privilege of the access (MPRV applied), not the current privilege
void hart::sv32_perm_check(u32 va, u32 u, u32 xwr, access_type_t access_type, u32 priv) {
    trap_cause_t page_fault_exception = access_type_to_page_fault_exception(access_type);

    if (mstatus.MXR()) {
        xwr |= xwr >> 2;
    }
    if (!(static_cast<u32>(access_type) & xwr)) {
        throw trap{page_fault_exception, va};
    }

    if (u) {
        if (priv == 0b01) {
            if (access_type == access_type_t::x || !mstatus.SUM()) {
                throw trap{page_fault_exception, va};
            }
        }
    } else {
        if (priv == 0b00) {
            throw trap{page_fault_exception, va};
        }
    }
}

u32 hart::sv32_ptw(u32 va, access_type_t access_type, u32 priv) {
    u32 pte;
    u32 ppa;
    u32 vpn[2];

    trap_cause_t access_fault_exception = access_type_to_access_fault_exception(access_type);
    trap_cause_t page_fault_exception   = access_type_to_page_fault_exception(access_type);

    vpn[1] = get_part(va, 31, 22);
    vpn[0] = get_part(va, 21, 12);

    ppa = satp.PPN() << 12;

    if (get_part(satp, 21, 20)) {
        throw trap{page_fault_exception, va};
        // TODO: should we throw access fault
    }

    for (int i = 1; i >= 0; i--) {
        u32 a = ppa + vpn[i] * 4;
        if (!bus_load(a, 4, (u8*)&pte)) {
            throw trap{access_fault_exception, va};
        }

        u32 v = get_bit(pte, 0);
        u32 xwr = get_part(pte, 3, 1);

        if (v == 0 || xwr == 0b010 || xwr == 0b110) {
            throw trap{page_fault_exception, va};
        }

        if (get_part(pte, 31, 30)) {
            throw trap{page_fault_exception, va};
            // TODO: should we throw access fault
        }

        if (xwr == 0b000) {
            // TODO: U, A, D bits must be zero in non leaf PTEs
            ppa = get_part(pte, 29, 10, 12);
            continue;
        }

        sv32_perm_check(va, get_bit(pte, 4), xwr, access_type, priv);

        if (i == 1 && get_part(pte, 19, 10) != 0) {
            throw trap{page_fault_exception, va};
        }

        bool store = false;

        if (get_bit(pte, 6) == 0b0) {
            pte = set_bit(pte, 6);
            store = true;
        }

        if (access_type == access_type_t::w && get_bit(pte, 7) == 0b0) {
            pte = set_bit(pte, 7);
            store = true;
        }

        // TODO: PTE update must be atomic. Will not be issue in a single core simulator. Will be implemented later
        if (store) {
            if (!bus_store(a, 4, (u8*)&pte)) {
                throw trap{access_fault_exception, va};
            }
        }

        hart_tlb_t* tlb = access_type == access_type_t::x ? itlb.get() : dtlb.get();

        if (tlb) {
            TLBEntry e;

            e.asid  = satp.ASID();
            e.flags = get_part(pte, 7, 0);
            e.super = i == 1;
            e.vpn   = get_part(va, 31, 12);
            // a superpage is cached as the 4 KiB piece that was accessed, so indexed TLBs can use the low vpn bits
            e.ppn   = i == 1 ? (get_part(pte, 29, 20) << 10 | vpn[0]) : get_part(pte, 29, 10);

            tlb->insert(e);
        }

        u32 pa;

        switch (i) {
            case 1:
                pa = get_part(pte, 29, 20, 22) | get_part(va, 21, 0);
                break;
            case 0:
                pa = get_part(pte, 29, 10, 12) | get_part(va, 11, 0);
                break;
            default:
                pa = 0;
                break;
        }

        return pa;
    }

    throw trap{page_fault_exception, va};
}

u32 hart::resolve_addr(u32 addr, access_type_t access_type) {
    // TODO: implement PMA/PMP checks

    u32 _priv = (access_type != access_type_t::x && mstatus.MPRV()) ? mstatus.MPP() : priv;

    if (get_bit(_priv, 1) == 0b1 || satp.MODE() == 0b0) {
        return addr; // M mode or bare: no translation
    }

    hart_tlb_t* tlb = access_type == access_type_t::x ? itlb.get() : dtlb.get();

    if (tlb) {
        if (auto e = tlb->lookup(addr, satp.ASID())) {
            // entries are only inserted with A set. A write to a clean page is a miss so the walker sets D
            if (access_type != access_type_t::w || get_bit(e->flags, 7)) {
                sv32_perm_check(addr, get_bit(e->flags, 4), get_part(e->flags, 3, 1), access_type, _priv);
                return e->ppn << 12 | get_part(addr, 11, 0);
            }
        }
    }

    return sv32_ptw(addr, access_type, _priv);
}

void hart::load_slow(u32 addr, u32 len, u8* data, access_type_t access_type) {
    trap_cause_t access_fault_exception = access_type_to_access_fault_exception(access_type);

    u32 pa = resolve_addr(addr, access_type);

    if (!bus_load(pa, len, data)) {
        throw trap{access_fault_exception, addr};
    }

    fast_fill(access_type, addr, pa);
}

void hart::store_slow(u32 addr, u32 len, const u8* data, access_type_t access_type) {
    trap_cause_t access_fault_exception = access_type_to_access_fault_exception(access_type);

    u32 pa = resolve_addr(addr, access_type);

    if (!bus_store(pa, len, data)) {
        throw trap{access_fault_exception, addr};
    }

    fast_fill(access_type, addr, pa);
}

void hart::fast_flush()
{
    for (auto& e : fast_i) {
        e.tag_x = FAST_TLB_INVALID;
    }
    for (auto& e : fast_d) {
        e.tag_r = FAST_TLB_INVALID;
        e.tag_w = FAST_TLB_INVALID;
    }
    fetch_vpn = FAST_TLB_INVALID;
    fast_stats.flushes++;
}

void hart::fast_check_context()
{
    u32 dpriv = mstatus.MPRV() ? (u32)mstatus.MPP() : priv;
    u32 key = priv | dpriv << 2 | (u32)mstatus.SUM() << 4 | (u32)mstatus.MXR() << 5;
    if (key != fast_key) {
        fast_key = key;
        fast_flush();
    }
}

// called after an access passed resolve_addr (all permission checks, A/D bits set) and the bus.
// Only pages that lie completely in RAM are cached, everything else keeps going through mmio
void hart::fast_fill(access_type_t access_type, u32 va, u32 pa)
{
    u32 off = (pa & ~0xfffu) - ram_base;
    if (!(off < ram_size && ram_size - off >= 4096)) {
        return;
    }

    u32 vpn = va >> 12;
    u32 i = vpn & FAST_TLB_MASK;
    uintptr_t addend = (uintptr_t)(ram_host + off) - (uintptr_t)(va & ~0xfffu);

    switch (access_type) {
        case access_type_t::x:
            fast_i[i] = {vpn, addend};
            fast_stats.ifills++;
            break;
        case access_type_t::r:
            if (fast_d[i].tag_w != vpn) {
                fast_d[i].tag_w = FAST_TLB_INVALID;
            }
            fast_d[i].tag_r = vpn;
            fast_d[i].addend = addend;
            fast_stats.dfills++;
            break;
        case access_type_t::w:
            // W without R is a reserved PTE encoding, so a page that passed the write check is also readable
            fast_d[i] = {vpn, vpn, addend};
            fast_stats.dfills++;
            break;
    }
}