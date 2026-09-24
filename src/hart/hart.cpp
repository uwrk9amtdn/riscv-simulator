#include "hart.h"

hart::hart(::mmio* mmio, u32 mhartid, u32 pc)
{
    this->mmio_ = mmio;
    this->mhartid = mhartid;
    this->pc = pc;

    mstatus = 0;
    mstatus.MPP() = 0b11;
}

void hart::step()
{
    try {

        switch (priv) {
            case 0b00: { // user
                if (mip.MEIP() && mie.MEIE()) {
                    throw trap{trap_cause_t::machine_external_interrupt, 0};
                }
            } break;
            case 0b01: { // supervisor

            } break;
            case 0b11: { // machine

            } break;
            default: {

            } break;
        }

        if (mstatus.MIE()) {
            if (mip.MEIP() && mie.MEIE()) {
                throw trap{trap_cause_t::machine_external_interrupt, 0};
            }

            if (mip.MSIP() && mie.MSIE()) {
                throw trap{trap_cause_t::machine_software_interrupt, 0};
            }

            if (mip.MTIP() && mie.MTIE()) {
                throw trap{trap_cause_t::machine_timer_interrupt, 0};
            }
        }

        load(pc, 4, (u8*)&inst, access_type_t::x);

        opcode = get_part(inst,  6,  0);
        rs1    = get_part(inst, 19, 15);
        rs2    = get_part(inst, 24, 20);
        rd     = get_part(inst, 11,  7);
        funct3 = get_part(inst, 14, 12);
        funct7 = get_part(inst, 31, 25);

        // clang-format off
        switch (opcode) {
            case OP_LUI:      decode_lui();      break;
            case OP_AUIPC:    decode_auipc();    break;
            case OP_JAL:      decode_jal();      break;
            case OP_JALR:     decode_jalr();     break;
            case OP_BRANCH:   decode_branch();   break;
            case OP_LOAD:     decode_load();     break;
            case OP_STORE:    decode_store();    break;
            case OP_IMM:      decode_imm();      break;
            case OP_OP:       decode_op();       break;
            case OP_MISC_MEM: decode_misc_mem(); break;
            case OP_SYSTEM:   decode_system();   break;
            case OP_AMO:      decode_amo();      break;
            default:
                throw trap{trap_cause_t::illegal_instruction_exception, inst};
                break;
        }
        // clang-format on

    } catch (trap t) {

        if (priv == 0b00 || priv == 0b01) {

        } else {

        }

        // TODO: medeleg
        mcause = static_cast<u32>(t.cause);
        mtval = t.value;

        mstatus.MPIE() = mstatus.MIE();
        mstatus.MIE() = 0b0;
        mstatus.MPP() = priv;

        priv = 0b11;

        mepc = pc;

        if (mtvec & 0x1) {
            pc = (mtvec & ~create_mask(1, 0)) + (get_part(mcause, 30, 0) * 4);
        } else {
            pc = mtvec & ~create_mask(1, 0);
        }
    }

    regs[0] = 0;
}

void hart::set_meip(bool level)
{
    mip.MEIP() = level;
}

void hart::set_seip(bool level)
{
    mip.SEIP() = level;
}

void hart::set_mtip(bool level)
{
    mip.MTIP() = level;
}

void hart::set_stip(bool level)
{
    mip.STIP() = level;
}

void hart::set_msip(bool level)
{
    mip.MSIP() = level;
}

void hart::set_ssip(bool level)
{
    mip.SSIP() = level;
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

void hart::sv32_ptw(u32 va, u32& pa, access_type_t access_type) {
    u32 pte;
    u32 ppa;
    u32 vpn[2];

    trap_cause_t access_fault_exception = access_type_to_access_fault_exception(access_type);
    trap_cause_t page_fault_exception   = access_type_to_page_fault_exception(access_type);

    vpn[1] = get_part(va, 31, 22);
    vpn[0] = get_part(va, 21, 12);

    ppa = get_part(satp, 19, 0, 12);

    for (int i = 1; i >= 0; i--) {
        u32 a = ppa + vpn[i] * 4;
        if (!mmio_->load(a, 4, (u8*)&pte)) {
            throw trap{access_fault_exception, va};
        }

        u32 v = get_part(pte, 0, 0);
        u32 xwr = get_part(pte, 3, 1);

        if (v == 0 || xwr == 0b010 || xwr == 0b110) {
            throw trap{page_fault_exception, va};
        }

        if (xwr == 0b000) {
            if (i == 0) {
                throw trap{page_fault_exception, va};
            }
            ppa = get_part(pte, 29, 10, 12);
            continue;
        }

        if (!(static_cast<u32>(access_type) & xwr)) {
            throw trap{page_fault_exception, va};
        }

        switch (i) {
            case 1:
                if (get_part(pte, 21, 10) != 0) {
                    throw trap{page_fault_exception, va};
                }
                pa = get_part(pte, 29, 22, 22) + get_part(va, 21, 0);
                break;
            case 0:
                pa = get_part(pte, 29, 12, 12) + get_part(va, 11, 0);
                break;
            default:
                pa = 0;
                break;
        }

    }
}

void hart::load(u32 addr, u32 len, u8* data, access_type_t access_type) {
    trap_cause_t access_fault_exception = access_type_to_access_fault_exception(access_type);

    // disabled for now
    if (0 && get_part(priv, 1, 1) == 0b0 && get_part(satp, 31, 31) == 0b1) {
        u32 pa;
        sv32_ptw(addr, pa, access_type);
        if (!mmio_->load(pa, len, data)) {
            throw trap{access_fault_exception, addr};
        }
    } else {
        if (!mmio_->load(addr, len, data)) {
            throw trap{access_fault_exception, addr};
        }
    }
}

void hart::store(u32 addr, u32 len, const u8* data, access_type_t access_type) {
    trap_cause_t access_fault_exception = access_type_to_access_fault_exception(access_type);
    // disabled for now
    if (0 && get_part(priv, 1, 1) == 0b0 && get_part(satp, 31, 31) == 0b1) {
        u32 pa;
        sv32_ptw(addr, pa, access_type);
        if (!mmio_->store(pa, len, data)) {
            throw trap{access_fault_exception, addr};
        }
    } else {
        if (!mmio_->store(addr, len, data)) {
            throw trap{access_fault_exception, addr};
        }
    }
}