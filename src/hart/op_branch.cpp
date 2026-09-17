#include <hart.h>

void hart::op_branch()
{
    u32 target_pc;
    imm = sign_extend(get_part(inst, 31, 31, 12) | get_part(inst, 7, 7, 11) | get_part(inst, 30, 25, 5) | get_part(inst, 11, 8, 1), 12);

    switch (funct3) {
    case FUNCT3_BEQ:
        target_pc = pc + (regs[rs1] == regs[rs2] ? imm : 4);
        break;
    case FUNCT3_BNE:
        target_pc = pc + (regs[rs1] != regs[rs2] ? imm : 4);
        break;
    case FUNCT3_BLT:
        target_pc = pc + ((i32)regs[rs1] < (i32)regs[rs2] ? imm : 4);
        break;
    case FUNCT3_BGE:
        target_pc = pc + ((i32)regs[rs1] >= (i32)regs[rs2] ? imm : 4);
        break;
    case FUNCT3_BLTU:
        target_pc = pc + (regs[rs1] < regs[rs2] ? imm : 4);
        break;
    case FUNCT3_BGEU:
        target_pc = pc + (regs[rs1] >= regs[rs2] ? imm : 4);
        break;
    default:
        throw trap{trap_cause_t::illegal_instruction_exception, inst};
        break;
    }

    if (target_pc & 0x3) {
        throw trap{trap_cause_t::instruction_address_misaligned_exception, target_pc};
    } else {
        pc = target_pc;
    }
}
