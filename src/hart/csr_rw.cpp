
#include <hart.h>

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
        break;

    default:
        r = false;
        break;
    }

    return r;
}