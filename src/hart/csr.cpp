#include <hart.h>

void hart::csr_rw_misa (u32 read_mask, u32 write_mask, u32& read_data, u32 write_data) {
    // read only
    if (write_mask != 0) {
        throw trap{trap_cause_t::illegal_instruction_exception, inst};
    }
    read_data = misa & read_mask;
}

void hart::csr_rw_mvendorid (u32 read_mask, u32 write_mask, u32& read_data, u32 write_data) {
    // read only
    if (write_mask != 0) {
        throw trap{trap_cause_t::illegal_instruction_exception, inst};
    }
    read_data = mvendorid & read_mask;
}

void hart::csr_rw_marchid (u32 read_mask, u32 write_mask, u32& read_data, u32 write_data) {
    // read only
    if (write_mask != 0) {
        throw trap{trap_cause_t::illegal_instruction_exception, inst};
    }
    read_data = marchid & read_mask;
}

void hart::csr_rw_mimpid (u32 read_mask, u32 write_mask, u32& read_data, u32 write_data) {
    if (write_mask != 0) {
        throw trap{trap_cause_t::illegal_instruction_exception, inst};
    }
    read_data = mimpid & read_mask;
}

void hart::csr_rw_mhartid (u32 read_mask, u32 write_mask, u32& read_data, u32 write_data) {
    if (write_mask != 0) {
        throw trap{trap_cause_t::illegal_instruction_exception, inst};
    }
    read_data = mhartid & read_mask;
}

void hart::csr_rw_mstatus (u32 read_mask, u32 write_mask, u32& read_data, u32 write_data) {
    read_data = mstatus & read_mask;
    mstatus = (mstatus & ~write_mask) | (write_data & write_mask);
    mstatus &= 0x1888; // only MPP, MPIE, MIE

    // 00: user mode, 11: machine mode
    if (get_part(mstatus, 12, 11)) { // mode 01 and 10 is not supported, force 11
        mstatus |= create_mask(12, 11);
    }
}

void hart::csr_rw_mstatush (u32 read_mask, u32 write_mask, u32& read_data, u32 write_data) {
    read_data = mstatush & read_mask;
}

void hart::csr_rw_mconfigptr (u32 read_mask, u32 write_mask, u32& read_data, u32 write_data) {
    if (write_mask != 0) {
        throw trap{trap_cause_t::illegal_instruction_exception, inst};
    }
    read_data = mconfigptr & read_mask;
}

void hart::csr_rw_mtvec (u32 read_mask, u32 write_mask, u32& read_data, u32 write_data) {
    read_data = mtvec & read_mask;
    mtvec = (mtvec & ~write_mask) | (write_data & write_mask);
    mtvec &= ~create_mask(1, 1); // mode[1] reserved
}

void hart::csr_rw_mip (u32 read_mask, u32 write_mask, u32& read_data, u32 write_data) {
    read_data = mip & read_mask;
}

void hart::csr_rw_mie (u32 read_mask, u32 write_mask, u32& read_data, u32 write_data) {
    read_data = mie & read_mask;
    mie = (mie & ~write_mask) | (write_data & write_mask);
    mie &= 0x888;
}

void hart::csr_rw_mscratch (u32 read_mask, u32 write_mask, u32& read_data, u32 write_data) {
    read_data = mscratch & read_mask;
    mscratch = (mscratch & ~write_mask) | (write_data & write_mask);
}

void hart::csr_rw_mepc (u32 read_mask, u32 write_mask, u32& read_data, u32 write_data) {
    read_data = mepc & read_mask;
    mepc = (mepc & ~write_mask) | (write_data & write_mask);
    mepc &= ~create_mask(1, 0); // mepc[1:0] is always zero
}

void hart::csr_rw_mcause (u32 read_mask, u32 write_mask, u32& read_data, u32 write_data) {
    read_data = mcause & read_mask;
    mcause = (mcause & ~write_mask) | (write_data & write_mask);
}

void hart::csr_rw_mtval (u32 read_mask, u32 write_mask, u32& read_data, u32 write_data) {
    read_data = mtval & read_mask;
    mtval = (mtval & ~write_mask) | (write_data & write_mask);
    // TODO: should be readonly zero?
}