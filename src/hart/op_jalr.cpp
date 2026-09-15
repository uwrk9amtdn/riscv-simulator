#include <hart.h>

void hart::op_jalr()
{
    u32 target_pc;

    imm = get_part_s(inst, 31, 20);
    target_pc = (regs[rs1] + imm) & (~1);
    if (target_pc & 0x3) {
        throw trap{MCAUSE_INSTRUCTION_ADDRESS_MISALIGNED_EXCEPTION, target_pc};
    } else {
        regs[rd] = pc + 4;
        pc = target_pc;
    }
}