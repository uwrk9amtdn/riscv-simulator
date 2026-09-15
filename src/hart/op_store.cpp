#include <hart.h>

void hart::op_store()
{
    u32 addr;
    u32 data;

    addr = regs[rs1] + get_part_s(inst, 31, 25, 5) + get_part(inst, 11, 7);

    switch (funct3) {
    case FUNCT3_SB:
        data = regs[rs2];
        store(addr, 1, (u8*)&data);
        break;
    case FUNCT3_SH:
        data = regs[rs2];
        store(addr, 2, (u8*)&data);
        break;
    case FUNCT3_SW:
        data = regs[rs2];
        store(addr, 4, (u8*)&data);
        break;
    default:
        throw trap{MCAUSE_ILLEGAL_INSTRUCTION_EXCEPTION, inst};
        break;
    }

    pc = pc + 4;
}