
#include <hart.h>

void hart::op_imm()
{
    imm = get_part_s(inst, 31, 20);
    u32 shamt = rs2;

    switch (funct3) {
    case FUNCT3_ADDI:
        regs[rd] = regs[rs1] + imm;
        break;

    case FUNCT3_SLTI:
        regs[rd] = (i32)regs[rs1] < imm ? 1 : 0;
        break;

    case FUNCT3_SLTIU:
        regs[rd] = regs[rs1] < (u32)imm ? 1 : 0;
        break;

    case FUNCT3_XORI:
        regs[rd] = regs[rs1] ^ imm;
        break;

    case FUNCT3_ORI:
        regs[rd] = regs[rs1] | imm;
        break;

    case FUNCT3_ANDI:
        regs[rd] = regs[rs1] & imm;
        break;

    case FUNCT3_SLLI:
        if (funct7 == FUNCT7_SLLI) {
            regs[rd] = regs[rs1] << shamt;
        } else {
            throw trap{trap_cause_t::illegal_instruction_exception, inst};
        }
        break;

    case FUNCT3_SRLI_SRAI:

        switch (funct7) {
        case FUNCT7_SRLI:
            regs[rd] = regs[rs1] >> shamt;
            break;

        case FUNCT7_SRAI:
            regs[rd] = (i32)regs[rs1] >> shamt;
            break;

        default:
            throw trap{trap_cause_t::illegal_instruction_exception, inst};
            break;
        }
        break;

    default:
        throw trap{trap_cause_t::illegal_instruction_exception, inst};
        break;
    }
    pc = pc + 4;
}
