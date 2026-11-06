
#include <hart.h>

hart::handler_t hart::decode_imm()
{
    switch (funct3) {
        case FUNCT3_ADDI:  return &thunk<&hart::inst_addi>;
        case FUNCT3_SLTI:  return &thunk<&hart::inst_slti>;
        case FUNCT3_SLTIU: return &thunk<&hart::inst_sltiu>;
        case FUNCT3_XORI:  return &thunk<&hart::inst_xori>;
        case FUNCT3_ORI:   return &thunk<&hart::inst_ori>;
        case FUNCT3_ANDI:  return &thunk<&hart::inst_andi>;

        case FUNCT3_SLLI: {
            if (funct7 == FUNCT7_SLLI) {
                return &thunk<&hart::inst_slli>;
            } else {
                return &thunk<&hart::inst_illegal>;
            }
        } break;

        case FUNCT3_SRLI_SRAI: {
            switch (funct7) {
                case FUNCT7_SRLI: return &thunk<&hart::inst_srli>;
                case FUNCT7_SRAI: return &thunk<&hart::inst_srai>;
            }
            return &thunk<&hart::inst_illegal>;
        } break;

    }
    return &thunk<&hart::inst_illegal>;
}

void hart::inst_addi() {

    regs[rd] = regs[rs1] + imm;

    pc = pc + 4;
}

void hart::inst_slti() {

    regs[rd] = (i32)regs[rs1] < imm ? 1 : 0;

    pc = pc + 4;
}

void hart::inst_sltiu() {

    regs[rd] = regs[rs1] < (u32)imm ? 1 : 0;

    pc = pc + 4;
}

void hart::inst_xori() {

    regs[rd] = regs[rs1] ^ imm;

    pc = pc + 4;
}

void hart::inst_ori() {

    regs[rd] = regs[rs1] | imm;

    pc = pc + 4;
}

void hart::inst_andi() {

    regs[rd] = regs[rs1] & imm;

    pc = pc + 4;
}

void hart::inst_slli() {
    u32 shamt = rs2;

    regs[rd] = regs[rs1] << shamt;

    pc = pc + 4;
}

void hart::inst_srli() {
    u32 shamt = rs2;

    regs[rd] = regs[rs1] >> shamt;

    pc = pc + 4;
}

void hart::inst_srai() {
    u32 shamt = rs2;

    regs[rd] = (i32)regs[rs1] >> shamt;

    pc = pc + 4;
}