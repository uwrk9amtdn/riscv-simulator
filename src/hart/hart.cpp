#include "hart.h"

hart::hart(::mmio* mmio, u32 mhartid, u32 pc)
{
    this->mmio_ = mmio;
    this->mhartid = mhartid;
    this->pc = pc;

    mstatus = 0;
    mstatus |= create_mask(12, 11);
}

void hart::step()
{
    try {

        if ((mstatus & 0x8)) { // MIE is set
            if (mip & mie & (1 << 11)) {
                throw trap{MCAUSE_MACHINE_EXTERNAL_INTERRUPT, 0};
            } else if (mip & mie & (1 << 3)) {
                throw trap{MCAUSE_MACHINE_SOFTWARE_INTERRUPT, 0};
            } else if (mip & mie & (1 << 7)) {
                throw trap{MCAUSE_MACHINE_TIMER_INTERRUPT, 0};
            }
        }

        load(pc, 4, (u8*)&inst, access_type_t::x);

        opcode = get_part(inst, 6, 0);
        rs1 = get_part(inst, 19, 15);
        rs2 = get_part(inst, 24, 20);
        rd = get_part(inst, 11, 7);
        funct3 = get_part(inst, 14, 12);
        funct7 = get_part(inst, 31, 25);

        // clang-format off
        switch (opcode) {
        case OP_LUI:      op_lui();      break;
        case OP_AUIPC:    op_auipc();    break;
        case OP_JAL:      op_jal();      break;
        case OP_JALR:     op_jalr();     break;
        case OP_BRANCH:   op_branch();   break;
        case OP_LOAD:     op_load();     break;
        case OP_STORE:    op_store();    break;
        case OP_IMM:      op_imm();      break;
        case OP_OP:       op_op();       break;
        case OP_MISC_MEM: op_misc_mem(); break;
        case OP_SYSTEM:   op_system();   break;
        case OP_AMO:      op_amo();      break;
        default:
            throw trap{MCAUSE_ILLEGAL_INSTRUCTION_EXCEPTION, inst};
            break;
        }
        // clang-format on

    } catch (trap t) {
        // TODO: medeleg
        mcause = t.cause;
        mtval = t.tval;

        if (mstatus & (1 << 3)) {
            mstatus = set_bit(mstatus, 7);
        } else {
            mstatus = clear_bit(mstatus, 7);
        }
        mstatus = clear_bit(mstatus, 3);

        mstatus = set_part(mstatus, 12, 11, priv);
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
    if (level) {
        mip = set_bit(mip, 11);
    } else {
        mip = clear_bit(mip, 11);
    }
}

void hart::set_seip(bool level)
{
    (void)level;
}

void hart::set_mtip(bool level)
{
    if (level) {
        mip = set_bit(mip, 7);
    } else {
        mip = clear_bit(mip, 7);
    }
}

void hart::set_stip(bool level)
{
    (void)level;
}

void hart::set_msip(bool level)
{
    if (level) {
        mip = set_bit(mip, 3);
    } else {
        mip = clear_bit(mip, 3);
    }
}

void hart::set_ssip(bool level)
{
    (void)level;
}

u32 hart::access_type_to_mcause_access_fault(access_type_t access_type) {
    switch (access_type) {
    case access_type_t::r: return MCAUSE_LOAD_ACCESS_FAULT_EXCEPTION;
    case access_type_t::w: return MCAUSE_STORE_AMO_ACCESS_FAULT_EXCEPTION;
    case access_type_t::x: return MCAUSE_INSTRUCTION_ACCESS_FAULT_EXCEPTION;
    }
    return 0;
}

u32 hart::access_type_to_mcause_page_fault(access_type_t access_type) {
    // TODO
    return 0;
}

void hart::sv32_ptw(u32 va, u32& pa, access_type_t access_type) {
    u32 pte;
    u32 ppa;
    u32 vpn[2];

    u32 mcause_access_fault = access_type_to_mcause_access_fault(access_type);;
    u32 mcause_page_fault = access_type_to_mcause_page_fault(access_type);

    vpn[1] = get_part(va, 31, 22);
    vpn[0] = get_part(va, 21, 12);

    ppa = get_part(satp, 19, 0, 12);

    for (int i = 1; i >= 0; i--) {
        u32 a = ppa + vpn[i] * 4;
        if (!mmio_->load(a, 4, (u8*)&pte)) {
            throw trap{mcause_access_fault, va};
        }

        u32 v = get_part(pte, 0, 0);
        u32 xwr = get_part(pte, 3, 1);

        if (v == 0 || xwr == 0b010 || xwr == 0b110) {
            throw trap{mcause_page_fault, va};
        }

        if (xwr == 0b000) {
            if (i == 0) {
                throw trap{mcause_page_fault, va};
            }
            ppa = get_part(pte, 29, 10, 12);
            continue;
        }

        if (!(static_cast<u32>(access_type) & xwr)) {
            throw trap{mcause_page_fault, va};
        }

        switch (i) {
            case 1:
                if (get_part(pte, 21, 10) != 0) {
                    throw trap{mcause_page_fault, va};
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
    u32 mcause_access_fault = access_type_to_mcause_access_fault(access_type);
    // disabled for now
    if (0 && get_part(priv, 1, 1) == 0b0 && get_part(satp, 31, 31) == 0b1) {
        u32 pa;
        sv32_ptw(addr, pa, access_type);
        if (!mmio_->load(pa, len, data)) {
            throw trap{mcause_access_fault, addr};
        }
    } else {
        if (!mmio_->load(addr, len, data)) {
            throw trap{mcause_access_fault, addr};
        }
    }
}

void hart::store(u32 addr, u32 len, const u8* data, access_type_t access_type) {
    u32 mcause_access_fault = access_type_to_mcause_access_fault(access_type);
    // disabled for now
    if (0 && get_part(priv, 1, 1) == 0b0 && get_part(satp, 31, 31) == 0b1) {
        u32 pa;
        sv32_ptw(addr, pa, access_type);
        if (!mmio_->store(pa, len, data)) {
            throw trap{mcause_access_fault, addr};
        }
    } else {
        if (!mmio_->store(addr, len, data)) {
            throw trap{mcause_access_fault, addr};
        }
    }
}

// void hart::load(u32 addr, u32 len, u8* data, u32 exc) {
//     if (!mmio_->load(addr, len, data)) throw trap{exc, addr};
// }

// void hart::store(u32 addr, u32 len, const u8* data, u32 exc) {
//     if (!mmio_->store(addr, len, data)) throw trap{exc, addr};
// }
