
#include <hart.h>

hart::handler_t hart::decode_system()
{
    switch (funct3) {
        case FUNCT3_SYSTEM_OTHER: {
            if (rd != 0) {
                return &thunk<&hart::inst_illegal>;
            }

            if (get_part(inst, 31, 15) == FUNCT_31_15_SRET) {
                return &thunk<&hart::inst_sret>;
            }

            if (get_part(inst, 31, 15) == FUNCT_31_15_MRET) {
                return &thunk<&hart::inst_mret>;
            }

            if (get_part(inst, 31, 15) == FUNCT_31_15_WFI) {
                return &thunk<&hart::inst_wfi>;
            }

            if (get_part(inst, 31, 25) == FUNCT_31_25_SFENCE_VMA) {
                return &thunk<&hart::inst_sfence_vma>;
            }

            if (get_part(inst, 31, 15) == FUNCT_31_15_ECALL) {
                return &thunk<&hart::inst_ecall>;
            }

            if (get_part(inst, 31, 15) == FUNCT_31_15_EBREAK) {
                return &thunk<&hart::inst_ebreak>;
            }

            return &thunk<&hart::inst_illegal>;

        } break;

        case FUNCT3_CSRRW:  return &thunk<&hart::inst_csrrw>;
        case FUNCT3_CSRRS:  return &thunk<&hart::inst_csrrs>;
        case FUNCT3_CSRRC:  return &thunk<&hart::inst_csrrc>;
        case FUNCT3_CSRRWI: return &thunk<&hart::inst_csrrwi>;
        case FUNCT3_CSRRSI: return &thunk<&hart::inst_csrrsi>;
        case FUNCT3_CSRRCI: return &thunk<&hart::inst_csrrci>;
    }
    return &thunk<&hart::inst_illegal>;
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
        runtime_error("priv cannot be 0b10");
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

    if (!wfi_wake_pending()) {
        waiting = true;
    }
}

void hart::inst_mret() {
    if (priv != 0b11) {
        throw trap{trap_cause_t::illegal_instruction_exception, inst};
    }

    mstatus.MIE() = mstatus.MPIE();
    mstatus.MPIE() = 0b1;

    if (mstatus.MPP() != 0b11) {
        mstatus.MPRV() = 0b0;
    }

    priv = mstatus.MPP();
    mstatus.MPP() = 0b00;

    fast_check_context();
    irq_dirty = true;

    pc = mepc;
}

void hart::inst_sret() {
    // TODO: mstatus.TSR
    if (priv == 0b00) {
        throw trap{trap_cause_t::illegal_instruction_exception, inst};
    }
    mstatus.SIE()  = mstatus.SPIE();
    mstatus.SPIE() = 0b1;

    mstatus.MPRV() = 0b0;

    priv = mstatus.SPP();
    mstatus.SPP() = 0b0;

    fast_check_context();
    irq_dirty = true;

    pc = sepc;
}

void hart::inst_sfence_vma() {
    // TODO: mstatus.TVM

    if (priv == 0b00) {
        throw trap{trap_cause_t::illegal_instruction_exception, inst};
    }

    std::optional<u32> va;
    std::optional<u32> asid;

    if (rs1 != 0) {
        va = regs[rs1];
    }
    if (rs2 != 0) {
        asid = get_part(regs[rs2], 8, 0); // satp.ASID is 9 bits
    }

    fast_flush(); // the fast TLB has no ASID/global info, always drop everything

    for (hart_tlb_t* tlb : {itlb.get(), dtlb.get()}) {
        if (!tlb) {
            continue;
        }
        if (!va && !asid) {
            tlb->flush();
        } else {
            tlb->invalidate(va, asid);
        }
    }

    pc = pc + 4;
}

// TODO: csrrw to read only csr's should trap. csrrs and csrrc to read only csr's should trap if rs1 is not x0, even if the value of regs[rs1] is 0
// TODO: csrrw/csrrwi with rd=x0 must not read the csr and cause read side effects. Currently there are no read side effects.

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
    irq_dirty = true; // mstatus, mie, mip, mideleg and their s-mode views can change interrupt delivery

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
        case CSR_MCOUNTEREN: return csr_rw_mcounteren (rm, wm, rd, wd);

        case CSR_SSTATUS:    return csr_rw_sstatus    (rm, wm, rd, wd);
        case CSR_STVEC:      return csr_rw_stvec      (rm, wm, rd, wd);
        case CSR_SIP:        return csr_rw_sip        (rm, wm, rd, wd);
        case CSR_SIE:        return csr_rw_sie        (rm, wm, rd, wd);
        case CSR_SSCRATCH:   return csr_rw_sscratch   (rm, wm, rd, wd);
        case CSR_SEPC:       return csr_rw_sepc       (rm, wm, rd, wd);
        case CSR_SCAUSE:     return csr_rw_scause     (rm, wm, rd, wd);
        case CSR_STVAL:      return csr_rw_stval      (rm, wm, rd, wd);
        case CSR_SATP:       return csr_rw_satp       (rm, wm, rd, wd);
        case CSR_SCOUNTEREN: return csr_rw_scounteren (rm, wm, rd, wd);

        case CSR_TIME:       return csr_rw_time       (rm, rd, false);
        case CSR_TIMEH:      return csr_rw_time       (rm, rd, true);

        default: throw trap{trap_cause_t::illegal_instruction_exception, inst};
    };
}