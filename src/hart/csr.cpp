#include "common.h"
#include <hart.h>

void hart::csr_rw_misa (u32 rm, u32 wm, u32& rd, u32 wd) {
    rd = misa & rm;
}

void hart::csr_rw_mvendorid (u32 rm, u32 wm, u32& rd, u32 wd) {
    rd = mvendorid & rm;
}

void hart::csr_rw_marchid (u32 rm, u32 wm, u32& rd, u32 wd) {
    rd = marchid & rm;
}

void hart::csr_rw_mimpid (u32 rm, u32 wm, u32& rd, u32 wd) {
    rd = mimpid & rm;
}

void hart::csr_rw_mhartid (u32 rm, u32 wm, u32& rd, u32 wd) {
    rd = mhartid & rm;
}

void hart::csr_rw_mstatus (u32 rm, u32 wm, u32& rd, u32 wd) {
    rd = mstatus & rm;
    mstatus = set_with_mask(mstatus, wd, wm);

    mstatus &= 0x7E19AA; // TSR, TW, TWM, MXR, SUM, MPRV, MPP, SPP, MPIE, SPIE, MIE, SIE

    if (mstatus.TVM() || mstatus.TW() || mstatus.TSR()) {
        runtime_error("not implemented yet");
    }

    // 00: user, 01: supervisor, 11: machine
    if (mstatus.MPP() == 0b10) { // 10 is not supported, force 11
        mstatus.MPP() = 0b11;
    }
}

void hart::csr_rw_mstatush (u32 rm, u32 wm, u32& rd, u32 wd) {
    rd = mstatush & rm;
}

void hart::csr_rw_mconfigptr (u32 rm, u32 wm, u32& rd, u32 wd) {
    rd = mconfigptr & rm;
}

void hart::csr_rw_mtvec (u32 rm, u32 wm, u32& rd, u32 wd) {
    rd = mtvec & rm;
    mtvec = set_with_mask(mtvec, wd, wm);
    mtvec.field<1>() = 0; // mode[1] reserved
}

void hart::csr_rw_mip (u32 rm, u32 wm, u32& rd, u32 wd) {
    u32 mip_ = mip | (seip ? 1 << 9 : 0);
    rd = mip_ & rm;
    mip = set_with_mask(mip, wd, wm & 0x222); // SSIP, STIP, SEIP
}

void hart::csr_rw_mie (u32 rm, u32 wm, u32& rd, u32 wd) {
    rd = mie & rm;
    mie = set_with_mask(mie, wd, wm & 0xaaa); // SSIE, MSIE, STIE, MTIE, SEIE, MEIE
}

void hart::csr_rw_mscratch (u32 rm, u32 wm, u32& rd, u32 wd) {
    rd = mscratch & rm;
    mscratch = set_with_mask(mscratch, wd, wm);
}

void hart::csr_rw_mepc (u32 rm, u32 wm, u32& rd, u32 wd) {
    rd = mepc & rm;
    mepc = set_with_mask(mepc, wd, wm);
    mepc.field<1,0>() = 0; // mepc[1:0] is always zero
}

void hart::csr_rw_mcause (u32 rm, u32 wm, u32& rd, u32 wd) {
    rd = mcause & rm;
    mcause = set_with_mask(mcause, wd, wm);
}

void hart::csr_rw_mtval (u32 rm, u32 wm, u32& rd, u32 wd) {
    rd = mtval & rm;
    mtval = set_with_mask(mtval, wd, wm);
}

void hart::csr_rw_medeleg (u32 rm, u32 wm, u32& rd, u32 wd) {
    rd = medeleg & rm;
    medeleg = set_with_mask(medeleg, wd, wm);
    medeleg &= 0xb3ff; // ecall from m mode is not delegateable
}

void hart::csr_rw_mideleg (u32 rm, u32 wm, u32& rd, u32 wd) {
    rd = mideleg & rm;
    mideleg = set_with_mask(mideleg, wd, wm);
    mideleg &= 0x222; // M mode interrupts are not delegateable
}

void hart::csr_rw_mcounteren (u32 rm, u32 wm, u32& rd, u32 wd) {
    // counters are not implemented, reads of time always trap to m mode.
    // TM is writable so that the sbi firmware knows it is allowed to emulate time for s mode
    rd = mcounteren & rm;
    mcounteren = set_with_mask(mcounteren, wd, wm & 0x2); // TM
}

void hart::csr_rw_sstatus (u32 rm, u32 wm, u32& rd, u32 wd) {
    u32 mask = 0x0c0122;
    rd = (mstatus & rm & mask);
    mstatus = set_with_mask(mstatus, wd, wm & mask);
}

void hart::csr_rw_sip (u32 rm, u32 wm, u32& rd, u32 wd) {
    // only delegated
    u32 mip_ = mip | (seip ? 1 << 9 : 0);
    rd = mip_ & rm & mideleg;
    mip = set_with_mask(mip, wd, wm & mideleg & 0x2); // only ssip is writable if delegated
}

void hart::csr_rw_sie (u32 rm, u32 wm, u32& rd, u32 wd) {
    // only delegated
    rd = mie & rm & mideleg;
    mie = set_with_mask(mie, wd, wm & mideleg);
}

void hart::csr_rw_stvec (u32 rm, u32 wm, u32& rd, u32 wd) {
    rd = stvec & rm;
    stvec = set_with_mask(stvec, wd, wm);
    stvec.field<1>() = 0; // mode[1] reserved
}

void hart::csr_rw_sscratch (u32 rm, u32 wm, u32& rd, u32 wd) {
    rd = sscratch & rm;
    sscratch = set_with_mask(sscratch, wd, wm);
}

void hart::csr_rw_sepc (u32 rm, u32 wm, u32& rd, u32 wd) {
    rd = sepc & rm;
    sepc = set_with_mask(sepc, wd, wm);
    sepc.field<1,0>() = 0; // sepc[1:0] is always zero
}

void hart::csr_rw_scause (u32 rm, u32 wm, u32& rd, u32 wd) {
    rd = scause & rm;
    scause = set_with_mask(scause, wd, wm);
}

void hart::csr_rw_stval (u32 rm, u32 wm, u32& rd, u32 wd) {
    rd = stval & rm;
    stval = set_with_mask(stval, wd, wm);
}

void hart::csr_rw_satp (u32 rm, u32 wm, u32& rd, u32 wd) {
    // TODO: mstatus.TVM
    rd = satp & rm;
    satp = set_with_mask(satp, wd, wm);
    satp.field<21,20>() = 0b00;
}

void hart::csr_rw_scounteren (u32 rm, u32 wm, u32& rd, u32 wd) {
    rd = scounteren & rm;
    scounteren = set_with_mask(scounteren, wd, wm & 0x2); // TM
}
