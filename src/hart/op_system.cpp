
#include <hart.h>

void hart::decode_system()
{
    switch (funct3) {
        case FUNCT3_ECALL_EBREAK_WFI_MRET: {
            if (rd != 0 || rs1 != 0) {
                throw trap{trap_cause_t::illegal_instruction_exception, inst};
            }

            switch (funct7) {

                case FUNCT7_ECALL_EBREAK: {
                    switch (rs2) {
                        case RS2_ECALL:  return inst_ecall();
                        case RS2_EBREAK: return inst_ebreak();
                    }
                    throw trap{trap_cause_t::illegal_instruction_exception, inst}; break;
                } break;

                case FUNCT7_WFI: {
                    switch (rs2) {
                        case RS2_WFI: return inst_wfi();
                    }
                    throw trap{trap_cause_t::illegal_instruction_exception, inst};
                } break;

                case FUNCT7_MRET: {
                    switch (rs2) {
                        case RS2_MRET: return inst_mret();
                    }
                    throw trap{trap_cause_t::illegal_instruction_exception, inst}; break;
                } break;
            }
            throw trap{trap_cause_t::illegal_instruction_exception, inst}; break;

        } break;

        case FUNCT3_CSRRW:  return inst_csrrw ();
        case FUNCT3_CSRRS:  return inst_csrrs ();
        case FUNCT3_CSRRC:  return inst_csrrc ();
        case FUNCT3_CSRRWI: return inst_csrrwi();
        case FUNCT3_CSRRSI: return inst_csrrsi();
        case FUNCT3_CSRRCI: return inst_csrrci();
    }
    throw trap{trap_cause_t::illegal_instruction_exception, inst};
}

void hart::inst_ecall() {
    if (priv == 0b11) { // m mode
        throw trap{trap_cause_t::environment_call_from_m_mode_exception, 0};
    } else {
        throw trap{trap_cause_t::environment_call_from_u_mode_exception, 0};
    }
}

void hart::inst_ebreak() {
    throw trap{trap_cause_t::breakpoint_exception, 0};
}

void hart::inst_wfi() {
    pc = pc + 4;
}

void hart::inst_mret() {

    mstatus.MIE() = mstatus.MPIE();
    mstatus.MPIE() = 0b1;

    priv = mstatus.MPP();
    mstatus.MPP() = 0b00;

    pc = mepc;
}

void hart::inst_csrrw() {
    u32 csr = get_part(inst, 31, 20);
    csr_rw(csr, -1, -1, regs[rd], regs[rs1]);
    pc = pc + 4;
}

void hart::inst_csrrs() {
    u32 csr = get_part(inst, 31, 20);
    csr_rw(csr, -1, regs[rs1], regs[rd], -1);
    pc = pc + 4;
}

void hart::inst_csrrc() {
    u32 csr = get_part(inst, 31, 20);
    csr_rw(csr, -1, regs[rs1], regs[rd], 0);
    pc = pc + 4;
}

void hart::inst_csrrwi() {
    u32 csr = get_part(inst, 31, 20);
    csr_rw(csr, -1, -1, regs[rd], rs1);
    pc = pc + 4;
}

void hart::inst_csrrsi() {
    u32 csr = get_part(inst, 31, 20);
    csr_rw(csr, -1, rs1, regs[rd], -1);
    pc = pc + 4;
}

void hart::inst_csrrci() {
    u32 csr = get_part(inst, 31, 20);
    csr_rw(csr, -1, rs1, regs[rd], 0);
    pc = pc + 4;
}

void hart::csr_rw(u32 csr, u32 read_mask, u32 write_mask, u32& read_data, u32 write_data)
{
    // TODO: privilege check

    if (priv != 0b11) throw trap{trap_cause_t::illegal_instruction_exception, inst};

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