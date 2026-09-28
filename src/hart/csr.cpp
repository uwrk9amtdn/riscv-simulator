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
    mstatus = (mstatus & ~wm) | (wd & wm);
    mstatus &= 0x1888; // only MPP, MPIE, MIE

    // 00: user mode, 11: machine mode
    if (mstatus.MPP()) { // mode 01 and 10 is not supported, force 11
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
    mtvec = (mtvec & ~wm) | (wd & wm);
    mtvec.field<1>() = 0; // mode[1] reserved
}

void hart::csr_rw_mip (u32 rm, u32 wm, u32& rd, u32 wd) {
    rd = mip & rm;
}

void hart::csr_rw_mie (u32 rm, u32 wm, u32& rd, u32 wd) {
    rd = mie & rm;
    mie = (mie & ~wm) | (wd & wm);
    mie &= 0x888;
}

void hart::csr_rw_mscratch (u32 rm, u32 wm, u32& rd, u32 wd) {
    rd = mscratch & rm;
    mscratch = (mscratch & ~wm) | (wd & wm);
}

void hart::csr_rw_mepc (u32 rm, u32 wm, u32& rd, u32 wd) {
    rd = mepc & rm;
    mepc = (mepc & ~wm) | (wd & wm);
    mepc.field<1,0>() = 0; // mepc[1:0] is always zero
}

void hart::csr_rw_mcause (u32 rm, u32 wm, u32& rd, u32 wd) {
    rd = mcause & rm;
    mcause = (mcause & ~wm) | (wd & wm);
}

void hart::csr_rw_mtval (u32 rm, u32 wm, u32& rd, u32 wd) {
    rd = mtval & rm;
    mtval = (mtval & ~wm) | (wd & wm);
}

void hart::csr_rw_sstatus (u32 rm, u32 wm, u32& rd, u32 wd) {

}

void hart::csr_rw_sip (u32 rm, u32 wm, u32& rd, u32 wd) {

}

void hart::csr_rw_sie (u32 rm, u32 wm, u32& rd, u32 wd) {

}

void hart::csr_rw_stvec (u32 rm, u32 wm, u32& rd, u32 wd) {
    rd = stvec & rm;
    stvec = (stvec & ~wm) | (wd & wm);
    stvec.field<1>() = 0; // mode[1] reserved
}

void hart::csr_rw_sscratch (u32 rm, u32 wm, u32& rd, u32 wd) {
    rd = sscratch & rm;
    sscratch = (sscratch & ~wm) | (wd & wm);
}

void hart::csr_rw_sepc (u32 rm, u32 wm, u32& rd, u32 wd) {
    rd = sepc & rm;
    sepc = (sepc & ~wm) | (wd & wm);
    sepc.field<1,0>() = 0; // sepc[1:0] is always zero
}

void hart::csr_rw_scause (u32 rm, u32 wm, u32& rd, u32 wd) {
    rd = stval & rm;
    stval = (stval & ~wm) | (wd & wm);
}

void hart::csr_rw_stval (u32 rm, u32 wm, u32& rd, u32 wd) {
    rd = stval & rm;
    stval = (stval & ~wm) | (wd & wm);
}

void hart::csr_rw_satp (u32 rm, u32 wm, u32& rd, u32 wd) {

}
