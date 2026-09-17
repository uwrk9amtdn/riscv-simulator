#include <hart.h>

void hart::op_load()
{
    u32 addr;
    u32 data;
    i8 di8;
    i16 di16;
    u8 du8;
    u16 du16;

    addr = regs[rs1] + get_part_s(inst, 31, 20);

    switch (funct3) {
    case FUNCT3_LB:
        load(addr, 1, (u8*)&di8);
        data = di8;
        break;
    case FUNCT3_LH:
        load(addr, 2, (u8*)&di16);
        data = di16;
        break;
    case FUNCT3_LW:
        load(addr, 4, (u8*)&data);
        break;
    case FUNCT3_LBU:
        load(addr, 1, (u8*)&du8);
        data = du8;
        break;
    case FUNCT3_LHU:
        load(addr, 2, (u8*)&du16);
        data = du16;
        break;
    default:
        throw trap{trap_cause_t::illegal_instruction_exception, inst};
        break;
    }

    regs[rd] = data;
    pc = pc + 4;
}