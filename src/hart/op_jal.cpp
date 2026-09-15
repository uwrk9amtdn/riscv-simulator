#include <hart.h>

void hart::op_jal()
{
    u32 target_pc;
    imm = sign_extend(get_part(inst, 31, 31, 20) | get_part(inst, 19, 12, 12) | get_part(inst, 20, 20, 11) | get_part(inst, 30, 21, 1), 20);
    target_pc = pc + imm;
    if (target_pc & 0x3) {
        throw trap{MCAUSE_INSTRUCTION_ADDRESS_MISALIGNED_EXCEPTION, target_pc};
    } else {
        regs[rd] = pc + 4;
        pc = target_pc;
    }
}