
#include <hart.h>

void hart::decode_imm()
{
    switch (funct3) {
        case FUNCT3_ADDI:  return inst_addi();
        case FUNCT3_SLTI:  return inst_slti();
        case FUNCT3_SLTIU: return inst_sltiu();
        case FUNCT3_XORI:  return inst_xori();
        case FUNCT3_ORI:   return inst_ori();
        case FUNCT3_ANDI:  return inst_andi();

        case FUNCT3_SLLI: {
            if (funct7 == FUNCT7_SLLI) {
                return inst_slli();
            } else {
                throw trap{trap_cause_t::illegal_instruction_exception, inst};
            }
        } break;

        case FUNCT3_SRLI_SRAI: {
            switch (funct7) {
                case FUNCT7_SRLI: return inst_srli();
                case FUNCT7_SRAI: return inst_srai();
            }
            throw trap{trap_cause_t::illegal_instruction_exception, inst};
        } break;

    }
    throw trap{trap_cause_t::illegal_instruction_exception, inst};
}

void hart::inst_addi() {
    imm = get_part_s(inst, 31, 20);

    regs[rd] = regs[rs1] + imm;

    pc = pc + 4;
}

void hart::inst_slti() {
    imm = get_part_s(inst, 31, 20);

    regs[rd] = (i32)regs[rs1] < imm ? 1 : 0;

    pc = pc + 4;
}

void hart::inst_sltiu() {
    imm = get_part_s(inst, 31, 20);

    regs[rd] = regs[rs1] < (u32)imm ? 1 : 0;

    pc = pc + 4;
}

void hart::inst_xori() {
    imm = get_part_s(inst, 31, 20);

    regs[rd] = regs[rs1] ^ imm;

    pc = pc + 4;
}

void hart::inst_ori() {
    imm = get_part_s(inst, 31, 20);

    regs[rd] = regs[rs1] | imm;

    pc = pc + 4;
}

void hart::inst_andi() {
    imm = get_part_s(inst, 31, 20);

    regs[rd] = regs[rs1] & imm;

    pc = pc + 4;
}

void hart::inst_slli() {
    imm = get_part_s(inst, 31, 20);
    u32 shamt = rs2;

    regs[rd] = regs[rs1] << shamt;

    pc = pc + 4;
}

void hart::inst_srli() {
    imm = get_part_s(inst, 31, 20);
    u32 shamt = rs2;

    regs[rd] = regs[rs1] >> shamt;

    pc = pc + 4;
}

void hart::inst_srai() {
    imm = get_part_s(inst, 31, 20);
    u32 shamt = rs2;

    regs[rd] = (i32)regs[rs1] >> shamt;

    pc = pc + 4;
}