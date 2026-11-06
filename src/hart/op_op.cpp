#include <hart.h>

hart::handler_t hart::decode_op()
{
    switch (funct3 | (funct7 << 3)) {
        case FUNCT10_ADD:    return &thunk<&hart::inst_add>;
        case FUNCT10_SUB:    return &thunk<&hart::inst_sub>;
        case FUNCT10_SLL:    return &thunk<&hart::inst_sll>;
        case FUNCT10_SLT:    return &thunk<&hart::inst_slt>;
        case FUNCT10_SLTU:   return &thunk<&hart::inst_sltu>;
        case FUNCT10_XOR:    return &thunk<&hart::inst_xor>;
        case FUNCT10_SRL:    return &thunk<&hart::inst_srl>;
        case FUNCT10_SRA:    return &thunk<&hart::inst_sra>;
        case FUNCT10_OR:     return &thunk<&hart::inst_or>;
        case FUNCT10_AND:    return &thunk<&hart::inst_and>;
        case FUNCT10_MUL:    return &thunk<&hart::inst_mul>;
        case FUNCT10_MULH:   return &thunk<&hart::inst_mulh>;
        case FUNCT10_MULHSU: return &thunk<&hart::inst_mulhsu>;
        case FUNCT10_MULHU:  return &thunk<&hart::inst_mulhu>;
        case FUNCT10_DIV:    return &thunk<&hart::inst_div>;
        case FUNCT10_DIVU:   return &thunk<&hart::inst_divu>;
        case FUNCT10_REM:    return &thunk<&hart::inst_rem>;
        case FUNCT10_REMU:   return &thunk<&hart::inst_remu>;
    }
    return &thunk<&hart::inst_illegal>;
}
void hart::inst_add() {
    regs[rd] = regs[rs1] + regs[rs2];
    pc = pc + 4;
}

void hart::inst_sub() {
    regs[rd] = regs[rs1] - regs[rs2];
    pc = pc + 4;
}

void hart::inst_sll() {
    regs[rd] = regs[rs1] << (regs[rs2] & create_mask(4, 0));
    pc = pc + 4;
}

void hart::inst_slt() {
    regs[rd] = (i32)regs[rs1] < (i32)regs[rs2] ? 1 : 0;
    pc = pc + 4;
}

void hart::inst_sltu() {
    regs[rd] = regs[rs1] < regs[rs2] ? 1 : 0;
    pc = pc + 4;
}

void hart::inst_xor() {
    regs[rd] = regs[rs1] ^ regs[rs2];
    pc = pc + 4;
}

void hart::inst_srl() {
    regs[rd] = regs[rs1] >> (regs[rs2] & create_mask(4, 0));
    pc = pc + 4;
}

void hart::inst_sra() {
    regs[rd] = (i32)regs[rs1] >> (regs[rs2] & create_mask(4, 0));
    pc = pc + 4;
}

void hart::inst_or() {
    regs[rd] = regs[rs1] | regs[rs2];
    pc = pc + 4;
}

void hart::inst_and() {
    regs[rd] = regs[rs1] & regs[rs2];
    pc = pc + 4;
}

void hart::inst_mul() {
    regs[rd] = regs[rs1] * regs[rs2];
    pc = pc + 4;
}

void hart::inst_mulh() {
    regs[rd] = ((i64)(i32)regs[rs1] * (i64)(i32)regs[rs2]) >> 32;
    pc = pc + 4;
}

void hart::inst_mulhsu() {
    regs[rd] = ((i64)(i32)regs[rs1] * (i64)regs[rs2]) >> 32;
    pc = pc + 4;
}

void hart::inst_mulhu() {
    regs[rd] = ((u64)regs[rs1] * (u64)regs[rs2]) >> 32;
    pc = pc + 4;
}

void hart::inst_div() {
    if (regs[rs2] == 0) { // division by zero
        regs[rd] = -1;
    } else if ((i32)regs[rs1] == INT32_MIN && (i32)regs[rs2] == -1) { // overflow
        regs[rd] = INT32_MIN;
    } else {
        regs[rd] = (i32)regs[rs1] / (i32)regs[rs2];
    }
    pc = pc + 4;
}

void hart::inst_divu() {
    if (regs[rs2] == 0) { // divison by zero
        regs[rd] = UINT32_MAX;
    } else {
        regs[rd] = regs[rs1] / regs[rs2];
    }
    pc = pc + 4;
}

void hart::inst_rem() {
    if (regs[rs2] == 0) { // division by zero
        regs[rd] = regs[rs1];
    } else if ((i32)regs[rs1] == INT32_MIN && (i32)regs[rs2] == -1) { // overflow
        regs[rd] = 0;
    } else {
        regs[rd] = (i32)regs[rs1] % (i32)regs[rs2];
    }
    pc = pc + 4;
}

void hart::inst_remu() {
    if (regs[rs2] == 0) { // division by zero
        regs[rd] = regs[rs1];
    } else {
        regs[rd] = regs[rs1] % regs[rs2];
    }
    pc = pc + 4;
}
