#include "hart.h"
#include "constants.h"
#include <stdio.h>
#include <stdlib.h>

struct trap {
    u32 cause;
    u32 tval;
};

hart::hart(::mmio* mmio, u32 mhartid, u32 pc)
{
    this->mmio_ = mmio;
    this->mhartid = mhartid;
    this->pc = pc;

    mstatus = 0;
    mstatus |= create_mask(12, 11);
}

void hart::op_lui()
{
    imm = get_part_s(inst, 31, 12, 12);
    regs[rd] = imm;
    pc = pc + 4;
}

void hart::op_auipc()
{
    imm = get_part_s(inst, 31, 12, 12);
    regs[rd] = pc + imm;
    pc = pc + 4;
}

void hart::op_jal()
{
    u32 target_pc;
    imm = sign_extend(get_part(inst, 31, 31, 20) | get_part(inst, 19, 12, 12) | get_part(inst, 20, 20, 11) | get_part(inst, 30, 21, 1), 20);
    regs[rd] = pc + 4;
    target_pc = pc + imm;
    if (target_pc & 0x3) {
        throw trap{MCAUSE_INSTRUCTION_ADDRESS_MISALIGNED_EXCEPTION, target_pc};
    } else {
        pc = target_pc;
    }
}

void hart::op_jalr()
{
    u32 target_pc;

    imm = get_part_s(inst, 31, 20);
    target_pc = (regs[rs1] + imm) & (~1);
    regs[rd] = pc + 4;
    if (target_pc & 0x3) {
        throw trap{MCAUSE_INSTRUCTION_ADDRESS_MISALIGNED_EXCEPTION, target_pc};
    } else {
        pc = target_pc;
    }
}

void hart::op_branch()
{
    u32 target_pc;
    imm = sign_extend(get_part(inst, 31, 31, 12) | get_part(inst, 7, 7, 11) | get_part(inst, 30, 25, 5) | get_part(inst, 11, 8, 1), 12);

    switch (funct3) {
    case FUNCT3_BEQ:
        target_pc = pc + (regs[rs1] == regs[rs2] ? imm : 4);
        break;
    case FUNCT3_BNE:
        target_pc = pc + (regs[rs1] != regs[rs2] ? imm : 4);
        break;
    case FUNCT3_BLT:
        target_pc = pc + ((i32)regs[rs1] < (i32)regs[rs2] ? imm : 4);
        break;
    case FUNCT3_BGE:
        target_pc = pc + ((i32)regs[rs1] >= (i32)regs[rs2] ? imm : 4);
        break;
    case FUNCT3_BLTU:
        target_pc = pc + (regs[rs1] < regs[rs2] ? imm : 4);
        break;
    case FUNCT3_BGEU:
        target_pc = pc + (regs[rs1] >= regs[rs2] ? imm : 4);
        break;
    default:
        throw MCAUSE_ILLEGAL_INSTRUCTION_EXCEPTION;
        break;
    }

    if (target_pc & 0x3) {
        throw trap{MCAUSE_INSTRUCTION_ADDRESS_MISALIGNED_EXCEPTION, target_pc};
    } else {
        pc = target_pc;
    }
}

void hart::op_load()
{
    u32 addr;
    bool b;
    u32 data;
    i8 di8;
    i16 di16;
    u8 du8;
    u16 du16;

    addr = regs[rs1] + get_part_s(inst, 31, 20);

    switch (funct3) {
    case FUNCT3_LB:
        b = mmio_->load(addr, 1, (u8*)&di8);
        data = di8;
        break;
    case FUNCT3_LH:
        b = mmio_->load(addr, 2, (u8*)&di16);
        data = di16;
        break;
    case FUNCT3_LW:
        b = mmio_->load(addr, 4, (u8*)&data);
        break;
    case FUNCT3_LBU:
        b = mmio_->load(addr, 1, (u8*)&du8);
        data = du8;
        break;
    case FUNCT3_LHU:
        b = mmio_->load(addr, 2, (u8*)&du16);
        data = du16;
        break;
    default:
        throw trap{MCAUSE_ILLEGAL_INSTRUCTION_EXCEPTION, inst};
        break;
    }

    if (!b) {
        throw trap{MCAUSE_LOAD_ACCESS_FAULT_EXCEPTION, addr};
    } else {
        regs[rd] = data;
        pc = pc + 4;
    }
}

void hart::op_store()
{
    u32 addr;
    bool b;
    u32 data;

    addr = regs[rs1] + get_part_s(inst, 31, 25, 5) + get_part(inst, 11, 7);

    switch (funct3) {
    case FUNCT3_SB:
        data = regs[rs2];
        b = mmio_->store(addr, 1, (u8*)&data);
        break;
    case FUNCT3_SH:
        data = regs[rs2];
        b = mmio_->store(addr, 2, (u8*)&data);
        break;
    case FUNCT3_SW:
        data = regs[rs2];
        b = mmio_->store(addr, 4, (u8*)&data);
        break;
    default:
        throw trap{MCAUSE_ILLEGAL_INSTRUCTION_EXCEPTION, inst};
        break;
    }

    if (!b) {
        throw trap{MCAUSE_STORE_AMO_ACCESS_FAULT_EXCEPTION, addr};
    } else {
        pc = pc + 4;
    }
}

void hart::op_imm()
{
    imm = get_part_s(inst, 31, 20);
    u32 shamt = rs2;

    switch (funct3) {
    case FUNCT3_ADDI:
        regs[rd] = regs[rs1] + imm;
        break;

    case FUNCT3_SLTI:
        regs[rd] = (i32)regs[rs1] < imm ? 1 : 0;
        break;

    case FUNCT3_SLTIU:
        regs[rd] = regs[rs1] < (u32)imm ? 1 : 0;
        break;

    case FUNCT3_XORI:
        regs[rd] = regs[rs1] ^ imm;
        break;

    case FUNCT3_ORI:
        regs[rd] = regs[rs1] | imm;
        break;

    case FUNCT3_ANDI:
        regs[rd] = regs[rs1] & imm;
        break;

    case FUNCT3_SLLI:
        if (funct7 == FUNCT7_SLLI) {
            regs[rd] = regs[rs1] << shamt;
        } else {
            throw trap{MCAUSE_ILLEGAL_INSTRUCTION_EXCEPTION, inst};
        }
        break;

    case FUNCT3_SRLI_SRAI:

        switch (funct7) {
        case FUNCT7_SRLI:
            regs[rd] = regs[rs1] >> shamt;
            break;

        case FUNCT7_SRAI:
            regs[rd] = (i32)regs[rs1] >> shamt;
            break;

        default:
            throw trap{MCAUSE_ILLEGAL_INSTRUCTION_EXCEPTION, inst};
            break;
        }
        break;

    default:
        throw trap{MCAUSE_ILLEGAL_INSTRUCTION_EXCEPTION, inst};
        break;
    }
    pc = pc + 4;
}

void hart::op_op()
{
    switch (funct3 | (funct7 << 3)) {

    case FUNCT10_ADD:
        regs[rd] = regs[rs1] + regs[rs2];
        break;

    case FUNCT10_SUB:
        regs[rd] = regs[rs1] - regs[rs2];
        break;

    case FUNCT10_SLL:
        regs[rd] = regs[rs1] << (regs[rs2] & create_mask(4, 0));
        break;

    case FUNCT10_SLT:
        regs[rd] = (i32)regs[rs1] < (i32)regs[rs2] ? 1 : 0;
        break;

    case FUNCT10_SLTU:
        regs[rd] = regs[rs1] < regs[rs2] ? 1 : 0;
        break;

    case FUNCT10_XOR:
        regs[rd] = regs[rs1] ^ regs[rs2];
        break;

    case FUNCT10_SRL:
        regs[rd] = regs[rs1] >> (regs[rs2] & create_mask(4, 0));
        break;

    case FUNCT10_SRA:
        regs[rd] = (i32)regs[rs1] >> (regs[rs2] & create_mask(4, 0));
        break;

    case FUNCT10_OR:
        regs[rd] = regs[rs1] | regs[rs2];
        break;

    case FUNCT10_AND:
        regs[rd] = regs[rs1] & regs[rs2];
        break;

    case FUNCT10_MUL:
        regs[rd] = (i32)regs[rs1] * (i32)regs[rs2];
        break;

    case FUNCT10_MULH:
        regs[rd] = ((i64)(i32)regs[rs1] * (i64)(i32)regs[rs2]) >> 32;
        break;

    case FUNCT10_MULHSU:
        regs[rd] = ((i64)(i32)regs[rs1] * (i64)regs[rs2]) >> 32;
        break;

    case FUNCT10_MULHU:
        regs[rd] = ((u64)regs[rs1] * (u64)regs[rs2]) >> 32;
        break;

    case FUNCT10_DIV:
        if (regs[rs2] == 0) { // division by zero
            regs[rd] = -1;
        } else if ((i32)regs[rs1] == INT32_MIN && (i32)regs[rs2] == -1) { // overflow
            regs[rd] = INT32_MIN;
        } else {
            regs[rd] = (i32)regs[rs1] / (i32)regs[rs2];
        }
        break;

    case FUNCT10_DIVU:
        if (regs[rs2] == 0) { // divison by zero
            regs[rd] = UINT32_MAX;
        } else {
            regs[rd] = regs[rs1] / regs[rs2];
        }
        break;

    case FUNCT10_REM:
        if (regs[rs2] == 0) { // division by zero
            regs[rd] = regs[rs1];
        } else if ((i32)regs[rs1] == INT32_MIN && (i32)regs[rs2] == -1) { // overflow
            regs[rd] = 0;
        } else {
            regs[rd] = (i32)regs[rs1] % (i32)regs[rs2];
        }
        break;

    case FUNCT10_REMU:
        if (regs[rs2] == 0) { // division by zero
            regs[rd] = regs[rs1];
        } else {
            regs[rd] = regs[rs1] % regs[rs2];
        }
        break;

    default:
        throw trap{MCAUSE_ILLEGAL_INSTRUCTION_EXCEPTION, inst};
        break;
    }
    pc = pc + 4;
}

void hart::op_system()
{

    u32 csr = get_part(inst, 31, 20);
    bool b;
    u32 pc_next = pc + 4;
    switch (funct3) {
    case FUNCT3_ECALL_EBREAK_WFI_MRET:
        if (rd != 0 || rs1 != 0) {
            throw trap{MCAUSE_ILLEGAL_INSTRUCTION_EXCEPTION, inst};
        }

        switch (funct7) {
        case FUNCT7_ECALL_EBREAK:
            switch (rs2) {
            case RS2_ECALL:
                if (priv == 0b11) { // m mode
                    throw trap{MCAUSE_ENVIRONMENT_CALL_FROM_M_MODE_EXCEPTION, 0};
                } else {
                    throw trap{MCAUSE_ENVIRONMENT_CALL_FROM_U_MODE_EXCEPTION, 0};
                }
                break;
            case RS2_EBREAK:
                throw trap{MCAUSE_BREAKPOINT_EXCEPTION, 0};
                break;
            default:
                throw trap{MCAUSE_ILLEGAL_INSTRUCTION_EXCEPTION, inst};
                break;
            }
            break;
        case FUNCT7_WFI:
            switch (rs2) {
            case RS2_WFI:
                break; // WFI implemented as NOP
            default:
                throw trap{MCAUSE_ILLEGAL_INSTRUCTION_EXCEPTION, inst};
                break;
            }
            break;
        case FUNCT7_MRET:
            switch (rs2) {
            case RS2_MRET:
                if (mstatus & (1 << 7)) {
                    mstatus = set_bit(mstatus, 3);
                } else {
                    mstatus = clear_bit(mstatus, 3);
                }
                mstatus = set_bit(mstatus, 7);

                priv = get_part(mstatus, 12, 11);
                mstatus = set_part(mstatus, 12, 11, 0b00);

                pc_next = mepc;
                break;
            default:
                throw trap{MCAUSE_ILLEGAL_INSTRUCTION_EXCEPTION, inst};
                break;
            }
            break;
        default:
            throw trap{MCAUSE_ILLEGAL_INSTRUCTION_EXCEPTION, inst};
            break;
        }
        break;
    case FUNCT3_CSRRW:
        b = csr_rw(csr, -1, -1, regs[rd], regs[rs1]);
        if (!b) {
            throw trap{MCAUSE_ILLEGAL_INSTRUCTION_EXCEPTION, inst};
        }
        break;
    case FUNCT3_CSRRS:
        b = csr_rw(csr, -1, regs[rs1], regs[rd], -1);
        if (!b) {
            throw trap{MCAUSE_ILLEGAL_INSTRUCTION_EXCEPTION, inst};
        }
        break;
    case FUNCT3_CSRRC:
        b = csr_rw(csr, -1, regs[rs1], regs[rd], 0);
        if (!b) {
            throw trap{MCAUSE_ILLEGAL_INSTRUCTION_EXCEPTION, inst};
        }
        break;
    case FUNCT3_CSRRWI:
        b = csr_rw(csr, -1, -1, regs[rd], rs1);
        if (!b) {
            throw trap{MCAUSE_ILLEGAL_INSTRUCTION_EXCEPTION, inst};
        }
        break;
    case FUNCT3_CSRRSI:
        b = csr_rw(csr, -1, rs1, regs[rd], -1);
        if (!b) {
            throw trap{MCAUSE_ILLEGAL_INSTRUCTION_EXCEPTION, inst};
        }
        break;
    case FUNCT3_CSRRCI:
        b = csr_rw(csr, -1, rs1, regs[rd], 0);
        if (!b) {
            throw trap{MCAUSE_ILLEGAL_INSTRUCTION_EXCEPTION, inst};
        }
        break;
    default:
        throw trap{MCAUSE_ILLEGAL_INSTRUCTION_EXCEPTION, inst};
        break;
    }
    pc = pc_next;
}

void hart::op_misc_mem()
{

    switch (funct3) {
    case FUNCT3_FENCE:
        break;

    case FUNCT3_FENCE_I:
        break;

    default:
        throw trap{MCAUSE_ILLEGAL_INSTRUCTION_EXCEPTION, inst};
        break;
    }

    pc = pc + 4;
}

void hart::op_amo()
{
    u32 funct5 = get_part(inst, 31, 27);
    // u32 aq = get_part(inst, 26, 26);
    // u32 rl = get_part(inst, 25, 25);

    bool b;
    u32 addr;
    u32 data;

    if (funct3 != FUNCT3_AMO) {
        throw trap{MCAUSE_ILLEGAL_INSTRUCTION_EXCEPTION, inst};
    }

    switch (funct5) {
    case FUNCT5_LR_W:
        if (rs2 != 0) {
            throw trap{MCAUSE_ILLEGAL_INSTRUCTION_EXCEPTION, inst};
        }

        addr = regs[rs1];

        if (addr & 0x03) {
            throw trap{MCAUSE_LOAD_ADDRESS_MISALIGNED_EXCEPTION, addr};
        }

        b = mmio_->load(addr, 4, (u8*)&data);

        if (!b) {
            throw trap{MCAUSE_LOAD_ACCESS_FAULT_EXCEPTION, addr};
        }

        regs[rd] = data;

        reservation_set = true;
        reserved_addr = addr;

        break;
    case FUNCT5_SC_W:

        addr = regs[rs1];
        data = regs[rs2];

        if (addr & 0x03) {
            throw trap{MCAUSE_STORE_AMO_ADDRESS_MISALIGNED_EXCEPTION, addr};
        }

        if (reservation_set && reserved_addr == addr) {
            b = mmio_->store(addr, 4, (u8*)&data);

            if (!b) {
                throw trap{MCAUSE_STORE_AMO_ACCESS_FAULT_EXCEPTION, addr};
            }
            regs[rd] = 0;
            reservation_set = false;
        } else {
            regs[rd] = 1;
        }

        break;
    case FUNCT5_AMOSWAP_W:
    case FUNCT5_AMOADD_W:
    case FUNCT5_AMOXOR_W:
    case FUNCT5_AMOAND_W:
    case FUNCT5_AMOOR_W:
    case FUNCT5_AMOMIN_W:
    case FUNCT5_AMOMAX_W:
    case FUNCT5_AMOMINU_W:
    case FUNCT5_AMOMAXU_W:
        addr = regs[rs1];

        if (addr & 0x03) {
            throw trap{MCAUSE_STORE_AMO_ADDRESS_MISALIGNED_EXCEPTION, addr};
        }

        b = mmio_->load(addr, 4, (u8*)&data);

        if (!b) {
            throw trap{MCAUSE_LOAD_ACCESS_FAULT_EXCEPTION, addr};
        }

        regs[rd] = data;

        // clang-format off
        switch (funct5) {
        case FUNCT5_AMOSWAP_W: data = regs[rs2];                                       break;
        case FUNCT5_AMOADD_W:  data = data + regs[rs2];                                break;
        case FUNCT5_AMOXOR_W:  data = data ^ regs[rs2];                                break;
        case FUNCT5_AMOAND_W:  data = data & regs[rs2];                                break;
        case FUNCT5_AMOOR_W:   data = data | regs[rs2];                                break;
        case FUNCT5_AMOMIN_W:  data = ((i32)data > (i32)regs[rs2]) ? regs[rs2] : data; break;
        case FUNCT5_AMOMAX_W:  data = ((i32)data > (i32)regs[rs2]) ? data : regs[rs2]; break;
        case FUNCT5_AMOMINU_W: data = (data > regs[rs2]) ? regs[rs2] : data;           break;
        case FUNCT5_AMOMAXU_W: data = (data > regs[rs2]) ? data : regs[rs2];           break;
        default: break;
        }
        // clang-format on

        b = mmio_->store(addr, 4, (u8*)&data);

        if (!b) {
            throw trap{MCAUSE_STORE_AMO_ACCESS_FAULT_EXCEPTION, addr};
        }

        break;
    default:
        throw trap{MCAUSE_ILLEGAL_INSTRUCTION_EXCEPTION, inst};
        break;
    }

    pc = pc + 4;
}

void hart::step()
{
    bool b;

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

        b = mmio_->load(pc, 4, (u8*)&inst);

        if (!b) {
            throw trap{MCAUSE_INSTRUCTION_ACCESS_FAULT_EXCEPTION, pc};
        }

        opcode = get_part(inst, 6, 0);
        rs1 = get_part(inst, 19, 15);
        rs2 = get_part(inst, 24, 20);
        rd = get_part(inst, 11, 7);
        funct3 = get_part(inst, 14, 12);
        funct7 = get_part(inst, 31, 25);

        // printf(
        //     "pc = %08x, "
        //     "inst = %08x, "
        //     "x0 = %08x, "
        //     "x1 = %08x, "
        //     "x2 = %08x, "
        //     "x3 = %08x, "
        //     "x4 = %08x, "
        //     "x5 = %08x, "
        //     "x6 = %08x, "
        //     "x7 = %08x, "
        //     "x8 = %08x, "
        //     "x9 = %08x, "
        //     "x10 = %08x, "
        //     "x11 = %08x, "
        //     "x12 = %08x, "
        //     "x13 = %08x, "
        //     "x14 = %08x, "
        //     "x15 = %08x\n",
        //     pc,
        //     inst,
        //     regs[0],
        //     regs[1],
        //     regs[2],
        //     regs[3],
        //     regs[4],
        //     regs[5],
        //     regs[6],
        //     regs[7],
        //     regs[8],
        //     regs[9],
        //     regs[10],
        //     regs[11],
        //     regs[12],
        //     regs[13],
        //     regs[14],
        //     regs[15]
        // );

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

        printf("interrupt: cause = %08x, tval = %08x, pc = %08x\n", mcause, mtval, pc);

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

bool hart::csr_rw(u32 csr, u32 read_mask, u32 write_mask, u32& read_data, u32 write_data)
{
    bool r = false;

    if (priv != 0b11) {
        return false;
    }

    switch (csr) {
    case CSR_MVENDORID: // read only
        if (write_mask != 0) {
            r = false;
            break;
        }
        read_data = mvendorid & read_mask;
        r = true;
        break;

    case CSR_MARCHID: // read only
        if (write_mask != 0) {
            r = false;
            break;
        }
        read_data = marchid & read_mask;
        r = true;
        break;

    case CSR_MIMPID: // read only
        if (write_mask != 0) {
            r = false;
            break;
        }
        read_data = mimpid & read_mask;
        r = true;
        break;

    case CSR_MHARTID: // read only
        if (write_mask != 0) {
            r = false;
            break;
        }
        read_data = mhartid & read_mask;
        r = true;
        break;

    case CSR_MCONFIGPTR: // read only
        if (write_mask != 0) {
            r = false;
            break;
        }
        read_data = mconfigptr & read_mask;
        r = true;
        break;

    case CSR_MSTATUS:
        read_data = mstatus & read_mask;
        mstatus = (mstatus & ~write_mask) | (write_data & write_mask);
        mstatus &= 0x1888; // only MPP, MPIE, MIE

        // 00: user mode, 11: machine mode
        if (get_part(mstatus, 12, 11)) { // mode 01 and 10 is not supported, force 11
            mstatus |= create_mask(12, 11);
        }
        r = true;
        break;

    case CSR_MISA: // read only
        if (write_mask != 0) {
            r = false;
            break;
        }
        read_data = misa & read_mask;
        r = true;
        break;

    case CSR_MIE:
        read_data = mie & read_mask;
        mie = (mie & ~write_mask) | (write_data & write_mask);
        mie &= 0x888;
        r = true;
        break;

    case CSR_MTVEC:
        read_data = mtvec & read_mask;
        mtvec = (mtvec & ~write_mask) | (write_data & write_mask);
        mtvec &= ~create_mask(1, 1); // mode[1] reserved
        r = true;
        break;

    case CSR_MSTATUSH:
        read_data = mstatush & read_mask;
        r = true;
        break;

    case CSR_MSCRATCH:
        read_data = mscratch & read_mask;
        mscratch = (mscratch & ~write_mask) | (write_data & write_mask);
        r = true;
        break;

    case CSR_MEPC:
        read_data = mepc & read_mask;
        mepc = (mepc & ~write_mask) | (write_data & write_mask);
        mepc &= ~create_mask(1, 0); // mepc[1:0] is always zero
        r = true;
        break;

    case CSR_MCAUSE:
        read_data = mcause & read_mask;
        mcause = (mcause & ~write_mask) | (write_data & write_mask);
        r = true;
        break;

    case CSR_MTVAL:
        read_data = mtval & read_mask;
        mtval = (mtval & ~write_mask) | (write_data & write_mask);
        r = true;
        // TODO: should be readonly zero?
        break;

    case CSR_MIP:
        read_data = mip & read_mask;
        r = true;
        if (write_mask & (1 << 3)) {
            if (write_data & (1 << 3)) {
                set_msip(1);
            } else {
                set_msip(0);
            }
        }
        break;

    default:
        r = false;
        break;
    }

    return r;
}