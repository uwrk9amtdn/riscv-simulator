#include <hart.h>

void hart::decode_branch()
{
    switch (funct3) {
        case FUNCT3_BEQ:  return inst_beq();
        case FUNCT3_BNE:  return inst_bne();
        case FUNCT3_BLT:  return inst_blt();
        case FUNCT3_BGE:  return inst_bge();
        case FUNCT3_BLTU: return inst_bltu();
        case FUNCT3_BGEU: return inst_bgeu();
    }
    throw trap{trap_cause_t::illegal_instruction_exception, inst};
}

void hart::inst_beq() {
    u32 target_pc;
    imm = sign_extend(get_part(inst, 31, 31, 12) | get_part(inst, 7, 7, 11) | get_part(inst, 30, 25, 5) | get_part(inst, 11, 8, 1), 12);

    target_pc = pc + (regs[rs1] == regs[rs2] ? imm : 4);

    if (target_pc & 0x3) {
        throw trap{trap_cause_t::instruction_address_misaligned_exception, target_pc};
    } else {
        pc = target_pc;
    }
}

void hart::inst_bne() {
    u32 target_pc;
    imm = sign_extend(get_part(inst, 31, 31, 12) | get_part(inst, 7, 7, 11) | get_part(inst, 30, 25, 5) | get_part(inst, 11, 8, 1), 12);

    target_pc = pc + (regs[rs1] != regs[rs2] ? imm : 4);

    if (target_pc & 0x3) {
        throw trap{trap_cause_t::instruction_address_misaligned_exception, target_pc};
    } else {
        pc = target_pc;
    }
}

void hart::inst_blt() {
    u32 target_pc;
    imm = sign_extend(get_part(inst, 31, 31, 12) | get_part(inst, 7, 7, 11) | get_part(inst, 30, 25, 5) | get_part(inst, 11, 8, 1), 12);

    target_pc = pc + ((i32)regs[rs1] < (i32)regs[rs2] ? imm : 4);

    if (target_pc & 0x3) {
        throw trap{trap_cause_t::instruction_address_misaligned_exception, target_pc};
    } else {
        pc = target_pc;
    }
}

void hart::inst_bge() {
    u32 target_pc;
    imm = sign_extend(get_part(inst, 31, 31, 12) | get_part(inst, 7, 7, 11) | get_part(inst, 30, 25, 5) | get_part(inst, 11, 8, 1), 12);

    target_pc = pc + ((i32)regs[rs1] >= (i32)regs[rs2] ? imm : 4);

    if (target_pc & 0x3) {
        throw trap{trap_cause_t::instruction_address_misaligned_exception, target_pc};
    } else {
        pc = target_pc;
    }
}

void hart::inst_bltu() {
    u32 target_pc;
    imm = sign_extend(get_part(inst, 31, 31, 12) | get_part(inst, 7, 7, 11) | get_part(inst, 30, 25, 5) | get_part(inst, 11, 8, 1), 12);

    target_pc = pc + (regs[rs1] < regs[rs2] ? imm : 4);

    if (target_pc & 0x3) {
        throw trap{trap_cause_t::instruction_address_misaligned_exception, target_pc};
    } else {
        pc = target_pc;
    }
}

void hart::inst_bgeu() {
    u32 target_pc;
    imm = sign_extend(get_part(inst, 31, 31, 12) | get_part(inst, 7, 7, 11) | get_part(inst, 30, 25, 5) | get_part(inst, 11, 8, 1), 12);

    target_pc = pc + (regs[rs1] >= regs[rs2] ? imm : 4);

    if (target_pc & 0x3) {
        throw trap{trap_cause_t::instruction_address_misaligned_exception, target_pc};
    } else {
        pc = target_pc;
    }
}
