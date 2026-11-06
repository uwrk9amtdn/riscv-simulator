#include <hart.h>

hart::handler_t hart::decode_store()
{
    switch (funct3) {
        case FUNCT3_SB: return &thunk<&hart::inst_sb>;
        case FUNCT3_SH: return &thunk<&hart::inst_sh>;
        case FUNCT3_SW: return &thunk<&hart::inst_sw>;
    }

    return &thunk<&hart::inst_illegal>;
}

void hart::inst_sb() {
    u32 addr;
    u32 data;

    addr = regs[rs1] + imm;
    data = regs[rs2];
    store(addr, 1, (u8*)&data);

    pc = pc + 4;
}

void hart::inst_sh() {
    u32 addr;
    u32 data;

    addr = regs[rs1] + imm;

    if (addr & 0x01) {
        throw trap{trap_cause_t::store_amo_address_misaligned_exception, addr};
    }

    data = regs[rs2];
    store(addr, 2, (u8*)&data);

    pc = pc + 4;
}

void hart::inst_sw() {
    u32 addr;
    u32 data;

    addr = regs[rs1] + imm;

    if (addr & 0x03) {
        throw trap{trap_cause_t::store_amo_address_misaligned_exception, addr};
    }

    data = regs[rs2];
    store(addr, 4, (u8*)&data);

    pc = pc + 4;
}
