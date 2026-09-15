#include <hart.h>

void hart::op_op()
{
    switch (funct3 | (funct7 << 3)) {

    case FUNCT10_ADD:
        regs[rd] = regs[rs1] + regs[rs2];
        break;

    case FUNCT10_SUB:
        regs[rd] = regs[rs1] - regs[rs2];
        break;

    case FUNCT10_SLL:
        regs[rd] = regs[rs1] << (regs[rs2] & create_mask(4, 0));
        break;

    case FUNCT10_SLT:
        regs[rd] = (i32)regs[rs1] < (i32)regs[rs2] ? 1 : 0;
        break;

    case FUNCT10_SLTU:
        regs[rd] = regs[rs1] < regs[rs2] ? 1 : 0;
        break;

    case FUNCT10_XOR:
        regs[rd] = regs[rs1] ^ regs[rs2];
        break;

    case FUNCT10_SRL:
        regs[rd] = regs[rs1] >> (regs[rs2] & create_mask(4, 0));
        break;

    case FUNCT10_SRA:
        regs[rd] = (i32)regs[rs1] >> (regs[rs2] & create_mask(4, 0));
        break;

    case FUNCT10_OR:
        regs[rd] = regs[rs1] | regs[rs2];
        break;

    case FUNCT10_AND:
        regs[rd] = regs[rs1] & regs[rs2];
        break;

    case FUNCT10_MUL:
        regs[rd] = (i32)regs[rs1] * (i32)regs[rs2];
        break;

    case FUNCT10_MULH:
        regs[rd] = ((i64)(i32)regs[rs1] * (i64)(i32)regs[rs2]) >> 32;
        break;

    case FUNCT10_MULHSU:
        regs[rd] = ((i64)(i32)regs[rs1] * (i64)regs[rs2]) >> 32;
        break;

    case FUNCT10_MULHU:
        regs[rd] = ((u64)regs[rs1] * (u64)regs[rs2]) >> 32;
        break;

    case FUNCT10_DIV:
        if (regs[rs2] == 0) { // division by zero
            regs[rd] = -1;
        } else if ((i32)regs[rs1] == INT32_MIN && (i32)regs[rs2] == -1) { // overflow
            regs[rd] = INT32_MIN;
        } else {
            regs[rd] = (i32)regs[rs1] / (i32)regs[rs2];
        }
        break;

    case FUNCT10_DIVU:
        if (regs[rs2] == 0) { // divison by zero
            regs[rd] = UINT32_MAX;
        } else {
            regs[rd] = regs[rs1] / regs[rs2];
        }
        break;

    case FUNCT10_REM:
        if (regs[rs2] == 0) { // division by zero
            regs[rd] = regs[rs1];
        } else if ((i32)regs[rs1] == INT32_MIN && (i32)regs[rs2] == -1) { // overflow
            regs[rd] = 0;
        } else {
            regs[rd] = (i32)regs[rs1] % (i32)regs[rs2];
        }
        break;

    case FUNCT10_REMU:
        if (regs[rs2] == 0) { // division by zero
            regs[rd] = regs[rs1];
        } else {
            regs[rd] = regs[rs1] % regs[rs2];
        }
        break;

    default:
        throw trap{MCAUSE_ILLEGAL_INSTRUCTION_EXCEPTION, inst};
        break;
    }
    pc = pc + 4;
}