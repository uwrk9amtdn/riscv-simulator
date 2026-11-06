#include <hart.h>

hart::handler_t hart::decode_load()
{
    switch (funct3) {
        case FUNCT3_LB:  return &thunk<&hart::inst_lb>;
        case FUNCT3_LH:  return &thunk<&hart::inst_lh>;
        case FUNCT3_LW:  return &thunk<&hart::inst_lw>;
        case FUNCT3_LBU: return &thunk<&hart::inst_lbu>;
        case FUNCT3_LHU: return &thunk<&hart::inst_lhu>;
    }
    return &thunk<&hart::inst_illegal>;
}

void hart::inst_lb() {
    i8 di8;
    u32 addr;
    u32 data;
    addr = regs[rs1] + imm;
    load(addr, 1, (u8*)&di8);
    data = di8;
    regs[rd] = data;
    pc = pc + 4;
}

void hart::inst_lh() {
    u32 addr;
    u32 data;
    i16 di16;

    addr = regs[rs1] + imm;

    if (addr & 0x01) {
        throw trap{trap_cause_t::load_address_misaligned_exception, addr};
    }

    load(addr, 2, (u8*)&di16);
    data = di16;

    regs[rd] = data;
    pc = pc + 4;
}

void hart::inst_lw() {
    u32 addr;
    u32 data;

    addr = regs[rs1] + imm;

    if (addr & 0x03) {
        throw trap{trap_cause_t::load_address_misaligned_exception, addr};
    }

    load(addr, 4, (u8*)&data);

    regs[rd] = data;
    pc = pc + 4;
}

void hart::inst_lbu() {
    u32 addr;
    u32 data;
    u8 du8;

    addr = regs[rs1] + imm;

    load(addr, 1, (u8*)&du8);
    data = du8;

    regs[rd] = data;
    pc = pc + 4;
}

void hart::inst_lhu() {
    u32 addr;
    u32 data;
    u16 du16;

    addr = regs[rs1] + imm;

    if (addr & 0x01) {
        throw trap{trap_cause_t::load_address_misaligned_exception, addr};
    }

    load(addr, 2, (u8*)&du16);
    data = du16;

    regs[rd] = data;
    pc = pc + 4;
}