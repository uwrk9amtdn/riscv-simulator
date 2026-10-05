
#include <hart.h>
#include <stdexcept>

void hart::decode_system()
{
    switch (funct3) {
        case FUNCT3_SYSTEM_OTHER: {
            if (rd != 0) {
                throw trap{trap_cause_t::illegal_instruction_exception, inst};
            }

            if (get_part(inst, 31, 15) == FUNCT_31_15_SRET) {
                return inst_sret();
            }

            if (get_part(inst, 31, 15) == FUNCT_31_15_MRET) {
                return inst_mret();
            }

            if (get_part(inst, 31, 15) == FUNCT_31_15_WFI) {
                return inst_wfi();
            }

            if (get_part(inst, 31, 25) == FUNCT_31_25_SFENCE_VMA) {
                return inst_sfence_vma();
            }

            if (get_part(inst, 31, 15) == FUNCT_31_15_ECALL) {
                return inst_ecall();
            }

            if (get_part(inst, 31, 15) == FUNCT_31_15_EBREAK) {
                return inst_ebreak();
            }

            throw trap{trap_cause_t::illegal_instruction_exception, inst};

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
    if (priv == 0b00) {
        throw trap{trap_cause_t::environment_call_from_u_mode_exception, 0};
    }

    if (priv == 0b01) {
        throw trap{trap_cause_t::environment_call_from_s_mode_exception, 0};
    }

    if (priv == 0b11) {
        throw trap{trap_cause_t::environment_call_from_m_mode_exception, 0};
    }

    if (priv == 0b10) {
        throw std::runtime_error("priv cannot be 0b10");
    }

}

void hart::inst_ebreak() {
    throw trap{trap_cause_t::breakpoint_exception, 0};
}

void hart::inst_wfi() {
    // TODO: mstatus.TW
    if (priv == 0b00) {
        throw trap{trap_cause_t::illegal_instruction_exception, inst};
    }
    pc = pc + 4;
}

void hart::inst_mret() {
    // TODO: mstatus.MPRV
    if (priv != 0b11) {
        throw trap{trap_cause_t::illegal_instruction_exception, inst};
    }

    mstatus.MIE() = mstatus.MPIE();
    mstatus.MPIE() = 0b1;

    priv = mstatus.MPP();
    mstatus.MPP() = 0b00;

    pc = mepc;
}

void hart::inst_sret() {
    // TODO: mstatus.TSR
    // TODO: mstatus.MPRV
    if (priv == 0b00) {
        throw trap{trap_cause_t::illegal_instruction_exception, inst};
    }
    mstatus.SIE()  = mstatus.SPIE();
    mstatus.SPIE() = 0b1;

    priv = mstatus.SPP();
    mstatus.SPP() = 0b0;

    pc = sepc;
}

void hart::inst_sfence_vma() {
    // TODO: mstatus.TVM

    if (priv == 0b00) {
        throw trap{trap_cause_t::illegal_instruction_exception, inst};
    }
    pc = pc + 4;
}

// TODO: csrrw to read only csr's should trap. csrrs and csrrc to read only csr's should trap if rs1 is not x0, even if the value of regs[rs1] is 0

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

void hart::csr_rw(u32 csr, u32 rm, u32 wm, u32& rd, u32 wd)
{

    u32 lowest_priv = get_part(csr, 9, 8);
    u32 access_type = get_part(csr, 11, 10);

    switch (lowest_priv) {
        case 0b11: // machine
            if (priv != 0b11) {
                throw trap{trap_cause_t::illegal_instruction_exception, inst};
            }
            break;

        case 0b01: // supervisor
            if (priv == 0b00) {
                throw trap{trap_cause_t::illegal_instruction_exception, inst};
            }
            break;

        case 0b10:
            throw trap{trap_cause_t::illegal_instruction_exception, inst};
            break;

        case 0b00:
            break;
    }

    if (access_type == 0b11 && wm != 0) {
        throw trap{trap_cause_t::illegal_instruction_exception, inst};
    }

    switch (csr) {
        case CSR_MISA:       return csr_rw_misa       (rm, wm, rd, wd);
        case CSR_MVENDORID:  return csr_rw_mvendorid  (rm, wm, rd, wd);
        case CSR_MARCHID:    return csr_rw_marchid    (rm, wm, rd, wd);
        case CSR_MIMPID:     return csr_rw_mimpid     (rm, wm, rd, wd);
        case CSR_MHARTID:    return csr_rw_mhartid    (rm, wm, rd, wd);
        case CSR_MCONFIGPTR: return csr_rw_mconfigptr (rm, wm, rd, wd);
        case CSR_MSTATUS:    return csr_rw_mstatus    (rm, wm, rd, wd);
        case CSR_MIE:        return csr_rw_mie        (rm, wm, rd, wd);
        case CSR_MTVEC:      return csr_rw_mtvec      (rm, wm, rd, wd);
        case CSR_MSTATUSH:   return csr_rw_mstatush   (rm, wm, rd, wd);
        case CSR_MSCRATCH:   return csr_rw_mscratch   (rm, wm, rd, wd);
        case CSR_MEPC:       return csr_rw_mepc       (rm, wm, rd, wd);
        case CSR_MCAUSE:     return csr_rw_mcause     (rm, wm, rd, wd);
        case CSR_MTVAL:      return csr_rw_mtval      (rm, wm, rd, wd);
        case CSR_MIP:        return csr_rw_mip        (rm, wm, rd, wd);
        case CSR_MEDELEG:    return csr_rw_medeleg    (rm, wm, rd, wd);
        case CSR_MIDELEG:    return csr_rw_mideleg    (rm, wm, rd, wd);

        case CSR_SSTATUS:    return csr_rw_sstatus    (rm, wm, rd, wd);
        case CSR_STVEC:      return csr_rw_stvec      (rm, wm, rd, wd);
        case CSR_SIP:        return csr_rw_sip        (rm, wm, rd, wd);
        case CSR_SIE:        return csr_rw_sie        (rm, wm, rd, wd);
        case CSR_SSCRATCH:   return csr_rw_sscratch   (rm, wm, rd, wd);
        case CSR_SEPC:       return csr_rw_sepc       (rm, wm, rd, wd);
        case CSR_SCAUSE:     return csr_rw_scause     (rm, wm, rd, wd);
        case CSR_STVAL:      return csr_rw_stval      (rm, wm, rd, wd);
        case CSR_SATP:       return csr_rw_satp       (rm, wm, rd, wd);

        default: throw trap{trap_cause_t::illegal_instruction_exception, inst};
    };
}