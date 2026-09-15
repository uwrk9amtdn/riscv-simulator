
#include <hart.h>

void hart::op_amo()
{
    u32 funct5 = get_part(inst, 31, 27);
    // u32 aq = get_part(inst, 26, 26);
    // u32 rl = get_part(inst, 25, 25);

    u32 addr;
    u32 ldata;
    u32 sdata;

    if (funct3 != FUNCT3_AMO) {
        throw trap{MCAUSE_ILLEGAL_INSTRUCTION_EXCEPTION, inst};
    }

    switch (funct5) {
    case FUNCT5_LR_W:
        if (rs2 != 0) {
            throw trap{MCAUSE_ILLEGAL_INSTRUCTION_EXCEPTION, inst};
        }

        addr = regs[rs1];

        if (addr & 0x03) {
            throw trap{MCAUSE_LOAD_ADDRESS_MISALIGNED_EXCEPTION, addr};
        }

       load(addr, 4, (u8*)&ldata, MCAUSE_STORE_AMO_ACCESS_FAULT_EXCEPTION);

        regs[rd] = ldata;

        reservation_set = true;
        reserved_addr = addr;

        break;
    case FUNCT5_SC_W:

        addr = regs[rs1];
        sdata = regs[rs2];

        if (addr & 0x03) {
            throw trap{MCAUSE_STORE_AMO_ADDRESS_MISALIGNED_EXCEPTION, addr};
        }

        if (reservation_set && reserved_addr == addr) {
            store(addr, 4, (u8*)&sdata);

            regs[rd] = 0;
            reservation_set = false;
        } else {
            regs[rd] = 1;
        }

        break;
    case FUNCT5_AMOSWAP_W:
    case FUNCT5_AMOADD_W:
    case FUNCT5_AMOXOR_W:
    case FUNCT5_AMOAND_W:
    case FUNCT5_AMOOR_W:
    case FUNCT5_AMOMIN_W:
    case FUNCT5_AMOMAX_W:
    case FUNCT5_AMOMINU_W:
    case FUNCT5_AMOMAXU_W:
        addr = regs[rs1];

        if (addr & 0x03) {
            throw trap{MCAUSE_STORE_AMO_ADDRESS_MISALIGNED_EXCEPTION, addr};
        }

        load(addr, 4, (u8*)&ldata, MCAUSE_STORE_AMO_ACCESS_FAULT_EXCEPTION);

        // clang-format off
        switch (funct5) {
        case FUNCT5_AMOSWAP_W: sdata = regs[rs2];                                         break;
        case FUNCT5_AMOADD_W:  sdata = ldata + regs[rs2];                                 break;
        case FUNCT5_AMOXOR_W:  sdata = ldata ^ regs[rs2];                                 break;
        case FUNCT5_AMOAND_W:  sdata = ldata & regs[rs2];                                 break;
        case FUNCT5_AMOOR_W:   sdata = ldata | regs[rs2];                                 break;
        case FUNCT5_AMOMIN_W:  sdata = ((i32)ldata > (i32)regs[rs2]) ? regs[rs2] : ldata; break;
        case FUNCT5_AMOMAX_W:  sdata = ((i32)ldata > (i32)regs[rs2]) ? ldata : regs[rs2]; break;
        case FUNCT5_AMOMINU_W: sdata = (ldata > regs[rs2]) ? regs[rs2] : ldata;           break;
        case FUNCT5_AMOMAXU_W: sdata = (ldata > regs[rs2]) ? ldata : regs[rs2];           break;
        default: break;
        }
        // clang-format on

        store(addr, 4, (u8*)&sdata);

        regs[rd] = ldata;

        break;
    default:
        throw trap{MCAUSE_ILLEGAL_INSTRUCTION_EXCEPTION, inst};
        break;
    }

    pc = pc + 4;
}