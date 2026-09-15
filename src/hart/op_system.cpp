
#include <hart.h>

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