#include <hart.h>

hart::handler_t hart::decode_branch()
{
    switch (funct3) {
        case FUNCT3_BEQ:  return &thunk<&hart::inst_beq>;
        case FUNCT3_BNE:  return &thunk<&hart::inst_bne>;
        case FUNCT3_BLT:  return &thunk<&hart::inst_blt>;
        case FUNCT3_BGE:  return &thunk<&hart::inst_bge>;
        case FUNCT3_BLTU: return &thunk<&hart::inst_bltu>;
        case FUNCT3_BGEU: return &thunk<&hart::inst_bgeu>;
    }
    return &thunk<&hart::inst_illegal>;
}

void hart::inst_beq() {
    u32 target_pc;

    target_pc = pc + (regs[rs1] == regs[rs2] ? imm : 4);

    if (target_pc & 0x3) {
        throw trap{trap_cause_t::instruction_address_misaligned_exception, target_pc};
    } else {
        pc = target_pc;
    }
}

void hart::inst_bne() {
    u32 target_pc;

    target_pc = pc + (regs[rs1] != regs[rs2] ? imm : 4);

    if (target_pc & 0x3) {
        throw trap{trap_cause_t::instruction_address_misaligned_exception, target_pc};
    } else {
        pc = target_pc;
    }
}

void hart::inst_blt() {
    u32 target_pc;

    target_pc = pc + ((i32)regs[rs1] < (i32)regs[rs2] ? imm : 4);

    if (target_pc & 0x3) {
        throw trap{trap_cause_t::instruction_address_misaligned_exception, target_pc};
    } else {
        pc = target_pc;
    }
}

void hart::inst_bge() {
    u32 target_pc;

    target_pc = pc + ((i32)regs[rs1] >= (i32)regs[rs2] ? imm : 4);

    if (target_pc & 0x3) {
        throw trap{trap_cause_t::instruction_address_misaligned_exception, target_pc};
    } else {
        pc = target_pc;
    }
}

void hart::inst_bltu() {
    u32 target_pc;

    target_pc = pc + (regs[rs1] < regs[rs2] ? imm : 4);

    if (target_pc & 0x3) {
        throw trap{trap_cause_t::instruction_address_misaligned_exception, target_pc};
    } else {
        pc = target_pc;
    }
}

void hart::inst_bgeu() {
    u32 target_pc;

    target_pc = pc + (regs[rs1] >= regs[rs2] ? imm : 4);

    if (target_pc & 0x3) {
        throw trap{trap_cause_t::instruction_address_misaligned_exception, target_pc};
    } else {
        pc = target_pc;
    }
}
