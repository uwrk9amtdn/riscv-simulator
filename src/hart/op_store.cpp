#include <hart.h>

void hart::decode_store()
{
    switch (funct3) {
        case FUNCT3_SB: return inst_sb();
        case FUNCT3_SH: return inst_sh();
        case FUNCT3_SW: return inst_sw();
    }

    throw trap{trap_cause_t::illegal_instruction_exception, inst};
}

void hart::inst_sb() {
    u32 addr;
    u32 data;

    addr = regs[rs1] + get_part_s(inst, 31, 25, 5) + get_part(inst, 11, 7);
    data = regs[rs2];
    store(addr, 1, (u8*)&data);

    pc = pc + 4;
}

void hart::inst_sh() {
    u32 addr;
    u32 data;

    addr = regs[rs1] + get_part_s(inst, 31, 25, 5) + get_part(inst, 11, 7);
    data = regs[rs2];
    store(addr, 2, (u8*)&data);

    pc = pc + 4;
}

void hart::inst_sw() {
    u32 addr;
    u32 data;

    addr = regs[rs1] + get_part_s(inst, 31, 25, 5) + get_part(inst, 11, 7);
    data = regs[rs2];
    store(addr, 4, (u8*)&data);

    pc = pc + 4;
}
