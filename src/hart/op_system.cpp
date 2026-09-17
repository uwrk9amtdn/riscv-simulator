
#include <hart.h>

void hart::op_system()
{

    u32 csr = get_part(inst, 31, 20);
    bool b;
    u32 pc_next = pc + 4;
    switch (funct3) {
    case FUNCT3_ECALL_EBREAK_WFI_MRET:
        if (rd != 0 || rs1 != 0) {
            throw trap{trap_cause_t::illegal_instruction_exception, inst};
        }

        switch (funct7) {
        case FUNCT7_ECALL_EBREAK:
            switch (rs2) {
            case RS2_ECALL:
                if (priv == 0b11) { // m mode
                    throw trap{trap_cause_t::environment_call_from_m_mode_exception, 0};
                } else {
                    throw trap{trap_cause_t::environment_call_from_u_mode_exception, 0};
                }
                break;
            case RS2_EBREAK:
                throw trap{trap_cause_t::breakpoint_exception, 0};
                break;
            default:
                throw trap{trap_cause_t::illegal_instruction_exception, inst};
                break;
            }
            break;
        case FUNCT7_WFI:
            switch (rs2) {
            case RS2_WFI:
                break; // WFI implemented as NOP
            default:
                throw trap{trap_cause_t::illegal_instruction_exception, inst};
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
                throw trap{trap_cause_t::illegal_instruction_exception, inst};
                break;
            }
            break;
        default:
            throw trap{trap_cause_t::illegal_instruction_exception, inst};
            break;
        }
        break;
    case FUNCT3_CSRRW:
        csr_rw(csr, -1, -1, regs[rd], regs[rs1]);
        break;
    case FUNCT3_CSRRS:
        csr_rw(csr, -1, regs[rs1], regs[rd], -1);
        break;
    case FUNCT3_CSRRC:
        csr_rw(csr, -1, regs[rs1], regs[rd], 0);
        break;
    case FUNCT3_CSRRWI:
        csr_rw(csr, -1, -1, regs[rd], rs1);
        break;
    case FUNCT3_CSRRSI:
        csr_rw(csr, -1, rs1, regs[rd], -1);
        break;
    case FUNCT3_CSRRCI:
        csr_rw(csr, -1, rs1, regs[rd], 0);
        break;
    default:
        throw trap{trap_cause_t::illegal_instruction_exception, inst};
        break;
    }
    pc = pc_next;
}

void hart::csr_rw(u32 csr, u32 read_mask, u32 write_mask, u32& read_data, u32 write_data)
{
    // TODO: privilege check
    switch (csr) {
        case CSR_MISA:       return csr_rw_misa       (read_mask, write_mask, read_data, write_data);
        case CSR_MVENDORID:  return csr_rw_mvendorid  (read_mask, write_mask, read_data, write_data);
        case CSR_MARCHID:    return csr_rw_marchid    (read_mask, write_mask, read_data, write_data);
        case CSR_MIMPID:     return csr_rw_mimpid     (read_mask, write_mask, read_data, write_data);
        case CSR_MHARTID:    return csr_rw_mhartid    (read_mask, write_mask, read_data, write_data);
        case CSR_MCONFIGPTR: return csr_rw_mconfigptr (read_mask, write_mask, read_data, write_data);
        case CSR_MSTATUS:    return csr_rw_mstatus    (read_mask, write_mask, read_data, write_data);
        case CSR_MIE:        return csr_rw_mie        (read_mask, write_mask, read_data, write_data);
        case CSR_MTVEC:      return csr_rw_mtvec      (read_mask, write_mask, read_data, write_data);
        case CSR_MSTATUSH:   return csr_rw_mstatush   (read_mask, write_mask, read_data, write_data);
        case CSR_MSCRATCH:   return csr_rw_mscratch   (read_mask, write_mask, read_data, write_data);
        case CSR_MEPC:       return csr_rw_mepc       (read_mask, write_mask, read_data, write_data);
        case CSR_MCAUSE:     return csr_rw_mcause     (read_mask, write_mask, read_data, write_data);
        case CSR_MTVAL:      return csr_rw_mtval      (read_mask, write_mask, read_data, write_data);
        case CSR_MIP:        return csr_rw_mip        (read_mask, write_mask, read_data, write_data);

        default: throw trap{trap_cause_t::illegal_instruction_exception, inst};
    };
}