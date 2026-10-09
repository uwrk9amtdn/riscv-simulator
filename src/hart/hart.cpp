#include "hart.h"

hart::hart(::mmio* mmio, u32 mhartid, u32 pc)
{
    this->mmio_ = mmio;
    this->mhartid = mhartid;
    this->pc = pc;

    mstatus = 0;
    mstatus.MPP() = 0b11;

    misa = 0x40141101; // MXL = 32, extensions: A, I, M, S, U
}

void hart::step()
{
    try {

        {
            u32 mip_ = mip | (seip ? 1 << 9 : 0);
            u32 mp = mip_ & mie & ~mideleg;
            u32 sp = mip_ & mie & mideleg;

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

    }

    regs[0] = 0;
}

void hart::set_meip(bool level)
{
    mip.MEIP() = level;
}

void hart::set_seip(bool level)
{
    seip = level;
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

u32 hart::sv32_ptw(u32 va, access_type_t access_type, u32 priv) {
    u32 pte;
    u32 ppa;
    u32 vpn[2];

    trap_cause_t access_fault_exception = access_type_to_access_fault_exception(access_type);
    trap_cause_t page_fault_exception   = access_type_to_page_fault_exception(access_type);

    vpn[1] = get_part(va, 31, 22);
    vpn[0] = get_part(va, 21, 12);

    ppa = satp.PPN() << 12;

    // if (get_part(satp, 21, 20)) {
    //     runtime_error("only 32 bit physical address supported");
    // }

    for (int i = 1; i >= 0; i--) {
        u32 a = ppa + vpn[i] * 4;
        if (!mmio_->load(a, 4, (u8*)&pte)) {
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
            runtime_error("only 32 bit physical address supported");
        }

        if (xwr == 0b000) {
            // TODO: U, A, D bits must be zero in non leaf PTEs
            ppa = get_part(pte, 29, 10, 12);
            continue;
        }

        if (mstatus.MXR()) {
            xwr |= xwr >> 2;
        }
        if (!(static_cast<u32>(access_type) & xwr)) {
            throw trap{page_fault_exception, va};
        }

        if (get_bit(pte, 4)) { // U
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
            if (!mmio_->store(a, 4, (u8*)&pte)) {
                throw trap{access_fault_exception, va};
            }
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

    u32 _priv = priv;

    _priv = (access_type != access_type_t::x && mstatus.MPRV()) ? mstatus.MPP() : priv;

    if (get_bit(_priv, 1) == 0b0 && satp.MODE() == 0b1) {
        return sv32_ptw(addr, access_type, _priv);
    } else {
        return addr;
    }
}

void hart::load(u32 addr, u32 len, u8* data, access_type_t access_type) {
    trap_cause_t access_fault_exception = access_type_to_access_fault_exception(access_type);

    u32 pa = resolve_addr(addr, access_type);

    if (!mmio_->load(pa, len, data)) {
        throw trap{access_fault_exception, addr};
    }
}

void hart::store(u32 addr, u32 len, const u8* data, access_type_t access_type) {
    trap_cause_t access_fault_exception = access_type_to_access_fault_exception(access_type);

    u32 pa = resolve_addr(addr, access_type);

    if (!mmio_->store(pa, len, data)) {
        throw trap{access_fault_exception, addr};
    }
}