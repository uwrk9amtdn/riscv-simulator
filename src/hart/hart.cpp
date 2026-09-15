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

        load(pc, 4, (u8*)&inst, MCAUSE_INSTRUCTION_ACCESS_FAULT_EXCEPTION);

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

void hart::load(u32 addr, u32 len, u8* data, u32 exc) {
    if (!mmio_->load(addr, len, data)) throw trap{exc, addr};
}

void hart::store(u32 addr, u32 len, const u8* data, u32 exc) {
    if (!mmio_->store(addr, len, data)) throw trap{exc, addr};
}
